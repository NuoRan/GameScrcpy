package com.gamescrcpy.server.audio;

import android.os.Build;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.transport.MediaEndpoint;
import java.io.IOException;

public final class AudioController {

    public interface Notifier {
        void notice(ErrorCode code, String message);
    }

    private static final int DEFAULT_BITRATE = 128_000;
    private static final int MAX_BITRATE = 510_000;

    private final Notifier notifier;
    private boolean enabled;
    private int bitrate = DEFAULT_BITRATE;
    private MediaEndpoint channel;
    private AudioStreamer streamer;
    private int session;

    public AudioController(Notifier notifier) {
        this.notifier = notifier;
    }

    public synchronized void configure(boolean enabled, long bitrate) {
        this.enabled = enabled;
        this.bitrate = bitrate > 0 ? (int) Math.min(bitrate, MAX_BITRATE) : DEFAULT_BITRATE;
        restart();
    }

    public synchronized void channelOpened(MediaEndpoint channel) {
        this.channel = channel;
        restart();
    }

    public synchronized void connectionLost() {
        stopStreaming();
        channel = null;
    }

    private void restart() {
        stopStreaming();
        if (!enabled || channel == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return;
        }
        session = session % 65535 + 1;
        final MediaEndpoint target = channel;
        streamer = new AudioStreamer(new AudioPackets(target::writePacket), session, bitrate, notifier::notice);
        try {
            streamer.start();
        } catch (AudioException e) {
            streamer = null;
            final Throwable cause = e.getCause();
            notifier.notice(e.code, cause == null ? e.getMessage() : e.getMessage() + ": " + cause);
        } catch (IOException e) {
            streamer = null;
            Log.d("audio channel closed: " + e.getMessage());
        }
    }

    private void stopStreaming() {
        if (streamer != null) {
            streamer.stop();
            streamer = null;
        }
    }
}
