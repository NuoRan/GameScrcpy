package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Messages.Display;
import org.junit.jupiter.api.Test;

class DisplaySnapshotTest {

    private static DisplaySnapshot snapshot(int width, int height, int rotation) {
        return new DisplaySnapshot(0, width, height, rotation, 0, 0, 420, 120_000, DisplaySnapshot.STATE_ON,
                0, 98, 0, 0);
    }

    @Test
    void protocolDisplayCarriesEveryField() {
        final Display display = snapshot(1080, 2400, 0).toProtocol();
        assertEquals(0, display.displayId);
        assertEquals(1080, display.width);
        assertEquals(2400, display.height);
        assertEquals(0, display.rotation);
        assertEquals(420, display.densityDpi);
        assertEquals(120_000, display.refreshMilliHz);
        assertEquals(98, display.safeTop);
    }

    @Test
    void outOfRangeValuesAreClamped() {
        final Display display = new DisplaySnapshot(-1, 70_000, -5, 5, 0, 0, 100_000, -1, 0, -1, 0, 0, 0)
                .toProtocol();
        assertEquals(0xFFFFFFFFL, display.displayId);
        assertEquals(0xFFFF, display.width);
        assertEquals(0, display.height);
        assertEquals(1, display.rotation);
        assertEquals(0xFFFF, display.densityDpi);
        assertEquals(0, display.refreshMilliHz);
        assertEquals(0, display.safeLeft);
    }

    @Test
    void geometryComparesSizeAndRotationOnly() {
        assertTrue(snapshot(1080, 2400, 0).sameGeometry(snapshot(1080, 2400, 0)));
        assertFalse(snapshot(1080, 2400, 0).sameGeometry(snapshot(2400, 1080, 1)));
        assertFalse(snapshot(1080, 2400, 0).sameGeometry(null));
        assertTrue(snapshot(1080, 2400, 0).isOn());
    }
}
