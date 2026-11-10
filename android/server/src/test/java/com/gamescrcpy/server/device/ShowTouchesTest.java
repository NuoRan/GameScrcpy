package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.gamescrcpy.server.wrappers.PlatformException;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class ShowTouchesTest {

    private static final class FakeStore implements ShowTouches.Store {
        String value;
        boolean refuseReads;
        boolean refuseWrites;
        final List<String> writes = new ArrayList<>();

        FakeStore(String value) {
            this.value = value;
        }

        @Override
        public String get() throws PlatformException {
            if (refuseReads) {
                throw new PlatformException("refused");
            }
            return value;
        }

        @Override
        public void put(String newValue) throws PlatformException {
            if (refuseWrites) {
                throw new PlatformException("refused");
            }
            writes.add(newValue);
            value = newValue;
        }
    }

    @Test
    void turningOnRemembersTheSettingAndTurningOffGivesItBack() throws PlatformException {
        final FakeStore store = new FakeStore("0");
        final ShowTouches touches = new ShowTouches(store);
        touches.set(true);
        assertEquals("1", store.value);
        touches.set(true);
        touches.set(false);
        assertEquals("0", store.value);
        assertEquals(List.of("1", "1", "0"), store.writes);
        touches.set(false);
        touches.restore();
        assertEquals(List.of("1", "1", "0"), store.writes);
    }

    @Test
    void aPhoneThatAlreadyShowsTouchesKeepsShowingThem() throws PlatformException {
        final FakeStore store = new FakeStore("1");
        final ShowTouches touches = new ShowTouches(store);
        touches.set(true);
        touches.restore();
        assertEquals("1", store.value);
    }

    @Test
    void anUnsetSettingIsGivenBackAsOff() throws PlatformException {
        final FakeStore store = new FakeStore(null);
        final ShowTouches touches = new ShowTouches(store);
        touches.set(true);
        touches.restore();
        assertEquals("0", store.value);
    }

    @Test
    void anUnreadableSettingIsNotChanged() {
        final FakeStore store = new FakeStore("0");
        store.refuseReads = true;
        final ShowTouches touches = new ShowTouches(store);
        assertThrows(PlatformException.class, () -> touches.set(true));
        touches.restore();
        assertEquals(List.of(), store.writes);
    }

    @Test
    void aRefusedRestoreIsLoggedAndNotRetried() throws PlatformException {
        final FakeStore store = new FakeStore("0");
        final ShowTouches touches = new ShowTouches(store);
        touches.set(true);
        store.refuseWrites = true;
        touches.restore();
        store.refuseWrites = false;
        touches.restore();
        assertEquals(List.of("1"), store.writes);
    }
}
