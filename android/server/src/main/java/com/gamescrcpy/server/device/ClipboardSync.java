package com.gamescrcpy.server.device;

import android.view.KeyEvent;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ClipboardAck;
import com.gamescrcpy.protocol.Messages.ClipboardChanged;
import com.gamescrcpy.protocol.Messages.GetClipboard;
import com.gamescrcpy.protocol.Messages.SetClipboard;
import java.nio.charset.StandardCharsets;

public final class ClipboardSync {

    public interface Store {
        String text();

        void setText(String text);

        boolean holdsTransientText();

        void addListener(Runnable changed);
    }

    public interface Peer {
        void send(Message message);

        void pressKey(int keycode);

        void refused(String reason);
    }

    static final int MAX_TEXT_BYTES = 262_000;

    private final Store store;
    private final Peer peer;
    private final String unwatched;
    private boolean connected;
    private String shared;
    private String unreadable;
    private boolean told;

    public ClipboardSync(Store store, Peer peer) {
        this.store = store;
        this.peer = peer;
        String refusal = null;
        try {
            store.addListener(this::changed);
        } catch (RuntimeException e) {
            refusal = "the phone does not report clipboard changes, so copies on the phone stay there: "
                    + e.getMessage();
        }
        unwatched = refusal;
    }

    public void controlConnected() {
        final String current = read();
        final String refusal;
        synchronized (this) {
            connected = true;
            shared = current;
            told = false;
            refusal = unreadable != null ? unreadable : unwatched;
        }
        if (refusal != null) {
            tell(refusal);
        }
    }

    public synchronized void connectionLost() {
        connected = false;
    }

    public void set(SetClipboard message) {
        synchronized (this) {
            shared = message.text;
        }
        store.setText(message.text);
        final ClipboardAck ack = new ClipboardAck();
        ack.sequence = message.sequence;
        peer.send(ack);
        if (message.paste) {
            peer.pressKey(KeyEvent.KEYCODE_PASTE);
        }
    }

    public void get(GetClipboard message) {
        switch (message.copyKey) {
            case COPY -> peer.pressKey(KeyEvent.KEYCODE_COPY);
            case CUT -> peer.pressKey(KeyEvent.KEYCODE_CUT);
            case NONE -> {
                final String text = read();
                if (text != null) {
                    synchronized (this) {
                        shared = text;
                    }
                }
                send(text == null ? "" : text);
            }
        }
    }

    private void changed() {
        if (store.holdsTransientText()) {
            return;
        }
        final String text = read();
        if (text == null) {
            return;
        }
        synchronized (this) {
            if (!connected || text.equals(shared)) {
                return;
            }
            shared = text;
        }
        send(text);
    }

    private String read() {
        try {
            final String text = store.text();
            synchronized (this) {
                unreadable = null;
            }
            return text;
        } catch (RuntimeException e) {
            final String reason = "the phone does not let its clipboard be read: " + e.getMessage();
            synchronized (this) {
                unreadable = reason;
            }
            tell(reason);
            return null;
        }
    }

    private void tell(String reason) {
        synchronized (this) {
            if (!connected || told) {
                return;
            }
            told = true;
        }
        peer.refused(reason);
    }

    private void send(String text) {
        final ClipboardChanged changed = new ClipboardChanged();
        changed.text = limit(text);
        peer.send(changed);
    }

    static String limit(String text) {
        final byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        if (bytes.length <= MAX_TEXT_BYTES) {
            return text;
        }
        int end = MAX_TEXT_BYTES;
        while (end > 0 && (bytes[end] & 0xC0) == 0x80) {
            --end;
        }
        return new String(bytes, 0, end, StandardCharsets.UTF_8);
    }
}
