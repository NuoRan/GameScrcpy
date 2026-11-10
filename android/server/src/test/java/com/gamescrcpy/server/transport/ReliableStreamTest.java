package com.gamescrcpy.server.transport;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.fail;

import com.gamescrcpy.protocol.Datagram;
import com.gamescrcpy.protocol.Proofs;
import com.gamescrcpy.protocol.Reliable;
import com.gamescrcpy.server.transport.ReliableStream.Kind;
import com.gamescrcpy.server.transport.ReliableStream.Verdict;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Random;
import org.junit.jupiter.api.Test;

class ReliableStreamTest {

    private static final long T0 = 3_600_000_000L;
    private static final long MS = 1_000;
    private static final byte[] TOKEN = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
    private static final long CONNECTION = 0x0BADF00DL;

    private static byte[] pattern(int size, int seed) {
        final byte[] bytes = new byte[size];
        for (int i = 0; i < size; ++i) {
            bytes[i] = (byte) (seed * 31 + i * 7);
        }
        return bytes;
    }

    private static final class SimulatedLink {
        private record InFlight(long atUs, long order, byte[] bytes) {
        }

        private final long delayUs;
        private final double loss;
        private final double duplicate;
        private final double reorder;
        private final Random random;
        private final List<InFlight> queue = new ArrayList<>();
        private final List<Integer> drop = new ArrayList<>();
        private int count;
        private long order;

        SimulatedLink(long delayUs, double loss, double duplicate, double reorder, long seed) {
            this.delayUs = delayUs;
            this.loss = loss;
            this.duplicate = duplicate;
            this.reorder = reorder;
            this.random = new Random(seed);
        }

        static SimulatedLink clean(long seed) {
            return new SimulatedLink(5 * MS, 0, 0, 0, seed);
        }

        void drop(Integer... numbers) {
            drop.addAll(Arrays.asList(numbers));
        }

        void push(byte[] data, int length, long nowUs) {
            final int number = count++;
            if (drop.contains(number) || chance(loss)) {
                return;
            }
            enqueue(Arrays.copyOf(data, length), nowUs + delayUs + (chance(reorder) ? 3 * MS : 0));
            if (chance(duplicate)) {
                enqueue(Arrays.copyOf(data, length), nowUs + delayUs + MS);
            }
        }

        byte[] pop(long nowUs) {
            if (queue.isEmpty() || queue.get(0).atUs() > nowUs) {
                return null;
            }
            return queue.remove(0).bytes();
        }

        long next() {
            return queue.isEmpty() ? ReliableStream.NEVER : queue.get(0).atUs();
        }

        private boolean chance(double probability) {
            return probability > 0 && random.nextDouble() < probability;
        }

        private void enqueue(byte[] bytes, long atUs) {
            queue.add(new InFlight(atUs, order++, bytes));
            queue.sort(Comparator.comparingLong(InFlight::atUs).thenComparingLong(InFlight::order));
        }
    }

    private static final class Peer {
        final ReliableStream stream;
        final ByteArrayOutputStream sent = new ByteArrayOutputStream();
        final ByteArrayOutputStream received = new ByteArrayOutputStream();
        final List<long[]> arrivals = new ArrayList<>();
        boolean reading = true;
        boolean overfull;
        boolean broken;

        Peer(Kind kind) {
            this(kind, 4 << 20);
        }

        Peer(Kind kind, int receiveLimit) {
            stream = new ReliableStream(kind, CONNECTION, TOKEN, receiveLimit);
        }

        void write(byte[] bytes) {
            stream.send(bytes, 0, bytes.length);
            sent.write(bytes, 0, bytes.length);
        }
    }

    private record Message(long atUs, boolean fromA, byte[] bytes) {
    }

    private static void transmit(Peer peer, SimulatedLink link, long nowUs) {
        for (int length = peer.stream.poll(nowUs); length != 0; length = peer.stream.poll(nowUs)) {
            peer.overfull |= length > Reliable.DATAGRAM_LIMIT;
            link.push(peer.stream.datagram(), length, nowUs);
        }
        peer.overfull |= peer.stream.inFlight() > ReliableStream.WINDOW;
    }

    private static void deliver(SimulatedLink link, Peer peer, long nowUs) {
        for (byte[] datagram = link.pop(nowUs); datagram != null; datagram = link.pop(nowUs)) {
            peer.broken |= peer.stream.receive(datagram, datagram.length, nowUs) == Verdict.BROKEN;
        }
        final int readable = peer.stream.readableBytes();
        if (peer.reading && readable != 0) {
            peer.received.write(peer.stream.readBuffer(), peer.stream.readOffset(), readable);
            peer.stream.consume(readable);
            peer.arrivals.add(new long[] {peer.received.size(), nowUs});
        }
    }

