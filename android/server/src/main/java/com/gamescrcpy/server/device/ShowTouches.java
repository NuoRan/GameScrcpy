package com.gamescrcpy.server.device;

import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.wrappers.PlatformException;

public final class ShowTouches {

    public interface Store {
        String get() throws PlatformException;

        void put(String value) throws PlatformException;
    }

    private static final String ON = "1";
    private static final String OFF = "0";

    private final Store store;
    private boolean changed;
    private String original;

    public ShowTouches(Store store) {
        this.store = store;
    }

    public synchronized void set(boolean on) throws PlatformException {
        if (!on) {
            restore();
            return;
        }
        if (!changed) {
            original = store.get();
        }
        store.put(ON);
        changed = true;
        Log.i("touches are shown on the phone for a latency measurement");
    }

    public synchronized void restore() {
        if (!changed) {
            return;
        }
        changed = false;
        try {
            store.put(original == null ? OFF : original);
            Log.i("the phone's touch display is given back");
        } catch (PlatformException e) {
            Log.w("cannot give the phone its touch display back", e);
        }
    }
}
