package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.ServerHello;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.jupiter.api.Test;

class DatagramTest {

    private static JSONObject vector() throws IOException {
        final Path directory = Path.of(System.getProperty("gs.protocol.vectorDir", "../../protocol/vectors"));
        final String text = Files.readString(directory.resolve("direct").resolve("datagrams.json"), StandardCharsets.UTF_8);
        return new JSONObject(text).getJSONArray("vectors").getJSONObject(0);
    }

    @Test
    void headersEncodeByteForByteAndDecodeToTheSameFields() throws IOException {
        final JSONArray headers = vector().getJSONArray("headers");
        for (int i = 0; i < headers.length(); ++i) {
            final JSONObject entry = headers.getJSONObject(i);
            final String name = entry.getString("name");
            final byte[] bytes = Json.fromHex(entry.getString("hex"));
            final Datagram.Header header = Datagram.decodeHeader(bytes, 0, bytes.length);
            assertEquals(entry.getInt("channel"), header.channel, name);
            assertEquals(entry.getLong("connection"), header.connection, name);
            assertEquals(entry.getInt("payloadSize"), bytes.length - Datagram.HEADER_SIZE, name);
            final byte[] encoded = new byte[Datagram.HEADER_SIZE];
            Datagram.writeHeader(encoded, 0, header.channel, header.connection);
            assertEquals(Json.toHex(bytes, 0, Datagram.HEADER_SIZE), Json.toHex(encoded), name);
        }
    }

    @Test
    void malformedHeadersReportTheErrorTheSpecificationOrders() throws IOException {
        final JSONArray malformed = vector().getJSONArray("malformed");
        for (int i = 0; i < malformed.length(); ++i) {
            final JSONObject entry = malformed.getJSONObject(i);
            final byte[] bytes = Json.fromHex(entry.getString("hex"));
            final DecodeException error = assertThrows(DecodeException.class,
                    () -> Datagram.decodeHeader(bytes, 0, bytes.length), entry.getString("name"));
            assertEquals(entry.getString("error"), error.error().specName(), entry.getString("name"));
        }
    }

    @Test
    void theHandshakeDatagramsCarryWholeHandshakeFrames() throws IOException {
        final JSONObject vector = vector();
        final JSONObject handshake = vector.getJSONObject("handshake");

        final byte[] knock = Json.fromHex(handshake.getString("knock"));
        final Datagram.Header knockHeader = Datagram.decodeHeader(knock, 0, knock.length);
        assertEquals(Datagram.HANDSHAKE_CHANNEL, knockHeader.channel);
        assertEquals(0, knockHeader.connection);
        assertEquals(Datagram.HEADER_SIZE, knock.length);

        final byte[] serverHello = Json.fromHex(handshake.getString("serverHello"));
        Datagram.decodeHeader(serverHello, 0, serverHello.length);
        final ServerHello hello = assertInstanceOf(ServerHello.class, Codec.decodeFrame(FrameContext.HANDSHAKE,
                Direction.TO_CLIENT, serverHello, Datagram.HEADER_SIZE, serverHello.length - Datagram.HEADER_SIZE));
        assertEquals(Constants.PROTOCOL_VERSION, hello.protocolVersion);
        assertEquals(vector.getString("nonce"), Json.toHex(hello.nonce));

        final byte[] clientHello = Json.fromHex(handshake.getString("clientHello"));
        Datagram.decodeHeader(clientHello, 0, clientHello.length);
        final ClientHello client = assertInstanceOf(ClientHello.class, Codec.decodeFrame(FrameContext.HANDSHAKE,
                Direction.TO_SERVER, clientHello, Datagram.HEADER_SIZE, clientHello.length - Datagram.HEADER_SIZE));
        assertEquals("2.0.0", client.clientVersion);
        assertEquals(vector.getString("helloProof"), Json.toHex(client.proof));
        assertNotEquals(vector.getString("helloProof"), vector.getString("tcpHelloProof"));

        final byte[] accepted = Json.fromHex(handshake.getString("accepted"));
        final Datagram.Header acceptance = Datagram.decodeHeader(accepted, 0, accepted.length);
        assertEquals(Datagram.HANDSHAKE_CHANNEL, acceptance.channel);
        assertEquals(handshake.getLong("connection"), acceptance.connection);
        assertNotEquals(0, acceptance.connection);
        assertEquals(Datagram.HEADER_SIZE, accepted.length);
    }

    @Test
    void theEncoderRefusesWhatTheDecoderWouldReject() {
        final byte[] out = new byte[Datagram.HEADER_SIZE];
        assertThrows(EncodeException.class, () -> Datagram.writeHeader(out, 0, 9, 0));
        assertThrows(EncodeException.class, () -> Datagram.writeHeader(out, 0, 0, -1));
        assertThrows(EncodeException.class, () -> Datagram.writeHeader(out, 0, 0, 0x1_0000_0000L));
        assertThrows(EncodeException.class, () -> Datagram.writeHeader(out, 1, 0, 0));
        Arrays.fill(out, (byte) 0);
        Datagram.writeHeader(out, 0, Datagram.HANDSHAKE_CHANNEL, 0xFFFF_FFFFL);
        assertEquals("47530f00ffffffff", Json.toHex(out));
    }
}
