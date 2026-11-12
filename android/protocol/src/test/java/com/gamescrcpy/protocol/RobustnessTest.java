package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Enums.TouchAction;
import com.gamescrcpy.protocol.Messages.ClientHello;
import com.gamescrcpy.protocol.Messages.DeviceStatus;
import com.gamescrcpy.protocol.Messages.Display;
import com.gamescrcpy.protocol.Messages.GetClipboard;
import com.gamescrcpy.protocol.Messages.SubscribeStatus;
import com.gamescrcpy.protocol.Messages.Text;
import com.gamescrcpy.protocol.Messages.TouchFrame;
import com.gamescrcpy.protocol.Messages.TouchPoint;
import com.gamescrcpy.protocol.Messages.Welcome;
import java.util.Arrays;
import java.util.List;
import java.util.SplittableRandom;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.function.Executable;

class RobustnessTest {

    private static void decodeEverywhere(byte[] data) {
        for (FrameContext context : FrameContext.values()) {
            for (Direction direction : Direction.values()) {
                try {
                    Codec.decodeFrame(context, direction, data, 0, data.length);
                } catch (DecodeException expected) {
                }
            }
        }
        for (MediaChannel channel : MediaChannel.values()) {
            try {
                Media.decodePacket(channel, data, 0, data.length);
            } catch (DecodeException expected) {
            }
        }
    }

    @Test
    void decodersNeverFailOutsideDecodeExceptionOnMutatedOrRandomInput() {
        final SplittableRandom random = new SplittableRandom(20261001L);
        final List<byte[]> seeds = List.of(
                Json.fromHex("01010000000b000101000080008000ffff"),
                Json.fromHex("8001000000164753435000010001020304050607080900010203040506"),
                Json.fromHex("0103000000070005616263"),
                Json.fromHex("02000001000000000000000000000000000000000000000e01043809600000000004380960"));

        int decodes = 0;
        for (byte[] seed : seeds) {
            for (int round = 0; round < 500; ++round) {
                byte[] mutated = seed.clone();
                switch (random.nextInt(3)) {
                    case 0 -> {
                        if (mutated.length > 0) {
                            mutated[random.nextInt(mutated.length)] ^= (byte) (1 << random.nextInt(8));
                        }
                    }
                    case 1 -> mutated = Arrays.copyOf(mutated, random.nextInt(mutated.length + 1));
                    default -> {
                        final int at = random.nextInt(mutated.length + 1);
                        final byte[] grown = new byte[mutated.length + 1];
                        System.arraycopy(mutated, 0, grown, 0, at);
                        grown[at] = (byte) random.nextInt(256);
                        System.arraycopy(mutated, at, grown, at + 1, mutated.length - at);
                        mutated = grown;
                    }
                }
                decodeEverywhere(mutated);
                ++decodes;
            }
        }
        for (int round = 0; round < 2000; ++round) {
            final byte[] noise = new byte[random.nextInt(64)];
            random.nextBytes(noise);
            decodeEverywhere(noise);
            ++decodes;
        }
        assertEquals(4000, decodes);
    }

    private static TouchFrame touchFrame(int videoSession, TouchPoint... points) {
        final TouchFrame frame = new TouchFrame();
        frame.videoSession = videoSession;
        for (TouchPoint point : points) {
            frame.points.add(point);
        }
        return frame;
    }

    @Test
    void touchFramesEncodeIntoACallerProvidedBuffer() {
        final TouchFrame frame = touchFrame(7, new TouchPoint(TouchAction.MOVE, 3, 100, 200, 0xFFFF));
        final byte[] buffer = new byte[Constants.FRAME_HEADER_SIZE + 11];
        assertEquals(buffer.length, Codec.encodeFrameInto(frame, buffer, 0));
        assertEquals("01010000000b0007010203006400c8ffff", Json.toHex(buffer));

        final byte[] tooSmall = new byte[Constants.FRAME_HEADER_SIZE + 10];
        assertEncodeError(EncodeError.BUFFER_TOO_SMALL, () -> Codec.encodeFrameInto(frame, tooSmall, 0));
    }

