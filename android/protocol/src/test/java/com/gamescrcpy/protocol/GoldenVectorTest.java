package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Messages.AudioSessionInfo;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestFactory;

class GoldenVectorTest {

    private static final List<Vector> VECTORS = loadVectors();

    private record Vector(String file, JSONObject data) {
        String label() {
            return file + ": " + data.getString("name");
        }

        boolean malformed() {
            return data.has("error");
        }
    }

    private static List<Vector> loadVectors() {
        final Path directory = Path.of(System.getProperty("gs.protocol.vectorDir", "../../protocol/vectors"));
        try (Stream<Path> files = Files.list(directory)) {
            final List<Vector> vectors = new ArrayList<>();
            for (Path file : files.filter(p -> p.toString().endsWith(".json")).sorted().toList()) {
                final JSONArray entries = new JSONObject(Files.readString(file)).getJSONArray("vectors");
                for (int i = 0; i < entries.length(); ++i) {
                    vectors.add(new Vector(file.getFileName().toString(), entries.getJSONObject(i)));
                }
            }
            return vectors;
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read the golden vectors in " + directory, e);
        }
    }

    @TestFactory
    Stream<DynamicTest> validVectorsRoundTripByteForByte() {
        return VECTORS.stream().filter(v -> !v.malformed())
                .map(v -> DynamicTest.dynamicTest(v.label(), () -> checkValid(v.data())));
    }

    @TestFactory
    Stream<DynamicTest> malformedVectorsFailWithTheSpecifiedError() {
        return VECTORS.stream().filter(Vector::malformed)
                .map(v -> DynamicTest.dynamicTest(v.label(), () -> checkMalformed(v.data())));
    }

    @Test
    void vectorsCoverEveryMessageAndManyMalformedInputs() {
        final Set<String> covered = VECTORS.stream().filter(v -> !v.malformed())
                .map(v -> v.data().getString("message")).collect(Collectors.toCollection(TreeSet::new));
        final Set<String> expected = Arrays.stream(MessageType.values()).map(MessageType::specName)
                .collect(Collectors.toCollection(TreeSet::new));
        expected.addAll(List.of("VideoSession", "AudioSession", "MediaConfig", "MediaFrame"));
        assertEquals(expected, covered);
        assertEquals(67, covered.size());
        assertTrue(VECTORS.stream().filter(Vector::malformed).count() >= 40);
    }

    private static void checkValid(JSONObject vector) {
        final String channel = vector.getString("channel");
        if (channel.equals("video") || channel.equals("audio")) {
            checkMedia(vector);
            return;
        }
        final MessageType type = typeNamed(vector.getString("message"));
        final JSONObject fields = vector.getJSONObject("fields");
        final byte[] expected = Json.fromHex(vector.getString("hex"));

        final byte[] encoded = Codec.encodeFrame(Json.fromJson(type.create(), fields));
        assertEquals(vector.getString("hex"), Json.toHex(encoded));

        final Message decoded = Codec.decodeFrame(context(channel), direction(vector), expected, 0,
                expected.length);
        assertEquals(type, decoded.type());
        final JSONObject roundTrip = Json.toJson(decoded);
        assertTrue(roundTrip.similar(fields), () -> "decoded " + roundTrip + " != " + fields);
    }

    private static void checkMedia(JSONObject vector) {
        final MediaChannel channel = mediaChannel(vector.getString("channel"));
        final String name = vector.getString("message");
        final JSONObject fields = vector.getJSONObject("fields");
        final byte[] expected = Json.fromHex(vector.getString("hex"));
        final int session = fields.getInt("session");

        final byte[] encoded = switch (name) {
            case "VideoSession" -> Media.encodeVideoSession(session, Json.fromJson(new VideoSessionInfo(), fields));
            case "AudioSession" -> Media.encodeAudioSession(session, Json.fromJson(new AudioSessionInfo(), fields));
            case "MediaConfig" -> Media.encodeConfig(channel, session, Json.fromHex(fields.getString("payload")));
            case "MediaFrame" -> Media.encodeFrame(channel, session,
                    (fields.getBoolean("keyFrame") ? Media.FLAG_KEY_FRAME : 0)
                            | (fields.getBoolean("discardable") ? Media.FLAG_DISCARDABLE : 0),
                    Long.parseLong(fields.getString("ptsUs")), fields.getLong("encodeDelayUs"),
                    Json.fromHex(fields.getString("payload")));
            default -> throw new AssertionError("unexpected media message " + name);
        };
        assertEquals(vector.getString("hex"), Json.toHex(encoded));

        final Media.Packet packet = Media.decodePacket(channel, expected, 0, expected.length);
        assertEquals(expected.length, Constants.MEDIA_HEADER_SIZE + packet.header.size);
        final JSONObject roundTrip;
        if (packet.session != null) {
            roundTrip = Json.toJson(packet.session);
        } else {
            roundTrip = new JSONObject();
            roundTrip.put("payload", Json.toHex(expected, packet.payloadOffset, packet.header.size));
            if (packet.header.kind == MediaKind.FRAME) {
                roundTrip.put("keyFrame", packet.header.keyFrame);
                roundTrip.put("discardable", packet.header.discardable);
                roundTrip.put("ptsUs", Long.toString(packet.header.ptsUs));
                roundTrip.put("encodeDelayUs", packet.header.encodeDelayUs);
            }
        }
        roundTrip.put("session", packet.header.session);
        assertTrue(roundTrip.similar(fields), () -> "decoded " + roundTrip + " != " + fields);
    }

    private static void checkMalformed(JSONObject vector) {
        final String channel = vector.getString("channel");
        final byte[] data = Json.fromHex(vector.getString("hex"));
        final DecodeException failure = assertThrows(DecodeException.class, () -> {
            if (channel.equals("video") || channel.equals("audio")) {
                Media.decodePacket(mediaChannel(channel), data, 0, data.length);
            } else {
                Codec.decodeFrame(context(channel), direction(vector), data, 0, data.length);
            }
        });
        assertEquals(vector.getString("error"), failure.error().specName());
    }

    private static MessageType typeNamed(String name) {
        return Arrays.stream(MessageType.values()).filter(t -> t.specName().equals(name)).findFirst()
                .orElseThrow(() -> new AssertionError("unknown message " + name));
    }

    private static FrameContext context(String channel) {
        return switch (channel) {
            case "handshake" -> FrameContext.HANDSHAKE;
            case "control" -> FrameContext.CONTROL;
            case "bulk" -> FrameContext.BULK;
            case "companion" -> FrameContext.COMPANION;
            default -> throw new AssertionError("unexpected channel " + channel);
        };
    }

    private static MediaChannel mediaChannel(String channel) {
        return channel.equals("video") ? MediaChannel.VIDEO : MediaChannel.AUDIO;
    }

    private static Direction direction(JSONObject vector) {
        return vector.getString("direction").equals("toServer") ? Direction.TO_SERVER : Direction.TO_CLIENT;
    }
}
