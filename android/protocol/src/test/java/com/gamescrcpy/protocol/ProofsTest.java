package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.jupiter.api.Test;

class ProofsTest {

    private static byte[] sequence(int first, int count) {
        final byte[] bytes = new byte[count];
        for (int i = 0; i < count; ++i) {
            bytes[i] = (byte) (first + i);
        }
        return bytes;
    }

    private static JSONObject datagramVector() throws IOException {
        final Path directory = Path.of(System.getProperty("gs.protocol.vectorDir", "../../protocol/vectors"));
        final String text = Files.readString(directory.resolve("direct").resolve("datagrams.json"), StandardCharsets.UTF_8);
        return new JSONObject(text).getJSONArray("vectors").getJSONObject(0);
    }

    @Test
    void tunnelProofsMatchTheReferenceHmac() {
        final byte[] token = sequence(0x00, 16);
        final byte[] nonce = sequence(0x10, 16);
        assertEquals("f60ad39d2e36645efe58ed945d8d7ac1c3d6365f721d912925fca68d0c45ad44",
                Json.toHex(Proofs.hello(token, nonce)));
        assertEquals("dc93fe5f1677ff1b5ad6bac80b7daf9d5cec01c0884891bbf5f61c2b007aeb97",
                Json.toHex(Proofs.channel(token, nonce, Enums.ChannelId.VIDEO.wireValue())));
    }

    @Test
    void theUdpHelloProofMatchesTheGoldenVectorAndDiffersFromTheTunnelOne() throws IOException {
        final JSONObject vector = datagramVector();
        final byte[] token = Json.fromHex(vector.getString("token"));
        final byte[] nonce = Json.fromHex(vector.getString("nonce"));

        assertEquals(vector.getString("helloProof"), Json.toHex(Proofs.udpHello(token, nonce)));
        assertEquals(vector.getString("tcpHelloProof"), Json.toHex(Proofs.hello(token, nonce)));
    }

    @Test
    void controlTagsMatchTheGoldenVectorsWhereverTheBytesSit() throws IOException {
        final JSONObject vector = datagramVector();
        final byte[] token = Json.fromHex(vector.getString("token"));
        final JSONArray tags = vector.getJSONArray("controlTags");
        for (int i = 0; i < tags.length(); ++i) {
            final JSONObject entry = tags.getJSONObject(i);
            final byte[] body = Json.fromHex(entry.getString("body"));
            final byte[] padded = new byte[body.length + 5];
            System.arraycopy(body, 0, padded, 3, body.length);

            assertEquals(entry.getString("tag"), Json.toHex(Proofs.controlTag(token, body, 0, body.length)),
                    entry.getString("name"));
            assertEquals(entry.getString("tag"), Json.toHex(Proofs.controlTag(token, padded, 3, body.length)),
                    entry.getString("name"));
        }
    }

    @Test
    void matchingIsExact() {
        assertTrue(Proofs.matches(new byte[] {1, 2, 3}, new byte[] {1, 2, 3}));
        assertFalse(Proofs.matches(new byte[] {1, 2, 3}, new byte[] {1, 2, 4}));
        assertFalse(Proofs.matches(new byte[] {1, 2, 3}, new byte[] {1, 2}));
    }
}
