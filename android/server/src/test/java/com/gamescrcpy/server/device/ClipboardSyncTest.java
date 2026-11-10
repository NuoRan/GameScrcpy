package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import android.view.KeyEvent;
import com.gamescrcpy.protocol.Enums.CopyKey;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ClipboardAck;
import com.gamescrcpy.protocol.Messages.ClipboardChanged;
import com.gamescrcpy.protocol.Messages.GetClipboard;
import com.gamescrcpy.protocol.Messages.SetClipboard;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class ClipboardSyncTest {

    private static final class FakeStore implements ClipboardSync.Store {
        String text;
        boolean transientText;
        boolean refuseReading;
        boolean refuseWatching;
        Runnable listener;

        @Override public String text() {
            if (refuseReading) {
                throw new SecurityException("clipboard access denied");
            }
            return text;
        }
        @Override public void setText(String newText) { text = newText; }
        @Override public boolean holdsTransientText() { return transientText; }
        @Override public void addListener(Runnable changed) {
            if (refuseWatching) {
                throw new SecurityException("no clipboard listener");
            }
            listener = changed;
        }

        void notifyChanged() { listener.run(); }
    }

    private static final class FakePeer implements ClipboardSync.Peer {
        final List<Message> sent = new ArrayList<>();
        final List<Integer> keys = new ArrayList<>();
        final List<String> refusals = new ArrayList<>();

        @Override public void send(Message message) { sent.add(message); }
        @Override public void pressKey(int keycode) { keys.add(keycode); }
        @Override public void refused(String reason) { refusals.add(reason); }

        List<String> clips() {
            final List<String> texts = new ArrayList<>();
            for (Message message : sent) {
                if (message instanceof ClipboardChanged changed) {
                    texts.add(changed.text);
                }
            }
            return texts;
        }
    }

    private final FakeStore store = new FakeStore();
    private final FakePeer peer = new FakePeer();
    private final ClipboardSync sync = new ClipboardSync(store, peer);

    private static SetClipboard set(long sequence, String text, boolean paste) {
        final SetClipboard message = new SetClipboard();
        message.sequence = sequence;
        message.text = text;
        message.paste = paste;
        return message;
    }

    private static GetClipboard get(CopyKey key) {
        final GetClipboard message = new GetClipboard();
        message.copyKey = key;
        return message;
    }

    @Test
    void aCopyOnTheDeviceIsSentOnce() {
        sync.controlConnected();
        store.text = "from the phone";
        store.notifyChanged();
        store.notifyChanged();
        assertEquals(List.of("from the phone"), peer.clips());
    }

    @Test
    void theClientsTextIsAcknowledgedAndNotEchoed() {
        sync.controlConnected();
        sync.set(set(7, "from the computer", false));
        store.notifyChanged();
        assertEquals("from the computer", store.text);
        assertEquals(1, peer.sent.size());
        assertTrue(peer.sent.get(0) instanceof ClipboardAck ack && ack.sequence == 7);
        assertTrue(peer.keys.isEmpty());
    }

    @Test
    void whatTheClipboardHeldBeforeConnectingStaysOnTheDevice() {
        store.text = "private";
        store.notifyChanged();
        sync.controlConnected();
        store.notifyChanged();
        assertTrue(peer.sent.isEmpty());
    }

    @Test
    void textInjectionsOwnWritesAreNotReported() {
        store.text = "kept";
        sync.controlConnected();
        store.transientText = true;
        store.text = "你好";
        store.notifyChanged();
        store.text = "kept";
        store.transientText = false;
        store.notifyChanged();
        assertTrue(peer.sent.isEmpty());
    }

    @Test
    void nothingIsSentWithoutAConnection() {
        sync.controlConnected();
        sync.connectionLost();
        store.text = "copied while disconnected";
        store.notifyChanged();
        assertTrue(peer.sent.isEmpty());
    }

    @Test
    void pastingAndCopyingPressTheKeysAndAPlainRequestSendsTheClip() {
        sync.controlConnected();
        sync.set(set(1, "pasted", true));
        sync.get(get(CopyKey.COPY));
        sync.get(get(CopyKey.CUT));
        assertEquals(List.of(KeyEvent.KEYCODE_PASTE, KeyEvent.KEYCODE_COPY, KeyEvent.KEYCODE_CUT), peer.keys);
        sync.get(get(CopyKey.NONE));
        assertEquals(List.of("pasted"), peer.clips());
    }

    @Test
    void aPlainRequestOnAnEmptyClipboardSendsEmptyTextAndKeepsTheSharedText() {
        sync.controlConnected();
        sync.set(set(3, "from the computer", false));
        store.text = null;
        sync.get(get(CopyKey.NONE));
        store.text = "from the computer";
        store.notifyChanged();
        assertEquals(List.of(""), peer.clips());
    }

    @Test
    void longTextIsCutBetweenCharacters() {
        final String text = "好".repeat(100_000);
        final String cut = ClipboardSync.limit(text);
        final byte[] bytes = cut.getBytes(StandardCharsets.UTF_8);
        assertTrue(bytes.length <= ClipboardSync.MAX_TEXT_BYTES);
        assertEquals(ClipboardSync.MAX_TEXT_BYTES / 3, cut.length());
        assertEquals("好".repeat(cut.length()), cut);
        assertEquals("short", ClipboardSync.limit("short"));
    }

    @Test
    void aPhoneThatRefusesToShowItsClipboardKeepsTheSessionAndIsToldOncePerConnection() {
        store.refuseReading = true;
        assertDoesNotThrow(sync::controlConnected);
        assertEquals(1, peer.refusals.size());
        assertTrue(peer.refusals.get(0).contains("clipboard access denied"));

        assertDoesNotThrow(store::notifyChanged);
        assertDoesNotThrow(() -> sync.get(get(CopyKey.NONE)));
        assertEquals(List.of(""), peer.clips());
        assertEquals(1, peer.refusals.size());

        sync.set(set(4, "from the computer", false));
        assertEquals("from the computer", store.text);

        sync.connectionLost();
        sync.controlConnected();
        assertEquals(2, peer.refusals.size());
    }

    @Test
    void aClipboardThatCanBeReadAgainIsNoLongerReported() {
        store.refuseReading = true;
        sync.controlConnected();
        sync.connectionLost();
        store.refuseReading = false;
        store.text = "readable";
        sync.controlConnected();
        assertEquals(1, peer.refusals.size());
        store.text = "copied";
        store.notifyChanged();
        assertEquals(List.of("copied"), peer.clips());
    }

    @Test
    void aPhoneThatWillNotReportClipboardChangesStillTakesTheComputersText() {
        final FakeStore unwatched = new FakeStore();
        unwatched.refuseWatching = true;
        final FakePeer told = new FakePeer();
        final ClipboardSync quiet = assertDoesNotThrow(() -> new ClipboardSync(unwatched, told));
        assertTrue(told.refusals.isEmpty());

        quiet.controlConnected();
        assertEquals(1, told.refusals.size());
        assertTrue(told.refusals.get(0).contains("no clipboard listener"));

        quiet.set(set(9, "from the computer", true));
        assertEquals("from the computer", unwatched.text);
        assertEquals(List.of(KeyEvent.KEYCODE_PASTE), told.keys);
        quiet.get(get(CopyKey.NONE));
        assertEquals(List.of("from the computer"), told.clips());
        assertEquals(1, told.refusals.size());
    }
}