    private static void run(Peer a, Peer b, SimulatedLink ab, SimulatedLink ba, List<Message> messages, long startUs,
                            long untilUs) {
        final List<Message> ordered = new ArrayList<>(messages);
        ordered.sort(Comparator.comparingLong(Message::atUs));
        int next = 0;
        long steps = 0;
        for (long now = startUs; now <= untilUs; ) {
            if (++steps > 2_000_000) {
                fail("the simulation stopped advancing at " + (now - T0) + " us");
            }
            for (; next < ordered.size() && ordered.get(next).atUs() <= now; ++next) {
                (ordered.get(next).fromA() ? a : b).write(ordered.get(next).bytes());
            }
            deliver(ab, b, now);
            deliver(ba, a, now);
            transmit(a, ab, now);
            transmit(b, ba, now);
            long wake = Math.min(Math.min(ab.next(), ba.next()), Math.min(a.stream.deadline(), b.stream.deadline()));
            if (next < ordered.size()) {
                wake = Math.min(wake, ordered.get(next).atUs());
            }
            if (wake == ReliableStream.NEVER) {
                break;
            }
            now = Math.max(wake, now + 1);
        }
    }

    private static List<Message> stream(boolean fromA, int count, long everyUs, int seed) {
        final List<Message> messages = new ArrayList<>();
        for (int i = 0; i < count; ++i) {
            messages.add(new Message(T0 + everyUs * i, fromA, pattern(10 + (i * 37 + seed) % 190, seed + i)));
        }
        return messages;
    }

    private static long[] latencies(List<Message> messages, Peer receiver) {
        final long[] out = new long[messages.size()];
        long end = 0;
        for (int i = 0; i < messages.size(); ++i) {
            end += messages.get(i).bytes().length;
            out[i] = Long.MAX_VALUE;
            for (long[] arrival : receiver.arrivals) {
                if (arrival[0] >= end) {
                    out[i] = arrival[1] - messages.get(i).atUs();
                    break;
                }
            }
        }
        return out;
    }

    private static Reliable.Block blockOf(Kind kind, byte[] datagram) {
        final Reliable.Block block = new Reliable.Block();
        if (kind == Kind.BULK) {
            return Reliable.decodeBulk(datagram, datagram.length, block);
        }
        assertTrue(Reliable.decodeControl(TOKEN, datagram, datagram.length, block) != 0);
        return block;
    }

    private static List<Long> seqsOf(Kind kind, byte[] datagram) {
        final Reliable.Block block = blockOf(kind, datagram);
        final List<Long> seqs = new ArrayList<>();
        while (block.next()) {
            seqs.add(block.seq);
        }
        return seqs;
    }

    private static byte[] poll(Peer peer, long nowUs) {
        final int length = peer.stream.poll(nowUs);
        return length == 0 ? null : Arrays.copyOf(peer.stream.datagram(), length);
    }

    @Test
    void aCleanLinkDeliversEveryMessageOnceInOrderOneLinkDelayAfterItWasWritten() {
        final Peer a = new Peer(Kind.CONTROL);
        final Peer b = new Peer(Kind.CONTROL);
        final List<Message> fromA = stream(true, 250, 4 * MS, 0);
        final List<Message> all = new ArrayList<>(fromA);
        all.addAll(stream(false, 50, 20 * MS, 7));
        run(a, b, SimulatedLink.clean(1), SimulatedLink.clean(2), all, T0, T0 + 3_000 * MS);

        assertArrayEquals(a.sent.toByteArray(), b.received.toByteArray());
        assertArrayEquals(b.sent.toByteArray(), a.received.toByteArray());
        assertTrue(a.stream.settled());
        assertTrue(b.stream.settled());
        assertFalse(a.overfull || b.overfull || a.broken || b.broken);
        assertEquals(0, a.stream.retransmitted());
        assertTrue(a.stream.piggybacked() > 0);
        for (long latency : latencies(fromA, b)) {
            assertEquals(5 * MS, latency);
        }
    }

