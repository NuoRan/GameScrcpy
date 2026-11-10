package com.gamescrcpy.server.input;

import android.content.ClipData;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.wrappers.Clipboard;
import com.gamescrcpy.server.wrappers.InputInjector;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;

public final class TextInjector {

    private final InputInjector injector;
    private final Clipboard clipboard;
    private final Consumer<String> clipboardDenied;
    private final KeyCharacterMap keys = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
    private final BlockingQueue<Runnable> tasks = new LinkedBlockingQueue<>();
    private final AtomicInteger pending = new AtomicInteger();
    private boolean deniedReported;

    public TextInjector(InputInjector injector, Clipboard clipboard, Consumer<String> clipboardDenied) {
        this.injector = injector;
        this.clipboard = clipboard;
        this.clipboardDenied = clipboardDenied;
        final Thread worker = new Thread(this::work, "gs-text");
        worker.setDaemon(true);
        worker.start();
    }

    public void type(String text, int displayId) {
        if (text.isEmpty()) {
            return;
        }
        final KeyEvent[] events = keys.getEvents(text.toCharArray());
        if (events != null) {
            submit(() -> {
                for (KeyEvent event : events) {
                    injector.inject(event, displayId, InputInjector.MODE_ASYNC);
                }
            });
        } else {
            submit(() -> paste(text, displayId));
        }
    }

    public boolean busy() {
        return pending.get() > 0;
    }

    public void submit(Runnable task) {
        pending.incrementAndGet();
        tasks.add(task);
    }

    private void work() {
        while (true) {
            final Runnable task;
            try {
                task = tasks.take();
            } catch (InterruptedException e) {
                return;
            }
            try {
                task.run();
            } finally {
                pending.decrementAndGet();
            }
        }
    }

    private void paste(String text, int displayId) {
        if (clipboard == null) {
            reportDenied("text beyond the keyboard needs the clipboard, which is unavailable");
            return;
        }
        clipboard.holdTransientText(true);
        try {
            final ClipData saved;
            try {
                saved = clipboard.get();
                clipboard.setText(text);
            } catch (RuntimeException e) {
                reportDenied("the clipboard refused access: " + e.getMessage());
                return;
            }
            final long now = SystemClock.uptimeMillis();
            injector.inject(pasteKey(now, KeyEvent.ACTION_DOWN), displayId, InputInjector.MODE_ASYNC);
            injector.inject(pasteKey(now, KeyEvent.ACTION_UP), displayId, InputInjector.MODE_WAIT_FOR_FINISH);
            try {
                clipboard.restore(saved);
            } catch (RuntimeException e) {
                Log.w("cannot restore the clipboard", e);
            }
        } finally {
            clipboard.holdTransientText(false);
        }
    }

    private void reportDenied(String reason) {
        Log.w(reason);
        if (!deniedReported) {
            deniedReported = true;
            clipboardDenied.accept(reason);
        }
    }

    private static KeyEvent pasteKey(long downTime, int action) {
        return new KeyEvent(downTime, SystemClock.uptimeMillis(), action, KeyEvent.KEYCODE_PASTE, 0, 0,
                KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0, InputDevice.SOURCE_KEYBOARD);
    }
}
