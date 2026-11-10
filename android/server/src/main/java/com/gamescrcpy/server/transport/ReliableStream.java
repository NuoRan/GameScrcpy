package com.gamescrcpy.server.transport;

import com.gamescrcpy.protocol.DecodeException;
import com.gamescrcpy.protocol.Reliable;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class ReliableStream {

    public enum Kind {
        CONTROL,
        BULK
    }

    public enum Verdict {
        ACCEPTED,
        DROPPED,
        BROKEN
    }

    public static final int WINDOW = 32;
    public static final int PIGGYBACK = 3;
    public static final long ACK_DELAY_US = 5_000;
    public static final long MIN_RTO_US = 20_000;
    public static final long INITIAL_RTO_US = 100_000;
    public static final long MAX_RTO_US = 1_000_000;
    public static final long NOW = Long.MIN_VALUE;
    public static final long NEVER = Long.MAX_VALUE;

    private static final int MAX_BACKOFF = 6;

    private static final class Outgoing {
        final long seq;
        final byte[] bytes;
        long firstSentUs;
        long lastSentUs;
        long lastTransmission;
        boolean acked;
        boolean lost;
        boolean resent;

        Outgoing(long seq, byte[] bytes) {
            this.seq = seq;
            this.bytes = bytes;
        }
    }

    private final Kind kind;
    private final long connection;
    private final byte[] token;
    private final int receiveLimit;
    private final int maxSegment;
    private final Reliable.Block block = new Reliable.Block();
    private final Reliable.Writer writer = new Reliable.Writer(new byte[Reliable.DATAGRAM_LIMIT]);
    private final int[] chosen = new int[255];

    private final List<Outgoing> outgoing = new ArrayList<>();
    private int head;
    private long base;
    private long sentEnd;
    private long nextSeq;
    private long transmissions;
    private long counter;
    private int backoff;
    private long srttUs = -1;
    private long rttvarUs;

    private long ack;
    private final byte[][] held = new byte[WINDOW + 1][];
    private byte[] readBuffer = new byte[16 * 1024];
    private int readStart;
    private int readEnd;
    private boolean ackDue;
    private long ackDueUs;
    private long highestCounter;
    private long seenCounters;

    private long datagramsSent;
    private long segmentsSent;
    private long retransmitted;
    private long piggybacked;
    private long duplicatesReceived;
    private String problem;

    public ReliableStream(Kind kind, long connection, byte[] token, int receiveLimit) {
        this.kind = kind;
        this.connection = connection;
        this.token = token.clone();
        this.receiveLimit = receiveLimit;
        this.maxSegment = kind == Kind.CONTROL ? Reliable.MAX_CONTROL_SEGMENT : Reliable.MAX_BULK_SEGMENT;
    }

    public void send(byte[] data, int offset, int length) {
        while (length > 0) {
            final int size = Math.min(length, maxSegment);
            outgoing.add(new Outgoing(nextSeq++, Arrays.copyOfRange(data, offset, offset + size)));
            offset += size;
            length -= size;
        }
    }

    public Verdict receive(byte[] data, int length, long nowUs) {
        if (kind == Kind.CONTROL) {
            if (length < Reliable.CONTROL_OVERHEAD) {
                return refuse(Verdict.DROPPED, "a control datagram too short for its tag");
            }
            final long datagramCounter;
            try {
                datagramCounter = Reliable.decodeControl(token, data, length, block);
            } catch (DecodeException e) {
                return refuse(Verdict.BROKEN, "a malformed control datagram (" + e.error().specName() + ")");
            }
            if (datagramCounter == 0) {
                return refuse(Verdict.DROPPED, "a control datagram with a wrong tag");
            }
            if (!acceptCounter(datagramCounter)) {
                return refuse(Verdict.DROPPED, "a replayed control datagram");
            }
        } else {
            try {
                Reliable.decodeBulk(data, length, block);
            } catch (DecodeException e) {
                return refuse(Verdict.DROPPED, "a malformed bulk datagram (" + e.error().specName() + ")");
            }
        }
        if (!acknowledge(nowUs)) {
            return refuse(kind == Kind.CONTROL ? Verdict.BROKEN : Verdict.DROPPED,
                    "an acknowledgement of segments never sent");
        }
        take(data, nowUs);
        problem = null;
        return Verdict.ACCEPTED;
    }

    public String problem() {
        return problem;
    }

    public int poll(long nowUs) {
        final Outgoing oldest = oldestUnacked();
        if (oldest != null && nowUs - oldest.lastSentUs >= rto()) {
            oldest.lost = true;
            oldest.resent = true;
            backoff = Math.min(backoff + 1, MAX_BACKOFF);
        }

        int count = 0;
        int room = Reliable.DATAGRAM_LIMIT - (kind == Kind.CONTROL ? Reliable.CONTROL_OVERHEAD : Reliable.BULK_OVERHEAD);
        final int sent = sentCount();
        int retransmissions = 0;
        for (int i = 0; i < sent; ++i) {
            final Outgoing segment = at(i);
            if (segment.lost && !segment.acked && fits(segment, room, count)) {
                chosen[count++] = i;
                room -= Reliable.SEGMENT_HEADER_SIZE + segment.bytes.length;
                ++retransmissions;
            }
        }
        int fresh = 0;
        for (int i = sent; i < size(); ++i) {
            final Outgoing segment = at(i);
            if (segment.seq - base >= WINDOW || !fits(segment, room, count)) {
                break;
            }
            chosen[count++] = i;
            room -= Reliable.SEGMENT_HEADER_SIZE + segment.bytes.length;
            ++fresh;
        }
        int riders = 0;
        if (kind == Kind.CONTROL && fresh != 0) {
            for (int i = sent - 1; i >= 0 && riders < PIGGYBACK; --i) {
                final Outgoing segment = at(i);
                if (!segment.acked && !segment.lost && fits(segment, room, count)) {
                    chosen[count++] = i;
                    room -= Reliable.SEGMENT_HEADER_SIZE + segment.bytes.length;
                    ++riders;
                }
            }
        }
        if (count == 0 && !(ackDue && nowUs >= ackDueUs)) {
            return 0;
        }
        Arrays.sort(chosen, 0, count);

        if (kind == Kind.CONTROL) {
            writer.startControl(connection, ++counter, ack, sackBits());
        } else {
            writer.startBulk(connection, ack, sackBits());
        }
        final long transmission = ++transmissions;
        for (int k = 0; k < count; ++k) {
            final Outgoing segment = at(chosen[k]);
            if (!writer.add(segment.seq, segment.bytes, 0, segment.bytes.length)) {
                throw new IllegalStateException("a chosen segment did not fit");
            }
            if (segment.lastTransmission == 0) {
                segment.firstSentUs = nowUs;
                sentEnd = Math.max(sentEnd, segment.seq + 1);
            }
            segment.lastSentUs = nowUs;
            segment.lastTransmission = transmission;
            segment.lost = false;
        }
        ackDue = false;
        ++datagramsSent;
        segmentsSent += count;
        retransmitted += retransmissions;
        piggybacked += riders;
        return kind == Kind.CONTROL ? writer.finish(token) : writer.finish();
    }

    public byte[] datagram() {
        return writer.buffer();
    }

    public long deadline() {
        if (sendable()) {
            return NOW;
        }
        long next = NEVER;
        if (ackDue) {
            next = Math.min(next, ackDueUs);
        }
        final Outgoing oldest = oldestUnacked();
        if (oldest != null) {
            next = Math.min(next, oldest.lastSentUs + rto());
        }
        return next;
    }

    public int readableBytes() {
        return readEnd - readStart;
    }

    public byte[] readBuffer() {
        return readBuffer;
    }

    public int readOffset() {
        return readStart;
    }

    public void consume(int count) {
        readStart += Math.min(count, readEnd - readStart);
        if (readStart == readEnd) {
            readStart = 0;
            readEnd = 0;
        }
    }

    public boolean settled() {
        return size() == 0;
    }

    public boolean acknowledgementPending() {
        return ackDue;
    }

    public long inFlight() {
        return sentEnd - base;
    }

    public long rto() {
        long timeout = srttUs < 0 ? INITIAL_RTO_US : Math.max(MIN_RTO_US, srttUs + 4 * rttvarUs);
        for (int i = 0; i < backoff && timeout < MAX_RTO_US; ++i) {
            timeout *= 2;
        }
        return Math.min(timeout, MAX_RTO_US);
    }

    public long smoothedRttUs() {
        return srttUs;
    }

    public long datagramsSent() {
        return datagramsSent;
    }

    public long segmentsSent() {
        return segmentsSent;
    }

    public long retransmitted() {
        return retransmitted;
    }

    public long piggybacked() {
        return piggybacked;
    }

    public long duplicatesReceived() {
        return duplicatesReceived;
    }

    private Verdict refuse(Verdict verdict, String why) {
        problem = why;
        return verdict;
    }

    private int size() {
        return outgoing.size() - head;
    }

    private Outgoing at(int index) {
        return outgoing.get(head + index);
    }

    private int sentCount() {
        return (int) (sentEnd - base);
    }

    private static boolean fits(Outgoing segment, int room, int count) {
        return count < 255 && Reliable.SEGMENT_HEADER_SIZE + segment.bytes.length <= room;
    }

    private Outgoing oldestUnacked() {
        final int sent = sentCount();
        for (int i = 0; i < sent; ++i) {
            final Outgoing segment = at(i);
            if (!segment.acked && !segment.lost) {
                return segment;
            }
        }
        return null;
    }

    private boolean sendable() {
        final int sent = sentCount();
        for (int i = 0; i < sent; ++i) {
            if (at(i).lost && !at(i).acked) {
                return true;
            }
        }
        return sent < size() && at(sent).seq - base < WINDOW;
    }

    private boolean acceptCounter(long value) {
        if (Long.compareUnsigned(value, highestCounter) > 0) {
            final long shift = value - highestCounter;
            seenCounters = Long.compareUnsigned(shift, 64) >= 0 ? 0 : seenCounters << shift;
            seenCounters |= 1;
            highestCounter = value;
            return true;
        }
        final long age = highestCounter - value;
        if (Long.compareUnsigned(age, 64) >= 0 || ((seenCounters >>> age) & 1) != 0) {
            return false;
        }
        seenCounters |= 1L << age;
        return true;
    }

    private boolean acknowledge(long nowUs) {
        final long cumulative = block.ack;
        final long selective = block.sack;
        if (cumulative > sentEnd
                || (selective != 0 && cumulative + 1 + (63 - Long.numberOfLeadingZeros(selective)) >= sentEnd)) {
            return false;
        }
        Outgoing newest = null;
        long newestTransmission = 0;
        final int sent = sentCount();
        for (int i = 0; i < sent; ++i) {
            final Outgoing segment = at(i);
            if (segment.acked) {
                continue;
            }
            boolean covered = segment.seq < cumulative;
            if (!covered && segment.seq > cumulative && segment.seq - cumulative - 1 < 32) {
                covered = ((selective >>> (segment.seq - cumulative - 1)) & 1) != 0;
            }
            if (!covered) {
                continue;
            }
            segment.acked = true;
            segment.lost = false;
            if (newest == null || segment.seq > newest.seq) {
                newest = segment;
            }
            newestTransmission = Math.max(newestTransmission, segment.lastTransmission);
        }
        if (newest != null && !newest.resent) {
            sample(nowUs - newest.firstSentUs);
        }
        for (int i = 0; i < sent; ++i) {
            final Outgoing segment = at(i);
            if (!segment.acked && !segment.lost && segment.lastTransmission < newestTransmission) {
                segment.lost = true;
                segment.resent = true;
            }
        }
        if (cumulative > base) {
            while (head < outgoing.size() && outgoing.get(head).seq < cumulative) {
                outgoing.set(head++, null);
            }
            if (head > 1024 && 2 * head > outgoing.size()) {
                outgoing.subList(0, head).clear();
                head = 0;
            }
            base = cumulative;
            backoff = 0;
        }
        return true;
    }

    private void take(byte[] data, long nowUs) {
        if (block.count == 0) {
            return;
        }
        final boolean full = readEnd - readStart >= receiveLimit;
        boolean fresh = false;
        while (block.next()) {
            if (block.seq < ack) {
                ++duplicatesReceived;
                continue;
            }
            final long offset = block.seq - ack;
            if (offset > WINDOW || full) {
                continue;
            }
            if (held[(int) offset] != null) {
                ++duplicatesReceived;
                continue;
            }
            held[(int) offset] = Arrays.copyOfRange(data, block.offset, block.offset + block.length);
            fresh = true;
        }
        while (held[0] != null) {
            append(held[0]);
            System.arraycopy(held, 1, held, 0, WINDOW);
            held[WINDOW] = null;
            ++ack;
        }
        boolean gap = false;
        for (int i = 1; i <= WINDOW && !gap; ++i) {
            gap = held[i] != null;
        }
        if (gap || !fresh) {
            ackDue = true;
            ackDueUs = nowUs;
        } else if (!ackDue) {
            ackDue = true;
            ackDueUs = nowUs + ACK_DELAY_US;
        }
    }

    private void append(byte[] bytes) {
        if (readEnd + bytes.length > readBuffer.length) {
            final int kept = readEnd - readStart;
            final byte[] target = kept + bytes.length > readBuffer.length
                    ? new byte[Math.max(2 * readBuffer.length, kept + bytes.length)]
                    : readBuffer;
            System.arraycopy(readBuffer, readStart, target, 0, kept);
            readBuffer = target;
            readStart = 0;
            readEnd = kept;
        }
        System.arraycopy(bytes, 0, readBuffer, readEnd, bytes.length);
        readEnd += bytes.length;
    }

    private void sample(long rttUs) {
        if (srttUs < 0) {
            srttUs = rttUs;
            rttvarUs = rttUs / 2;
            return;
        }
        rttvarUs = (3 * rttvarUs + Math.abs(srttUs - rttUs)) / 4;
        srttUs = (7 * srttUs + rttUs) / 8;
    }

    private long sackBits() {
        long bits = 0;
        for (int i = 0; i < WINDOW; ++i) {
            if (held[i + 1] != null) {
                bits |= 1L << i;
            }
        }
        return bits;
    }
}