    @Test
    void decodingIntoALongLivedMessageReusesItsPoints() {
        final byte[] wire = Codec.encodeFrame(touchFrame(1, new TouchPoint(TouchAction.DOWN, 0, 1, 2, 3)));
        final TouchFrame reused = new TouchFrame();
        Codec.decodePayload(reused, wire, Constants.FRAME_HEADER_SIZE, wire.length - Constants.FRAME_HEADER_SIZE);
        final TouchPoint first = reused.points.get(0);
        Codec.decodePayload(reused, wire, Constants.FRAME_HEADER_SIZE, wire.length - Constants.FRAME_HEADER_SIZE);
        assertSame(first, reused.points.get(0));
        assertEquals(1, reused.points.size());
    }

    @Test
    void theEncoderRefusesFramesTheDecoderWouldReject() {
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE,
                () -> Codec.encodeFrame(touchFrame(1, new TouchPoint(TouchAction.DOWN, 32, 0, 0, 0))));
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(touchFrame(1)));
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(touchFrame(1,
                new TouchPoint(TouchAction.DOWN, 4, 0, 0, 0), new TouchPoint(TouchAction.MOVE, 4, 1, 1, 1))));

        final ClientHello hello = new ClientHello();
        hello.clientVersion = "v".repeat(33);
        assertEncodeError(EncodeError.FIELD_TOO_LONG, () -> Codec.encodeFrame(hello));

        final Text text = new Text();
        text.text = "unpaired \uD800 surrogate";
        assertEncodeError(EncodeError.MALFORMED_UTF8, () -> Codec.encodeFrame(text));

        final Welcome reserved = new Welcome();
        reserved.capabilities = 1L << 13;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(reserved));

        final DeviceStatus overfull = new DeviceStatus();
        overfull.batteryPercent = 101;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(overfull));
        final DeviceStatus negative = new DeviceStatus();
        negative.batteryPercent = -1;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(negative));
        final DeviceStatus overloaded = new DeviceStatus();
        overloaded.cpuPermille = 1001;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(overloaded));

        final SubscribeStatus tooOften = new SubscribeStatus();
        tooOften.on = true;
        tooOften.periodMs = SubscribeStatus.MIN_PERIOD_MS - 1;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(tooOften));
        final SubscribeStatus tooRarely = new SubscribeStatus();
        tooRarely.on = true;
        tooRarely.periodMs = SubscribeStatus.MAX_PERIOD_MS + 1;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(tooRarely));

        final Welcome tooManyDisplays = new Welcome();
        for (int i = 0; i < 17; ++i) {
            tooManyDisplays.displays.add(new Display());
        }
        assertEncodeError(EncodeError.FIELD_TOO_LONG, () -> Codec.encodeFrame(tooManyDisplays));

        final GetClipboard clipboard = new GetClipboard();
        clipboard.copyKey = null;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(clipboard));

        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE,
                () -> Media.encodeFrame(MediaChannel.VIDEO, 0, Media.FLAG_KEY_FRAME, 0, 0, new byte[0]));
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE,
                () -> Media.encodeFrame(MediaChannel.VIDEO, 1, 0, 0, -1, new byte[0]));
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE,
                () -> Media.encodeFrame(MediaChannel.AUDIO, 1, 0, 0, 0x1_0000_0000L, new byte[0]));
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Media.writeHeader(
                new byte[Constants.MEDIA_HEADER_SIZE], 0, MediaChannel.VIDEO, MediaKind.CONFIG,
                Media.FLAG_DISCARDABLE, 1, 0, 0, 0));
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Media.writeHeader(
                new byte[Constants.MEDIA_HEADER_SIZE], 0, MediaChannel.VIDEO, MediaKind.CONFIG, 0, 1, 0, 7, 0));
    }

    @Test
    void frameHeadersCanBeValidatedBeforeThePayloadArrives() {
        final byte[] frame = Json.fromHex("01010000000b000101000080008000ffff");
        final Codec.FrameHeader header = Codec.decodeFrameHeader(FrameContext.CONTROL, Direction.TO_SERVER,
                frame, 0, Constants.FRAME_HEADER_SIZE);
        assertEquals(MessageType.TOUCH_FRAME, header.type);
        assertEquals(11, header.length);
    }

    @Test
    void messageTypesAreFoundByNumber() {
        for (MessageType type : MessageType.values()) {
            assertSame(type, MessageType.fromCode(type.code()));
        }
        assertNull(MessageType.fromCode(0x01FF));
    }

    @Test
    void theCompanionContextAdmitsItsOwnMessagesAndTheKeepAliveMessagesOnly() {
        assertTrue(FrameContext.COMPANION.allows(MessageType.COMPANION_HELLO));
        assertTrue(FrameContext.COMPANION.allows(MessageType.OVERLAY_CHANGED));
        assertTrue(FrameContext.COMPANION.allows(MessageType.PING));
        assertTrue(FrameContext.COMPANION.allows(MessageType.PONG));
        assertTrue(FrameContext.COMPANION.allows(MessageType.GOODBYE));
        assertTrue(FrameContext.COMPANION.allows(MessageType.SERVER_GOODBYE));
        assertFalse(FrameContext.COMPANION.allows(MessageType.RECEIVER_REPORT));
        assertFalse(FrameContext.COMPANION.allows(MessageType.NOTICE));
        assertFalse(FrameContext.COMPANION.allows(MessageType.TOUCH_FRAME));
        assertFalse(FrameContext.CONTROL.allows(MessageType.CURSOR_STATE));
        assertTrue(FrameContext.CONTROL.allows(MessageType.PING));
        assertEquals(16 * 1024, FrameContext.COMPANION.payloadLimit());

        final Messages.CompanionHello tooLong = new Messages.CompanionHello();
        tooLong.token = new byte[Constants.COMPANION_TOKEN_SIZE + 1];
        assertEncodeError(EncodeError.FIELD_TOO_LONG, () -> Codec.encodeFrame(tooLong));

        final Messages.CursorState reserved = new Messages.CursorState();
        reserved.flags = 4;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(reserved));
    }

    @Test
    void theCompanionsScreenshotMessagesStayInTheirContextAndWithinTheirLimits() {
        for (MessageType type : List.of(MessageType.SCREENSHOT_REQUEST, MessageType.SCREENSHOT_CHUNK,
                MessageType.SCREENSHOT_FAILED)) {
            assertTrue(FrameContext.COMPANION.allows(type));
            assertFalse(FrameContext.CONTROL.allows(type));
            assertFalse(FrameContext.BULK.allows(type));
        }
        assertTrue((Constants.MAX_SCREENSHOT_CHUNKS - 1) * Constants.MAX_SCREENSHOT_CHUNK < Constants.MAX_SCREENSHOT_BYTES);
        assertTrue(Constants.MAX_SCREENSHOT_BYTES <= Constants.MAX_SCREENSHOT_CHUNKS * Constants.MAX_SCREENSHOT_CHUNK);

        final Messages.ScreenshotRequest request = new Messages.ScreenshotRequest();
        assertEquals(0, request.maxSide);
        assertEquals(Constants.DEFAULT_SCREENSHOT_QUALITY, request.quality);

        final Messages.ScreenshotChunk last = new Messages.ScreenshotChunk();
        last.requestId = 9;
        last.index = Constants.MAX_SCREENSHOT_CHUNKS - 1;
        last.total = Constants.MAX_SCREENSHOT_CHUNKS;
        last.data = new byte[Constants.MAX_SCREENSHOT_CHUNK];
        final byte[] wire = Codec.encodeFrame(last);
        assertTrue(wire.length <= Constants.FRAME_HEADER_SIZE + FrameContext.COMPANION.payloadLimit());
        final Messages.ScreenshotChunk decoded = (Messages.ScreenshotChunk) Codec.decodeFrame(FrameContext.COMPANION,
                Direction.TO_CLIENT, wire, 0, wire.length);
        assertEquals(Constants.MAX_SCREENSHOT_CHUNKS - 1, decoded.index);
        assertEquals(Constants.MAX_SCREENSHOT_CHUNK, decoded.data.length);

        final Messages.ScreenshotChunk beyond = new Messages.ScreenshotChunk();
        beyond.index = 3;
        beyond.total = 3;
        beyond.data = new byte[1];
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(beyond));
        final Messages.ScreenshotChunk none = new Messages.ScreenshotChunk();
        none.total = 0;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(none));
        final Messages.ScreenshotChunk many = new Messages.ScreenshotChunk();
        many.total = Constants.MAX_SCREENSHOT_CHUNKS + 1;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(many));
        final Messages.ScreenshotChunk oversize = new Messages.ScreenshotChunk();
        oversize.data = new byte[Constants.MAX_SCREENSHOT_CHUNK + 1];
        assertEncodeError(EncodeError.FIELD_TOO_LONG, () -> Codec.encodeFrame(oversize));

        final Messages.ScreenshotRequest dull = new Messages.ScreenshotRequest();
        dull.quality = 0;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(dull));
        final Messages.ScreenshotRequest sharp = new Messages.ScreenshotRequest();
        sharp.quality = 101;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(sharp));

        final Messages.ScreenshotFailed wordy = new Messages.ScreenshotFailed();
        wordy.message = "x".repeat(1025);
        assertEncodeError(EncodeError.FIELD_TOO_LONG, () -> Codec.encodeFrame(wordy));
        final Messages.ScreenshotFailed blank = new Messages.ScreenshotFailed();
        blank.reason = null;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(blank));
    }

    @Test
    @DisplayName("ADR-0112: the companion's settings snapshot and setting change stay in their context and ranges")
    void theCompanionsSettingsMessagesStayInTheirContextAndWithinTheirRanges() {
        for (MessageType type : List.of(MessageType.COMPANION_SETTINGS, MessageType.COMPANION_SETTING_CHANGE)) {
            assertTrue(FrameContext.COMPANION.allows(type));
            assertFalse(FrameContext.CONTROL.allows(type));
            assertFalse(FrameContext.HANDSHAKE.allows(type));
        }
        assertEquals(Direction.TO_SERVER, MessageType.COMPANION_SETTINGS.direction());
        assertEquals(Direction.TO_CLIENT, MessageType.COMPANION_SETTING_CHANGE.direction());
        assertEquals(2, Constants.COMPANION_VERSION);
        assertEquals(2, new Messages.CompanionHello().version);

        final Messages.CompanionSettings longest = new Messages.CompanionSettings();
        longest.name = "n".repeat(Constants.MAX_COMPUTER_NAME);
        longest.accent = Constants.ACCENT_MASK;
        longest.applied = 0xFFFF_FFFFL;
        final byte[] wire = Codec.encodeFrame(longest);
        final Messages.CompanionSettings decoded = (Messages.CompanionSettings) Codec.decodeFrame(
                FrameContext.COMPANION, Direction.TO_SERVER, wire, 0, wire.length);
        assertEquals(longest.name, decoded.name);
        assertEquals(Constants.ACCENT_MASK, decoded.accent);
        assertEquals(Constants.DEFAULT_PHONE_HINT_OPACITY, decoded.hintOpacity);
        assertTrue(decoded.ime && decoded.autoConnect && !decoded.cursor && !decoded.hints);

        final Messages.CompanionSettings named = new Messages.CompanionSettings();
        named.name = "n".repeat(Constants.MAX_COMPUTER_NAME + 1);
        assertEncodeError(EncodeError.FIELD_TOO_LONG, () -> Codec.encodeFrame(named));
        final Messages.CompanionSettings tinted = new Messages.CompanionSettings();
        tinted.accent = 0x01FF_6B3DL;
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(tinted));
        for (int opacity : new int[] {Constants.MIN_PHONE_HINT_OPACITY - 1, Constants.MAX_PHONE_HINT_OPACITY + 1}) {
            final Messages.CompanionSettings faint = new Messages.CompanionSettings();
            faint.hintOpacity = opacity;
            assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(faint));
        }
        final Messages.CompanionSettings shortId = new Messages.CompanionSettings();
        shortId.computer = new byte[Constants.COMPANION_COMPUTER_ID_SIZE - 1];
        assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(shortId));

        assertTrue(Codec.encodeFrame(change(1, Enums.CompanionItem.CURSOR, 1)).length > 0);
        assertTrue(Codec.encodeFrame(change(1, Enums.CompanionItem.HINT_OPACITY, 10)).length > 0);
        for (Messages.CompanionSettingChange wrong : List.of(change(0, Enums.CompanionItem.CURSOR, 1),
                change(1, Enums.CompanionItem.IME, 2), change(1, Enums.CompanionItem.AUTO_CONNECT, 60),
                change(1, Enums.CompanionItem.HINT_OPACITY, 9), change(1, Enums.CompanionItem.HINT_OPACITY, 101),
                change(1, null, 1))) {
            assertEncodeError(EncodeError.VALUE_OUT_OF_RANGE, () -> Codec.encodeFrame(wrong));
        }
    }

    private static Messages.CompanionSettingChange change(long number, Enums.CompanionItem item, int value) {
        final Messages.CompanionSettingChange change = new Messages.CompanionSettingChange();
        change.change = number;
        change.item = item;
        change.value = value;
        return change;
    }

    @Test
    void aPhoneStatusStartsOutUnknownInEveryItemAndTravelsOnTheControlChannel() {
        final byte[] unknown = Codec.encodeFrame(new DeviceStatus());
        assertEquals("830500000006ff008000ffff", Json.toHex(unknown));
        final DeviceStatus decoded = (DeviceStatus) Codec.decodeFrame(FrameContext.CONTROL, Direction.TO_CLIENT,
                unknown, 0, unknown.length);
        assertEquals(DeviceStatus.UNKNOWN_BATTERY, decoded.batteryPercent);
        assertFalse(decoded.charging);
        assertEquals(DeviceStatus.UNKNOWN_TEMPERATURE, decoded.temperatureDeciC);
        assertEquals(DeviceStatus.UNKNOWN_CPU, decoded.cpuPermille);

        assertTrue(FrameContext.CONTROL.allows(MessageType.SUBSCRIBE_STATUS));
        assertTrue(FrameContext.CONTROL.allows(MessageType.DEVICE_STATUS));
        assertFalse(FrameContext.COMPANION.allows(MessageType.DEVICE_STATUS));
        assertEquals(Constants.CAPABILITY_DEVICE_STATUS, Constants.CAPABILITY_MASK & Constants.CAPABILITY_DEVICE_STATUS);
    }

    @Test
    void utf8ValidationIsStrict() {
        assertTrue(validHex("706c61696e"));
        assertTrue(validHex("e4bda0e5a5bd"));
        assertTrue(validHex("f09f8eae"));
        assertFalse(validHex("c080"));
        assertFalse(validHex("e08080"));
        assertFalse(validHex("eda080"));
        assertFalse(validHex("f4908080"));
        assertFalse(validHex("e4bd"));
        assertFalse(validHex("80"));
    }

    private static boolean validHex(String hex) {
        final byte[] bytes = Json.fromHex(hex);
        return Utf8.isValid(bytes, 0, bytes.length);
    }

    private static void assertEncodeError(EncodeError expected, Executable action) {
        assertEquals(expected, assertThrows(EncodeException.class, action).error());
    }
}