    @Test
    void aLostControlDatagramsMessagesComeWithTheNextDatagramWithoutWaitingForAResend() {
        final Peer a = new Peer(Kind.CONTROL);
        final Peer b = new Peer(Kind.CONTROL);
        final SimulatedLink ab = SimulatedLink.clean(3);
        ab.drop(10, 11);
        final List<Message> fromA = stream(true, 40, 4 * MS, 1);
        run(a, b, ab, SimulatedLink.clean(4), fromA, T0, T0 + 2_000 * MS);

        assertArrayEquals(a.sent.toByteArray(), b.received.toByteArray());
        assertTrue(a.stream.settled());
        assertEquals(0, a.stream.retransmitted());
        final long[] latency = latencies(fromA, b);
        assertEquals(5 * MS, latency[9]);
        assertEquals(13 * MS, latency[10]);
        assertEquals(9 * MS, latency[11]);
        assertEquals(5 * MS, latency[12]);
    }

    @Test
    void randomLossDuplicationAndReorderingLoseNothingAndReorderNothing() {
        for (double loss : new double[] {0.02, 0.05, 0.10}) {
            final long seed = (long) (loss * 1000);
            final Peer a = new Peer(Kind.CONTROL);
            final Peer b = new Peer(Kind.CONTROL);
            final List<Message> messages = new ArrayList<>(stream(true, 600, 4 * MS, 3));
            messages.addAll(stream(false, 150, 16 * MS, 5));
            messages.add(new Message(T0 + 1_000 * MS, true, pattern(20_000, 9)));
            run(a, b, new SimulatedLink(4 * MS, loss, 0.02, 0.05, seed), new SimulatedLink(4 * MS, loss, 0.02, 0.05, seed + 1),
                    messages, T0, T0 + 30_000 * MS);
            assertArrayEquals(a.sent.toByteArray(), b.received.toByteArray(), "control, loss " + loss);
            assertArrayEquals(b.sent.toByteArray(), a.received.toByteArray(), "control, loss " + loss);
            assertTrue(a.stream.settled() && b.stream.settled(), "control, loss " + loss);
            assertFalse(a.overfull || b.overfull || a.broken || b.broken, "control, loss " + loss);

            final Peer x = new Peer(Kind.BULK);
            final Peer y = new Peer(Kind.BULK);
            run(x, y, new SimulatedLink(4 * MS, loss, 0.02, 0.05, seed + 2), new SimulatedLink(4 * MS, loss, 0.02, 0.05, seed + 3),
                    List.of(new Message(T0, true, pattern(300_000, 11)), new Message(T0 + 50 * MS, false, pattern(50_000, 12))),
                    T0, T0 + 60_000 * MS);
            assertArrayEquals(x.sent.toByteArray(), y.received.toByteArray(), "bulk, loss " + loss);
            assertArrayEquals(y.sent.toByteArray(), x.received.toByteArray(), "bulk, loss " + loss);
            assertTrue(x.stream.settled() && y.stream.settled(), "bulk, loss " + loss);
            assertFalse(x.overfull || y.overfull || x.broken || y.broken, "bulk, loss " + loss);
            assertTrue(x.stream.retransmitted() > 0, "bulk, loss " + loss);
        }
    }

    @Test
    void bulkKeeps32SegmentsInFlightAndResendsALossAsSoonAsTheSelectiveAckShowsIt() {
        final Peer a = new Peer(Kind.BULK);
        final Peer b = new Peer(Kind.BULK);
        a.write(pattern(64 * Reliable.MAX_BULK_SEGMENT, 1));
        final List<byte[]> out = new ArrayList<>();
        for (byte[] datagram = poll(a, T0); datagram != null; datagram = poll(a, T0)) {
            out.add(datagram);
        }
        assertEquals(ReliableStream.WINDOW, out.size());
        assertEquals(ReliableStream.WINDOW, a.stream.inFlight());

        for (int i = 0; i < out.size(); ++i) {
            if (i != 3) {
                assertEquals(Verdict.ACCEPTED, b.stream.receive(out.get(i), out.get(i).length, T0 + 5 * MS));
            }
        }
        final byte[] ack = poll(b, T0 + 5 * MS);
        final Reliable.Block block = blockOf(Kind.BULK, ack);
        assertEquals(0, block.count);
        assertEquals(3, block.ack);
        assertEquals(0x0FFFFFFFL, block.sack);

        assertEquals(Verdict.ACCEPTED, a.stream.receive(ack, ack.length, T0 + 10 * MS));
        assertTrue(a.stream.deadline() <= T0 + 10 * MS);
        assertEquals(List.of(3L), seqsOf(Kind.BULK, poll(a, T0 + 10 * MS)));
        assertEquals(List.of(32L), seqsOf(Kind.BULK, poll(a, T0 + 10 * MS)));
        assertEquals(1, a.stream.retransmitted());

        b.received.write(b.stream.readBuffer(), b.stream.readOffset(), b.stream.readableBytes());
        b.stream.consume(b.stream.readableBytes());
        run(a, b, SimulatedLink.clean(5), SimulatedLink.clean(6), List.of(), T0 + 10 * MS, T0 + 5_000 * MS);
        assertArrayEquals(a.sent.toByteArray(), b.received.toByteArray());
        assertTrue(a.stream.settled());
    }

