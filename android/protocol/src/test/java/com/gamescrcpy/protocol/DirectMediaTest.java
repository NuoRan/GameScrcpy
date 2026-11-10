package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.jupiter.api.Test;

class DirectMediaTest {

    private static JSONObject vector() throws IOException {
        final Path directory = Path.of(System.getProperty("gs.protocol.vectorDir", "../../protocol/vectors"));
        final String text = Files.readString(directory.resolve("direct").resolve("media.json"), StandardCharsets.UTF_8);
        return new JSONObject(text).getJSONArray("vectors").getJSONObject(0);
    }

    @Test
    void mediaChannelsOpenWithAnEmptyDatagram() throws IOException {
        final JSONObject vector = vector();
        final long connection = vector.getLong("connection");
        final byte[] header = new byte[Datagram.HEADER_SIZE];
        Datagram.writeHeader(header, 0, DirectMedia.VIDEO_CHANNEL, connection);
        assertEquals(vector.getJSONObject("open").getString("video"), Json.toHex(header));
        Datagram.writeHeader(header, 0, DirectMedia.AUDIO_CHANNEL, connection);
        assertEquals(vector.getJSONObject("open").getString("audio"), Json.toHex(header));
        assertEquals(vector.getInt("maxShardSize"), DirectMedia.MAX_SHARD_SIZE);
    }

    @Test
    void closedAndItsAcknowledgementAreOneZeroByteAfterTheHeader() throws IOException {
        final JSONObject vector = vector();
        final long connection = vector.getLong("connection");
        final byte[] closed = new byte[DirectMedia.CLOSED_SIZE];
        for (String name : new String[] {"video", "audio"}) {
            final int channel = name.equals("video") ? DirectMedia.VIDEO_CHANNEL : DirectMedia.AUDIO_CHANNEL;
            assertEquals(DirectMedia.CLOSED_SIZE, DirectMedia.writeClosed(closed, channel, connection));
            assertEquals(vector.getJSONObject("closed").getString(name), Json.toHex(closed));
            assertTrue(DirectMedia.isClosed(closed, closed.length));
            closed[Datagram.HEADER_SIZE] = 1;
            assertFalse(DirectMedia.isClosed(closed, closed.length));
            assertFalse(DirectMedia.isClosed(closed, Datagram.HEADER_SIZE));
        }
    }

    @Test
    void theSlicerWritesEveryShardDatagramByteForByte() throws IOException {
        final JSONObject vector = vector();
        final long connection = vector.getLong("connection");
        final JSONArray video = vector.getJSONArray("video");
        final DirectMedia.VideoSlicer slicer = new DirectMedia.VideoSlicer();
        for (int c = 0; c < video.length(); ++c) {
            final JSONObject entry = video.getJSONObject(c);
            final String name = entry.getString("name");
            final byte[] packet = Json.fromHex(entry.getString("bytes"));
            final JSONArray parity = entry.getJSONArray("parity");
            final List<String> written = new ArrayList<>();
            slicer.slice(connection, entry.getLong("packet"), packet, 0, packet.length, entry.getInt("shardSize"),
                    (group, k) -> parity.getInt(group), (datagram, length) -> written.add(Json.toHex(datagram, 0, length)));
            final JSONArray expected = entry.getJSONArray("datagrams");
            assertEquals(expected.length(), written.size(), name);
            for (int i = 0; i < expected.length(); ++i) {
                assertEquals(expected.getString(i), written.get(i), name + " datagram " + i);
                final byte[] datagram = Json.fromHex(expected.getString(i));
                final DirectMedia.VideoShard shard = DirectMedia.decodeVideoShard(datagram, datagram.length);
                assertEquals(packet.length, shard.packetSize, name);
                assertEquals(parity.getInt(shard.group), shard.m, name);
            }
        }
        if (video.length() > 0) {
            final byte[] key = Json.fromHex(video.getJSONObject(0).getString("bytes"));
            assertEquals(video.getJSONObject(0).getInt("shardSize"), DirectMedia.recommendedShardSize(key.length));
        }
    }

    @Test
    void audioDatagramsCarryThePreviousPacket() throws IOException {
        final JSONObject vector = vector();
        final long connection = vector.getLong("connection");
        final JSONObject audio = vector.getJSONObject("audio");
        final JSONArray packets = audio.getJSONArray("packets");
        final JSONArray datagrams = audio.getJSONArray("datagrams");
        final byte[] out = new byte[Reliable.DATAGRAM_LIMIT];
        for (int i = 0; i < datagrams.length(); ++i) {
            final byte[] current = Json.fromHex(packets.getString(i));
            final byte[] previous = i > 0 ? Json.fromHex(packets.getString(i - 1)) : null;
            final int length = DirectMedia.writeAudio(out, connection, i, current, current.length, previous,
                    previous == null ? 0 : previous.length);
            assertEquals(datagrams.getString(i), Json.toHex(out, 0, length), "datagram " + i);
            final DirectMedia.AudioDatagram decoded = DirectMedia.decodeAudio(out, length);
            assertEquals(i, decoded.packet);
            assertEquals(i == 0 ? 1 : 2, decoded.count);
        }
    }

    @Test
    void malformedMediaDatagramsReportTheErrorTheSpecificationOrders() throws IOException {
        final JSONObject vector = vector();
        final JSONArray video = vector.getJSONArray("videoMalformed");
        for (int i = 0; i < video.length(); ++i) {
            final JSONObject entry = video.getJSONObject(i);
            final byte[] data = Json.fromHex(entry.getString("hex"));
            final DecodeException error = assertThrows(DecodeException.class,
                    () -> DirectMedia.decodeVideoShard(data, data.length), entry.getString("name"));
            assertEquals(entry.getString("error"), error.error().specName(), entry.getString("name"));
        }
        final JSONArray audio = vector.getJSONArray("audioMalformed");
        for (int i = 0; i < audio.length(); ++i) {
            final JSONObject entry = audio.getJSONObject(i);
            final byte[] data = Json.fromHex(entry.getString("hex"));
            final DecodeException error = assertThrows(DecodeException.class,
                    () -> DirectMedia.decodeAudio(data, data.length), entry.getString("name"));
            assertEquals(entry.getString("error"), error.error().specName(), entry.getString("name"));
        }
    }
}
