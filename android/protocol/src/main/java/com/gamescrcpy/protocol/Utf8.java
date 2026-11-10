package com.gamescrcpy.protocol;

import java.nio.charset.StandardCharsets;

public final class Utf8 {

    private Utf8() {
    }

    public static boolean isValid(byte[] bytes, int offset, int length) {
        int i = offset;
        final int end = offset + length;
        while (i < end) {
            final int lead = bytes[i] & 0xFF;
            if (lead < 0x80) {
                ++i;
                continue;
            }
            final int count;
            int min;
            int max = 0xBF;
            if (lead >= 0xC2 && lead <= 0xDF) {
                count = 1;
                min = 0x80;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                count = 2;
                min = lead == 0xE0 ? 0xA0 : 0x80;
                max = lead == 0xED ? 0x9F : 0xBF;
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                count = 3;
                min = lead == 0xF0 ? 0x90 : 0x80;
                max = lead == 0xF4 ? 0x8F : 0xBF;
            } else {
                return false;
            }
            if (end - i <= count) {
                return false;
            }
            for (int k = 1; k <= count; ++k) {
                final int next = bytes[i + k] & 0xFF;
                if (next < min || next > max) {
                    return false;
                }
                min = 0x80;
                max = 0xBF;
            }
            i += count + 1;
        }
        return true;
    }

    public static byte[] encode(String text) {
        final int length = text.length();
        for (int i = 0; i < length; ++i) {
            final char c = text.charAt(i);
            if (Character.isHighSurrogate(c)) {
                if (i + 1 >= length || !Character.isLowSurrogate(text.charAt(i + 1))) {
                    throw new EncodeException(EncodeError.MALFORMED_UTF8);
                }
                ++i;
            } else if (Character.isLowSurrogate(c)) {
                throw new EncodeException(EncodeError.MALFORMED_UTF8);
            }
        }
        return text.getBytes(StandardCharsets.UTF_8);
    }
}