    @Test
    void acknowledgementsWaitUpTo5MsButNotWhenAGapOpensOrCopiesRepeat() {
        final Peer a = new Peer(Kind.BULK);
        final Peer b = new Peer(Kind.BULK);
        a.write(pattern(100, 2));
        final byte[] only = poll(a, T0);
        assertEquals(Verdict.ACCEPTED, b.stream.receive(only, only.length, T0 + 5 * MS));
        assertEquals(0, b.stream.poll(T0 + 5 * MS));
        assertEquals(T0 + 10 * MS, b.stream.deadline());
        final Reliable.Block pure = blockOf(Kind.BULK, poll(b, T0 + 10 * MS));
        assertEquals(0, pure.count);
        assertEquals(1, pure.ack);
        assertEquals(0, b.stream.poll(T0 + 10 * MS));
        assertEquals(ReliableStream.NEVER, b.stream.deadline());

        final Peer c = new Peer(Kind.BULK);
        final Peer d = new Peer(Kind.BULK);
        c.write(pattern(3 * Reliable.MAX_BULK_SEGMENT, 3));
        final byte[] first = poll(c, T0);
        final byte[] second = poll(c, T0);
        final byte[] third = poll(c, T0);
        assertEquals(Verdict.ACCEPTED, d.stream.receive(second, second.length, T0 + 5 * MS));
        Reliable.Block ack = blockOf(Kind.BULK, poll(d, T0 + 5 * MS));
        assertEquals(0, ack.ack);
        assertEquals(0b1, ack.sack);
        assertEquals(Verdict.ACCEPTED, d.stream.receive(third, third.length, T0 + 6 * MS));
        ack = blockOf(Kind.BULK, poll(d, T0 + 6 * MS));
        assertEquals(0, ack.ack);
        assertEquals(0b11, ack.sack);
        assertEquals(Verdict.ACCEPTED, d.stream.receive(first, first.length, T0 + 7 * MS));
        assertEquals(0, d.stream.poll(T0 + 7 * MS));
        assertEquals(T0 + 12 * MS, d.stream.deadline());
        assertEquals(3 * Reliable.MAX_BULK_SEGMENT, d.stream.readableBytes());
        assertEquals(Verdict.ACCEPTED, d.stream.receive(first, first.length, T0 + 8 * MS));
        ack = blockOf(Kind.BULK, poll(d, T0 + 8 * MS));
        assertEquals(3, ack.ack);
        assertEquals(0, ack.sack);
        assertEquals(1, d.stream.duplicatesReceived());
    }

    @Test
    void timeoutsResendTheOldestSegmentAndDoubleTheTimeoutUpToOneSecond() {
        final Peer a = new Peer(Kind.CONTROL);
        a.write(pattern(30, 4));
        assertTrue(a.stream.poll(T0) != 0);
        final List<Long> gaps = new ArrayList<>();
        long last = T0;
        for (int i = 0; i < 6; ++i) {
            final long at = a.stream.deadline();
            assertEquals(List.of(0L), seqsOf(Kind.CONTROL, poll(a, at)));
            assertEquals(0, a.stream.poll(at));
            gaps.add((at - last) / MS);
            last = at;
        }
        assertEquals(List.of(100L, 200L, 400L, 800L, 1000L, 1000L), gaps);
        assertEquals(6, a.stream.retransmitted());
    }

    @Test
    void theRoundTripTimeIsMeasuredAndTheTimeoutFollowsItDownTo20Ms() {
        final Peer a = new Peer(Kind.CONTROL);
        final Peer b = new Peer(Kind.CONTROL);
        assertEquals(100 * MS, a.stream.rto());
        run(a, b, SimulatedLink.clean(7), SimulatedLink.clean(8), stream(true, 20, 50 * MS, 2), T0, T0 + 2_000 * MS);
        assertEquals(15 * MS, a.stream.smoothedRttUs());
        assertEquals(20 * MS, a.stream.rto());
    }

    private static byte[] signedControl(Reliable.Writer writer) {
        final int length = writer.finish(TOKEN);
        return Arrays.copyOf(writer.buffer(), length);
    }

