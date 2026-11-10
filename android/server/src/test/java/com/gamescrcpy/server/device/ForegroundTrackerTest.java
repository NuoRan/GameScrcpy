package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Messages.ForegroundAppChanged;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

class ForegroundTrackerTest {

    private static final int CATEGORY_UNDEFINED = -1;
    private static final int CATEGORY_GAME = 0;
    private static final int CATEGORY_AUDIO = 1;
    private static final int FLAG_SYSTEM = 1;
    private static final int FLAG_IS_GAME = 1 << 25;

    @Test
    void reportsOnlyWhatChanged() {
        final ForegroundTracker tracker = new ForegroundTracker();
        assertFalse(tracker.update("", "", false));
        assertFalse(tracker.update(null, null, false));
        assertTrue(tracker.update("com.example.game", "com.example.game.MainActivity", true));
        assertFalse(tracker.update("com.example.game", "com.example.game.MainActivity", true));
        assertTrue(tracker.update("com.example.game", "com.example.game.ShopActivity", true));
        assertTrue(tracker.update("com.android.launcher3", "com.android.launcher3.Launcher", false));

        final ForegroundAppChanged message = tracker.message();
        assertEquals("com.android.launcher3", message.packageName);
        assertEquals("com.android.launcher3.Launcher", message.activity);
        assertEquals(0, message.flags);
    }

    @Test
    void theGameMarkTravelsWithTheAppAndItsChangeIsReported() {
        final ForegroundTracker tracker = new ForegroundTracker();
        assertTrue(tracker.update("com.example.game", "com.example.game.MainActivity", true));
        assertEquals(ForegroundAppChanged.GAME, tracker.message().flags);
        assertTrue(tracker.update("com.example.game", "com.example.game.MainActivity", false));
        assertEquals(0, tracker.message().flags);
        assertFalse(tracker.update("com.example.game", "com.example.game.MainActivity", false));
    }

    @Test
    void nothingInFrontIsNeverAGame() {
        final ForegroundTracker tracker = new ForegroundTracker();
        assertFalse(tracker.update("", "", true));
        assertEquals(0, tracker.message().flags);
        assertTrue(tracker.update("com.example.game", "", true));
        assertTrue(tracker.update(null, null, true));
        assertEquals("", tracker.message().packageName);
        assertEquals(0, tracker.message().flags);
    }

    @Test
    void gamesAreTheGameCategoryOrTheOldGameFlag() {
        assertTrue(ForegroundTracker.isGame(CATEGORY_GAME, 0));
        assertTrue(ForegroundTracker.isGame(CATEGORY_UNDEFINED, FLAG_IS_GAME));
        assertTrue(ForegroundTracker.isGame(CATEGORY_AUDIO, FLAG_IS_GAME | FLAG_SYSTEM));
        assertFalse(ForegroundTracker.isGame(CATEGORY_UNDEFINED, 0));
        assertFalse(ForegroundTracker.isGame(CATEGORY_AUDIO, FLAG_SYSTEM));
    }

    @Test
    void longNamesAreClippedToTheWireLimitOnACharacterBoundary() {
        final String ascii = "a".repeat(300);
        assertEquals(255, ForegroundTracker.clip(ascii).length());
        final String wide = "\u6e38".repeat(100);
        final String clipped = ForegroundTracker.clip(wide);
        assertTrue(clipped.getBytes(StandardCharsets.UTF_8).length <= 255);
        assertEquals(85, clipped.length());
        final String emoji = "\uD83C\uDFAE".repeat(70);
        final String cut = ForegroundTracker.clip(emoji);
        assertTrue(cut.getBytes(StandardCharsets.UTF_8).length <= 255);
        assertFalse(Character.isHighSurrogate(cut.charAt(cut.length() - 1)));
    }
}
