package com.gamescrcpy.server.input;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.TouchAction;
import com.gamescrcpy.protocol.Messages.TouchFrame;
import com.gamescrcpy.protocol.Messages.TouchPoint;

public final class TouchPlanner {

    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MOVE = 2;
    public static final int ACTION_CANCEL = 3;
    public static final int ACTION_POINTER_DOWN = 5;
    public static final int ACTION_POINTER_UP = 6;
    public static final int ACTION_POINTER_INDEX_SHIFT = 8;

    private static final int POINTERS = Constants.MAX_POINTER_ID + 1;
    private static final float FULL_PRESSURE = 0xFFFF;

    public static final class Event {
        public int action;
        public boolean canceled;
        public boolean gestureStart;
        public int displayId;
        public int pointerCount;
        public final int[] ids = new int[POINTERS];
        public final float[] xs = new float[POINTERS];
        public final float[] ys = new float[POINTERS];
        public final float[] pressures = new float[POINTERS];
    }

    private final float[] x = new float[POINTERS];
    private final float[] y = new float[POINTERS];
    private final float[] pressure = new float[POINTERS];
    private final int[] androidOf = new int[POINTERS];
    private final int[] clientOf = new int[POINTERS];
    private int active;
    private int assigned;
    private int gestureDisplay;

    private final Event[] pool = new Event[1 + 2 * POINTERS];
    private final int[] lifting = new int[POINTERS];
    private final boolean[] liftCanceled = new boolean[POINTERS];
    private final int[] pressing = new int[POINTERS];
    private final float[] pressX = new float[POINTERS];
    private final float[] pressY = new float[POINTERS];
    private final float[] pressPressure = new float[POINTERS];

    public TouchPlanner() {
        for (int i = 0; i < pool.length; ++i) {
            pool[i] = new Event();
        }
    }

    public boolean anyDown() {
        return active != 0;
    }

    public Event event(int index) {
        return pool[index];
    }

    public int plan(TouchFrame frame, TouchGeometry current) {
        final boolean stale = current == null || frame.videoSession != current.session();
        boolean moved = false;
        int lifts = 0;
        int presses = 0;
        for (TouchPoint point : frame.points) {
            final int id = point.pointerId;
            final boolean down = (active & (1 << id)) != 0;
            final TouchAction action = point.action;
            if (action == TouchAction.UP || action == TouchAction.CANCEL) {
                if (!down) {
                    continue;
                }
                if (!stale && action == TouchAction.UP) {
                    moved |= moveTo(id, current.displayX(point.x), current.displayY(point.y));
                }
                lifting[lifts] = id;
                liftCanceled[lifts] = action == TouchAction.CANCEL;
                ++lifts;
            } else if (!stale) {
                final float px = current.displayX(point.x);
                final float py = current.displayY(point.y);
                if (down) {
                    moved |= moveTo(id, px, py);
                    pressure[id] = pressureOf(point);
                } else if (action == TouchAction.DOWN) {
                    pressing[presses] = id;
                    pressX[presses] = px;
                    pressY[presses] = py;
                    pressPressure[presses] = pressureOf(point);
                    ++presses;
                }
            }
        }

        int count = 0;
        if (moved) {
            fill(pool[count++], ACTION_MOVE, false, false);
        }
        for (int i = 0; i < lifts; ++i) {
            final int id = lifting[i];
            final boolean last = Integer.bitCount(active) == 1;
            final int action = last ? (liftCanceled[i] ? ACTION_CANCEL : ACTION_UP)
                    : ACTION_POINTER_UP | (indexOf(id) << ACTION_POINTER_INDEX_SHIFT);
            fill(pool[count++], action, liftCanceled[i] && !last, false);
            release(id);
        }
        for (int i = 0; i < presses; ++i) {
            final int id = pressing[i];
            final boolean first = active == 0;
            if (first) {
                gestureDisplay = current.displayId();
            }
            x[id] = pressX[i];
            y[id] = pressY[i];
            pressure[id] = pressPressure[i];
            final int android = Integer.numberOfTrailingZeros(~assigned);
            androidOf[id] = android;
            clientOf[android] = id;
            assigned |= 1 << android;
            active |= 1 << id;
            final int action = first ? ACTION_DOWN : ACTION_POINTER_DOWN | (indexOf(id) << ACTION_POINTER_INDEX_SHIFT);
            fill(pool[count++], action, false, first);
        }
        return count;
    }

    public int cancelAll() {
        int count = 0;
        while (active != 0) {
            final int id = Integer.numberOfTrailingZeros(active);
            final boolean last = Integer.bitCount(active) == 1;
            fill(pool[count++], last ? ACTION_CANCEL : ACTION_POINTER_UP | (indexOf(id) << ACTION_POINTER_INDEX_SHIFT),
                    !last, false);
            release(id);
        }
        return count;
    }

    private void release(int id) {
        active &= ~(1 << id);
        assigned &= ~(1 << androidOf[id]);
    }

    private boolean moveTo(int id, float px, float py) {
        if (x[id] == px && y[id] == py) {
            return false;
        }
        x[id] = px;
        y[id] = py;
        return true;
    }

    private int indexOf(int id) {
        return Integer.bitCount(assigned & ((1 << androidOf[id]) - 1));
    }

    private void fill(Event event, int action, boolean canceled, boolean gestureStart) {
        event.action = action;
        event.canceled = canceled;
        event.gestureStart = gestureStart;
        event.displayId = gestureDisplay;
        int n = 0;
        for (int bits = assigned; bits != 0; bits &= bits - 1) {
            final int android = Integer.numberOfTrailingZeros(bits);
            final int id = clientOf[android];
            event.ids[n] = android;
            event.xs[n] = x[id];
            event.ys[n] = y[id];
            event.pressures[n] = pressure[id];
            ++n;
        }
        event.pointerCount = n;
    }

    private static float pressureOf(TouchPoint point) {
        return point.pressure / FULL_PRESSURE;
    }
}
