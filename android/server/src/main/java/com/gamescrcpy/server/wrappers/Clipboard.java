package com.gamescrcpy.server.wrappers;

import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.PersistableBundle;
import com.gamescrcpy.server.device.ClipboardSync;
import java.lang.reflect.Constructor;

public final class Clipboard implements ClipboardSync.Store {

    private static final String LABEL = "GameScrcpy";

    private final ClipboardManager manager;
    private volatile boolean transientText;

    private Clipboard(ClipboardManager manager) {
        this.manager = manager;
    }

    public static Clipboard create() throws PlatformException {
        final Context context = ProcessEnvironment.context();
        if (context == null) {
            throw new PlatformException("no shell context for the clipboard");
        }
        try {
            final Constructor<ClipboardManager> constructor = ClipboardManager.class.getDeclaredConstructor(
                    Context.class, Handler.class);
            constructor.setAccessible(true);
            return new Clipboard(constructor.newInstance(context, new Handler(Looper.getMainLooper())));
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("the clipboard is unavailable", e);
        }
    }

    public ClipData get() {
        return manager.getPrimaryClip();
    }

    @Override
    public String text() {
        final ClipData clip = manager.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) {
            return null;
        }
        final CharSequence text = clip.getItemAt(0).getText();
        return text == null ? null : text.toString();
    }

    @Override
    public void setText(String text) {
        manager.setPrimaryClip(remote(ClipData.newPlainText(LABEL, text)));
    }

    public void restore(ClipData saved) {
        if (saved == null) {
            manager.clearPrimaryClip();
        } else {
            manager.setPrimaryClip(remote(saved));
        }
    }

    public void holdTransientText(boolean holding) {
        transientText = holding;
    }

    @Override
    public boolean holdsTransientText() {
        return transientText;
    }

    @Override
    public void addListener(Runnable changed) {
        manager.addPrimaryClipChangedListener(changed::run);
    }

    private static ClipData remote(ClipData clip) {
        if (Build.VERSION.SDK_INT >= 34) {
            final PersistableBundle extras = new PersistableBundle();
            extras.putBoolean(ClipDescription.EXTRA_IS_REMOTE_DEVICE, true);
            clip.getDescription().setExtras(extras);
        }
        return clip;
    }
}
