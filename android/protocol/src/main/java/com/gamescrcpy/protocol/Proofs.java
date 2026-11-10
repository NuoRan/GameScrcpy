package com.gamescrcpy.protocol;

import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.util.Arrays;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

public final class Proofs {

    private static final byte[] HELLO = "GSCP/1 hello".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] CHANNEL = "GSCP/1 channel".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] UDP_HELLO = "GSCP/1 udp hello".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] UDP_CONTROL = "GSCP/1 udp control".getBytes(StandardCharsets.US_ASCII);

    private Proofs() {
    }

    public static byte[] hello(byte[] token, byte[] nonce) {
        return hmac(token, HELLO, nonce, null);
    }

    public static byte[] channel(byte[] token, byte[] nonce, int channel) {
        return hmac(token, CHANNEL, nonce, (byte) channel);
    }

    public static byte[] udpHello(byte[] token, byte[] nonce) {
        return hmac(token, UDP_HELLO, nonce, null);
    }

    public static byte[] controlTag(byte[] token, byte[] datagram, int offset, int length) {
        final Mac mac = mac(token);
        mac.update(UDP_CONTROL);
        mac.update(datagram, offset, length);
        return Arrays.copyOf(mac.doFinal(), Datagram.CONTROL_TAG_SIZE);
    }

    public static boolean matches(byte[] expected, byte[] actual) {
        return MessageDigest.isEqual(expected, actual);
    }

    private static byte[] hmac(byte[] token, byte[] context, byte[] nonce, Byte suffix) {
        final Mac mac = mac(token);
        mac.update(context);
        mac.update(nonce);
        if (suffix != null) {
            mac.update(suffix);
        }
        return mac.doFinal();
    }

    private static Mac mac(byte[] token) {
        try {
            final Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(token, "HmacSHA256"));
            return mac;
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException("HmacSHA256 is unavailable", e);
        }
    }
}
