package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.jupiter.api.Test;

class ReliableTest {

    private static JSONObject vector() throws IOException {
        final Path directory = Path.of(System.getProperty("gs.protocol.vectorDir", "../../protocol/vectors"));
        final String text = Files.readString(directory.resolve("direct").resolve("reliable.json"), StandardCharsets.UTF_8);
        return new JSONObject(text).getJSONArray("vectors").getJSONObject(0);
    }

    private static void checkBlock(Reliable.Block block, byte[] data, JSONObject entry) {
        final String name = entry.getString("name");
        assertEquals(entry.getLong("ack"), block.ack, name);
        assertEquals(entry.getLong("sack"), block.sack, name);
        final JSONArray segments = entry.getJSONArray("segments");
        assertEquals(segments.length(), block.count, name);
        for (int i = 0; i < segments.length(); ++i) {
            assertTrue(block.next(), name);
            assertEquals(segments.getJSONObject(i).getLong("seq"), block.seq, name);
            assertEquals(segments.getJSONObject(i).getString("hex"), Json.toHex(data, block.offset, block.length), name);
        }
        assertFalse(block.next(), name);
    }

    @Test
    void datagramsDecodeToTheirFieldsAndEncodeByteForByte() throws IOException {
        final JSONObject vector = vector();
        assertEquals(vector.getJSONObject("maxSegment").getInt("control"), Reliable.MAX_CONTROL_SEGMENT);
        assertEquals(vector.getJSONObject("maxSegment").getInt("bulk"), Reliable.MAX_BULK_SEGMENT);
        final byte[] token = Json.fromHex(vector.getString("token"));
        final long connection = vector.getLong("connection");
        final JSONArray datagrams = vector.getJSONArray("datagrams");
        final Reliable.Block block = new Reliable.Block();
        final Reliable.Writer writer = new Reliable.Writer(new byte[Reliable.DATAGRAM_LIMIT]);

        for (int i = 0; i < datagrams.length(); ++i) {
            final JSONObject entry = datagrams.getJSONObject(i);
            final String name = entry.getString("name");
            final byte[] data = Json.fromHex(entry.getString("hex"));
            assertTrue(data.length <= Reliable.DATAGRAM_LIMIT, name);
            final Datagram.Header header = Datagram.decodeHeader(data, 0, data.length);
            assertEquals(entry.getInt("channel"), header.channel, name);
            assertEquals(connection, header.connection, name);

            final int length;
            if (header.channel == Reliable.CONTROL_CHANNEL) {
                final long counter = Reliable.decodeControl(token, data, data.length, block);
                assertEquals(Long.parseUnsignedLong(entry.getString("counter")), counter, name);
                checkBlock(block, data, entry);
                writer.startControl(connection, counter, block.ack, block.sack);
            } else {
                Reliable.decodeBulk(data, data.length, block);
                checkBlock(block, data, entry);
                writer.startBulk(connection, block.ack, block.sack);
            }
            final JSONArray segments = entry.getJSONArray("segments");
            for (int s = 0; s < segments.length(); ++s) {
                final byte[] bytes = Json.fromHex(segments.getJSONObject(s).getString("hex"));
                assertTrue(writer.add(segments.getJSONObject(s).getLong("seq"), bytes, 0, bytes.length), name);
            }
            length = header.channel == Reliable.CONTROL_CHANNEL ? writer.finish(token) : writer.finish();
            assertEquals(entry.getString("hex"), Json.toHex(writer.buffer(), 0, length), name);
        }
    }

    @Test
    void malformedDatagramsReportTheErrorTheSpecificationOrders() throws IOException {
        final JSONObject vector = vector();
        final byte[] token = Json.fromHex(vector.getString("token"));
        final JSONArray malformed = vector.getJSONArray("malformed");
        final Reliable.Block block = new Reliable.Block();
        for (int i = 0; i < malformed.length(); ++i) {
            final JSONObject entry = malformed.getJSONObject(i);
            final String name = entry.getString("name");
            final String expected = entry.getString("error");
            final byte[] data = Json.fromHex(entry.getString("hex"));
            if (entry.getInt("channel") == Reliable.CONTROL_CHANNEL) {
                if (expected.equals("BadTag")) {
                    assertEquals(0, Reliable.decodeControl(token, data, data.length, block), name);
                    continue;
                }
                final DecodeException error = assertThrows(DecodeException.class,
                        () -> Reliable.decodeControl(token, data, data.length, block), name);
                assertEquals(expected, error.error().specName(), name);
            } else {
                final DecodeException error = assertThrows(DecodeException.class,
                        () -> Reliable.decodeBulk(data, data.length, block), name);
                assertEquals(expected, error.error().specName(), name);
            }
        }
    }

    @Test
    void theWriterKeepsDatagramsWithin1200Bytes() {
        final byte[] big = new byte[Reliable.MAX_BULK_SEGMENT + 1];
        Arrays.fill(big, (byte) 0xAB);
        final Reliable.Writer writer = new Reliable.Writer(new byte[Reliable.DATAGRAM_LIMIT]);

        writer.startControl(7, 1, 0, 0);
        assertEquals(Reliable.MAX_CONTROL_SEGMENT, writer.room());
        assertFalse(writer.add(0, big, 0, Reliable.MAX_CONTROL_SEGMENT + 1));
        assertFalse(writer.add(0, big, 0, 0));
        assertTrue(writer.add(0, big, 0, 100));
        assertEquals(Reliable.MAX_CONTROL_SEGMENT - 100 - Reliable.SEGMENT_HEADER_SIZE, writer.room());
        assertTrue(writer.add(1, big, 0, writer.room()));
        assertEquals(0, writer.room());
        assertEquals(2, writer.count());
        assertEquals(Reliable.DATAGRAM_LIMIT, writer.finish(new byte[16]));

        writer.startBulk(7, 0, 0);
        assertEquals(Reliable.MAX_BULK_SEGMENT, writer.room());
        assertFalse(writer.add(0, big, 0, big.length));
        assertTrue(writer.add(0, big, 0, Reliable.MAX_BULK_SEGMENT));
        assertEquals(0, writer.room());
        assertEquals(Reliable.DATAGRAM_LIMIT, writer.finish());
        assertThrows(IllegalArgumentException.class, () -> new Reliable.Writer(new byte[Reliable.DATAGRAM_LIMIT - 1]));
    }
}
