package com.genymobile.scrcpy.control;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;

import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.util.Ln;

/**
 * FastTouch - High Performance Multi-Touch Handler
 *
 * Optimizations:
 * 1. Active pointer list instead of 256-element scan
 * 2. Pre-allocated PointerProperties/PointerCoords arrays
 * 3. No object allocation in hot path
 * 4. O(1) seqId to pointer lookup
 */
public class FastTouch {

    private static final int DEFAULT_DEVICE_ID = 0;
    private static final int MAX_POINTERS = 10;
    private static final int MAX_SEQ_ID = 256;

    // C-02: 移除自定义对象池，使用 MotionEvent 内置池

    // seqId -> index in activePointers (-1 if not active)
    private final int[] seqIdToIndex = new int[MAX_SEQ_ID];

    // Active pointers (compact list, no gaps)
    private final int[] activePointerIds = new int[MAX_POINTERS];
    private final int[] activeSeqIds = new int[MAX_POINTERS];
    private final float[] activeX = new float[MAX_POINTERS];
    private final float[] activeY = new float[MAX_POINTERS];
    private final float[] activePressure = new float[MAX_POINTERS];
    private int activeCount = 0;

    // Pre-allocated for MotionEvent - reuse same arrays
    private final MotionEvent.PointerProperties[] props = new MotionEvent.PointerProperties[MAX_POINTERS];
    private final MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[MAX_POINTERS];

    // Display size
    private volatile int displayWidth = 1920;
    private volatile int displayHeight = 1080;
    private volatile int targetDisplayId = 0;

    // Global DOWN time
    private long globalDownTime = 0;

    // pointerId allocation bitmap
    private int usedPointerIdBitmap = 0;