    @Test
    void forgedReplayedAndNonsensicalDatagramsAreDroppedOrEndTheSessionWhenAuthentic() {
        final Peer a = new Peer(Kind.CONTROL);
        final Peer b = new Peer(Kind.CONTROL);
        a.write(pattern(40, 5));
        final byte[] datagram = poll(a, T0);
        assertEquals(Verdict.ACCEPTED, b.stream.receive(datagram, datagram.length, T0 + 5 * MS));
        assertEquals(Verdict.DROPPED, b.stream.receive(datagram, datagram.length, T0 + 6 * MS));
        assertEquals("a replayed control datagram", b.stream.problem());
        final byte[] tampered = datagram.clone();
        tampered[30] ^= 0x40;
        assertEquals(Verdict.DROPPED, b.stream.receive(tampered, tampered.length, T0 + 6 * MS));
        assertEquals(Verdict.DROPPED, b.stream.receive(datagram, Reliable.CONTROL_OVERHEAD - 1, T0 + 6 * MS));

        final Reliable.Writer writer = new Reliable.Writer(new byte[Reliable.DATAGRAM_LIMIT]);
        for (long counter = 1000; counter <= 1070; ++counter) {
            if (counter != 1001 && counter != 1050) {
                final byte[] pure = signedControl(writer.startControl(CONNECTION, counter, 0, 0));
                assertEquals(Verdict.ACCEPTED, b.stream.receive(pure, pure.length, T0 + 7 * MS));
            }
        }
        final byte[] late = signedControl(writer.startControl(CONNECTION, 1050, 0, 0));
        assertEquals(Verdict.ACCEPTED, b.stream.receive(late, late.length, T0 + 8 * MS));
        assertEquals(Verdict.DROPPED, b.stream.receive(late, late.length, T0 + 8 * MS));
        final byte[] old = signedControl(writer.startControl(CONNECTION, 1001, 0, 0));
        assertEquals(Verdict.DROPPED, b.stream.receive(old, old.length, T0 + 8 * MS));

        final byte[] beyond = signedControl(writer.startControl(CONNECTION, 1000, 500, 0));
        assertEquals(Verdict.BROKEN, a.stream.receive(beyond, beyond.length, T0 + 9 * MS));
        writer.startControl(CONNECTION, 1001, 0, 0);
        final byte[] malformed = Arrays.copyOf(writer.buffer(), Datagram.HEADER_SIZE + Reliable.COUNTER_SIZE
                + Reliable.ACK_BLOCK_SIZE + 1);
        malformed[Datagram.HEADER_SIZE + Reliable.COUNTER_SIZE + 8] = 0;
        final byte[] sealed = Arrays.copyOf(malformed, malformed.length + Datagram.CONTROL_TAG_SIZE);
        System.arraycopy(Proofs.controlTag(TOKEN, malformed, 0, malformed.length), 0, sealed, malformed.length,
                Datagram.CONTROL_TAG_SIZE);
        assertEquals(Verdict.BROKEN, a.stream.receive(sealed, sealed.length, T0 + 9 * MS));

        final Peer c = new Peer(Kind.BULK);
        c.write(pattern(10, 7));
        assertTrue(c.stream.poll(T0) != 0);
        final int bulkLength = writer.startBulk(CONNECTION, 9, 0).finish();
        final byte[] bulk = Arrays.copyOf(writer.buffer(), bulkLength + 1);
        assertEquals(Verdict.DROPPED, c.stream.receive(bulk, bulkLength, T0 + MS));
        assertEquals(Verdict.DROPPED, c.stream.receive(bulk, bulkLength + 1, T0 + MS));
    }

    @Test
    void aReaderThatFallsBehindHoldsTheSenderBackWithoutLosingAnything() {
        final int limit = 4000;
        final Peer a = new Peer(Kind.BULK);
        final Peer b = new Peer(Kind.BULK, limit);
        final SimulatedLink ab = SimulatedLink.clean(9);
        final SimulatedLink ba = SimulatedLink.clean(10);
        b.reading = false;
        run(a, b, ab, ba, List.of(new Message(T0, true, pattern(20_000, 8))), T0, T0 + 500 * MS);
        assertTrue(b.stream.readableBytes() >= limit);
        assertTrue(b.stream.readableBytes() <= limit + Reliable.MAX_BULK_SEGMENT);
        assertFalse(a.stream.settled());

        b.reading = true;
        run(a, b, ab, ba, List.of(), T0 + 500 * MS, T0 + 10_000 * MS);
        assertArrayEquals(a.sent.toByteArray(), b.received.toByteArray());
        assertTrue(a.stream.settled());
    }
}