    public FastTouch() {
        // Initialize seqId lookup table
        for (int i = 0; i < MAX_SEQ_ID; i++) {
            seqIdToIndex[i] = -1;
        }

        // Pre-allocate PointerProperties and PointerCoords
        for (int i = 0; i < MAX_POINTERS; i++) {
            props[i] = new MotionEvent.PointerProperties();
            props[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coords[i] = new MotionEvent.PointerCoords();
            coords[i].size = 1f;
        }
        // C-02: 移除自定义对象池预热，使用 MotionEvent 内置池
    }

    public void start() {
    }

    public void stop() {
        reset();
        // C-02: 移除自定义对象池清理
    }

    /**
     * C-02: 修复对象池逻辑 - 直接使用 MotionEvent.obtain() 的内置池
     *
     * 原问题: 从池中取出后立即 recycle()，然后又创建新对象，
     * 实际上并没有复用对象池中的对象。
     *
     * 修复方案: MotionEvent.obtain() 本身就有对象池机制，
     * 我们只需要确保在使用后调用 recycle() 即可。
     * 移除自定义对象池，避免重复管理。
     */
    private MotionEvent obtainMotionEvent(long downTime, long eventTime, int action,
                                          int pointerCount, int displayId) {
        // 直接使用 MotionEvent.obtain() 的内置对象池
        return MotionEvent.obtain(
                downTime, eventTime, action, pointerCount, props, coords,
                0, 0, 1f, 1f, DEFAULT_DEVICE_ID, 0,
                InputDevice.SOURCE_TOUCHSCREEN, 0);
    }

    /**
     * C-02: 回收 MotionEvent 到系统内置对象池
     */
    private void recycleMotionEvent(MotionEvent event) {
        // 直接调用 recycle()，让系统管理对象池
        if (event != null) {
            event.recycle();
        }
    }

    public void setDisplaySize(int width, int height) {
        this.displayWidth = width;
        this.displayHeight = height;
    }

    public void setTargetDisplayId(int displayId) {
        this.targetDisplayId = displayId;
    }

    private int allocatePointerId() {
        for (int i = 0; i < MAX_POINTERS; i++) {
            if ((usedPointerIdBitmap & (1 << i)) == 0) {
                usedPointerIdBitmap |= (1 << i);
                return i;
            }
        }
        return -1;
    }

    private void releasePointerId(int pointerId) {
        if (pointerId >= 0 && pointerId < MAX_POINTERS) {
            usedPointerIdBitmap &= ~(1 << pointerId);
        }
    }

    /**
     * C-01: 释放指定索引位置的触摸点
     * 使用交换删除法保持数组紧凑
     *
     * @param index 要释放的触摸点在活动列表中的索引
     * @param seqIdx seqId 的低 8 位，用于更新查找表
     */
    private void releasePointerAt(int index, int seqIdx) {
        if (index < 0 || index >= activeCount) {
            return;
        }
        int pointerId = activePointerIds[index];
        releasePointerId(pointerId);
        seqIdToIndex[seqIdx] = -1;
        activeCount--;
        if (index < activeCount) {
            // 将最后一个元素移动到当前位置（交换删除）
            activePointerIds[index] = activePointerIds[activeCount];
            activeSeqIds[index] = activeSeqIds[activeCount];
            activeX[index] = activeX[activeCount];
            activeY[index] = activeY[activeCount];
            activePressure[index] = activePressure[activeCount];
            seqIdToIndex[activeSeqIds[index] & 0xFF] = index;
        }
        if (activeCount == 0) {
            globalDownTime = 0;
        }
    }

    public boolean inject(int seqId, int action, int x, int y, int displayId) {
        int idx = seqId & 0xFF;
        long now = SystemClock.uptimeMillis();

        float fx = (float) x / 65535f * displayWidth;
        float fy = (float) y / 65535f * displayHeight;

        int motionAction;

        switch (action) {
            case 0: // DOWN
                // C-01: 如果该 seqId 已存在，先释放旧的触摸点
                int existingIndex = seqIdToIndex[idx];
                if (existingIndex >= 0) {
                    releasePointerAt(existingIndex, idx);
                }

                if (activeCount >= MAX_POINTERS) {
                    Ln.w("FastTouch: Max pointers reached");
                    return false;
                }

                int pointerId = allocatePointerId();
                if (pointerId < 0) {
                    return false;
                }

                // Add to active list
                int newIndex = activeCount;
                activePointerIds[newIndex] = pointerId;
                activeSeqIds[newIndex] = seqId;
                activeX[newIndex] = fx;
                activeY[newIndex] = fy;
                activePressure[newIndex] = 1f;
                seqIdToIndex[idx] = newIndex;
                activeCount++;

                if (activeCount == 1) {
                    motionAction = MotionEvent.ACTION_DOWN;
                    globalDownTime = now;
                } else {
                    // Find pointer index after sorting by pointerId
                    int pointerIndex = 0;
                    for (int i = 0; i < activeCount; i++) {
                        if (activePointerIds[i] < pointerId) {
                            pointerIndex++;
                        }
                    }
                    motionAction = MotionEvent.ACTION_POINTER_DOWN
                            | (pointerIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
                }
                break;

            case 1: // UP
                int upIndex = seqIdToIndex[idx];
                if (upIndex < 0) {
                    return true; // Already released
                }

                int upPointerId = activePointerIds[upIndex];
                activeX[upIndex] = fx;
                activeY[upIndex] = fy;
                activePressure[upIndex] = 0f;

                if (activeCount == 1) {
                    motionAction = MotionEvent.ACTION_UP;
                } else {
                    // Find pointer index
                    int pointerIndex = 0;
                    for (int i = 0; i < activeCount; i++) {
                        if (activePointerIds[i] < upPointerId) {
                            pointerIndex++;
                        }
                    }
                    motionAction = MotionEvent.ACTION_POINTER_UP
                            | (pointerIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
                }

                // Inject before removing
                boolean result = injectMotionEvent(motionAction, now, displayId);

                // Remove from active list (swap with last)
                releasePointerId(upPointerId);
                seqIdToIndex[idx] = -1;
                activeCount--;
                if (upIndex < activeCount) {
                    // Move last to this position
                    activePointerIds[upIndex] = activePointerIds[activeCount];
                    activeSeqIds[upIndex] = activeSeqIds[activeCount];
                    activeX[upIndex] = activeX[activeCount];
                    activeY[upIndex] = activeY[activeCount];
                    activePressure[upIndex] = activePressure[activeCount];
                    seqIdToIndex[activeSeqIds[upIndex] & 0xFF] = upIndex;
                }

                if (activeCount == 0) {
                    globalDownTime = 0;
                }

                return result;

            case 2: // MOVE
                int moveIndex = seqIdToIndex[idx];
                if (moveIndex < 0) {
                    return true; // Not tracked
                }

                activeX[moveIndex] = fx;
                activeY[moveIndex] = fy;
                motionAction = MotionEvent.ACTION_MOVE;
                break;

            default:
                return false;
        }

        return injectMotionEvent(motionAction, now, displayId);
    }

    private boolean injectMotionEvent(int motionAction, long eventTime, int displayId) {
        if (activeCount == 0) {
            return true;
        }

        // Build sorted pointer arrays (sort by pointerId)
        // Use insertion sort since count is small (<=10)
        for (int i = 0; i < activeCount; i++) {
            props[i].id = activePointerIds[i];
            coords[i].x = activeX[i];
            coords[i].y = activeY[i];
            coords[i].pressure = activePressure[i];
        }

        // Simple insertion sort by props[i].id
        for (int i = 1; i < activeCount; i++) {
            int keyId = props[i].id;
            float keyX = coords[i].x;
            float keyY = coords[i].y;
            float keyP = coords[i].pressure;
            int j = i - 1;
            while (j >= 0 && props[j].id > keyId) {
                props[j + 1].id = props[j].id;
                coords[j + 1].x = coords[j].x;
                coords[j + 1].y = coords[j].y;
                coords[j + 1].pressure = coords[j].pressure;
                j--;
            }
            props[j + 1].id = keyId;
            coords[j + 1].x = keyX;
            coords[j + 1].y = keyY;
            coords[j + 1].pressure = keyP;
        }

        long downTime = globalDownTime > 0 ? globalDownTime : eventTime;

        // 使用对象池获取 MotionEvent
        MotionEvent event = obtainMotionEvent(downTime, eventTime, motionAction, activeCount, displayId);

        boolean result = Device.injectEvent(event, displayId, Device.INJECT_MODE_ASYNC);

        // 回收到对象池
        recycleMotionEvent(event);

        return result;
    }

    public boolean inject(int seqId, int action, int x, int y) {
        return inject(seqId, action, x, y, this.targetDisplayId);
    }

    /**
     * Batch inject (standard format): 9 bytes per event
     */
    public int injectBatch(byte[] events, int count) {
        int success = 0;
        int offset = 0;
        int displayId = this.targetDisplayId;

        for (int i = 0; i < count && offset + 9 <= events.length; i++) {
            int seqId = ((events[offset] & 0xFF) << 24) |
                    ((events[offset + 1] & 0xFF) << 16) |
                    ((events[offset + 2] & 0xFF) << 8) |
                    (events[offset + 3] & 0xFF);
            int action = events[offset + 4] & 0xFF;
            int x = ((events[offset + 5] & 0xFF) << 8) | (events[offset + 6] & 0xFF);
            int y = ((events[offset + 7] & 0xFF) << 8) | (events[offset + 8] & 0xFF);

            if (inject(seqId, action, x, y, displayId)) {
                success++;
            }
            offset += 9;
        }

        return success;
    }

    /**
     * Batch inject (compact format): 7 bytes per event
     */
    public int injectBatchCompact(byte[] events, int count) {
        int success = 0;
        int offset = 0;
        int displayId = this.targetDisplayId;

        for (int i = 0; i < count && offset + 7 <= events.length; i++) {
            int seqId = events[offset] & 0xFF;
            int action = events[offset + 1] & 0xFF;
            int x = ((events[offset + 2] & 0xFF) << 8) | (events[offset + 3] & 0xFF);
            int y = ((events[offset + 4] & 0xFF) << 8) | (events[offset + 5] & 0xFF);

            if (inject(seqId, action, x, y, displayId)) {
                success++;
            }
            offset += 7;
        }

        return success;
    }

    public void reset() {
        for (int i = 0; i < activeCount; i++) {
            releasePointerId(activePointerIds[i]);
            seqIdToIndex[activeSeqIds[i] & 0xFF] = -1;
        }
        activeCount = 0;
        usedPointerIdBitmap = 0;
        globalDownTime = 0;
    }

    public String getStats() {
        return String.format("activePointers=%d", activeCount);
    }
}
