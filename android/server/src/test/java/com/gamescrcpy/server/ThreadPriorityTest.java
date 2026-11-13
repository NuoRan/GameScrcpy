package com.gamescrcpy.server;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

class ThreadPriorityTest {

    private final ByteArrayOutputStream errors = new ByteArrayOutputStream();
    private PrintStream previous;

    @BeforeEach
    void captureTheLog() {
        previous = System.err;
        System.setErr(new PrintStream(errors, true, StandardCharsets.UTF_8));
        Log.setMinimum(Log.Level.DEBUG);
        ThreadPriority.resetLogged();
    }

    @AfterEach
    void restoreTheLog() {
        Log.setMinimum(Log.Level.INFO);
        System.setErr(previous);
    }

    private String log() {
        return errors.toString(StandardCharsets.UTF_8);
    }

    private static int count(String text, String part) {
        int count = 0;
        for (int at = text.indexOf(part); at >= 0; at = text.indexOf(part, at + part.length())) {
            ++count;
        }
        return count;
    }

    @Test
    void adr0114VideoIsMinusTenAndAudioIsMinusSixteen() {
        assertEquals(-10, ThreadPriority.VIDEO);
        assertEquals(-16, ThreadPriority.AUDIO);
    }

    @Test
    void adr0114TheSetterIsCalledWithTheGivenPriority() {
        final List<Integer> seen = new ArrayList<>();
        ThreadPriority.raise(ThreadPriority.VIDEO, seen::add);
        ThreadPriority.raise(ThreadPriority.AUDIO, seen::add);
        assertEquals(List.of(-10, -16), seen);
    }

    @Test
    void adr0114AnAppliedPriorityIsLoggedOnceAndAtDebugLevel() {
        ThreadPriority.raise(ThreadPriority.VIDEO, priority -> { });
        ThreadPriority.raise(ThreadPriority.VIDEO, priority -> { });
        ThreadPriority.raise(ThreadPriority.AUDIO, priority -> { });
        final String text = log();
        assertEquals(1, count(text, "thread priority -10 applied"));
        assertEquals(1, count(text, "thread priority -16 applied"));
        assertTrue(text.contains("DEBUG: thread priority -10 applied"));
        assertTrue(text.contains("DEBUG: thread priority -16 applied"));
    }

    @Test
    void adr0114ARefusedPriorityIsSwallowedAndLoggedOnce() {
        final ThreadPriority.Setter refused = priority -> {
            throw new SecurityException("no permission to set the priority");
        };
        assertDoesNotThrow(() -> ThreadPriority.raise(ThreadPriority.VIDEO, refused));
        ThreadPriority.raise(ThreadPriority.AUDIO, refused);
        ThreadPriority.raise(ThreadPriority.VIDEO, refused);
        final String text = log();
        assertEquals(1, count(text, "java.lang.SecurityException: no permission to set the priority"));
        assertTrue(text.contains("DEBUG: could not raise the thread priority to -10: java.lang.SecurityException"));
    }

    @Test
    void adr0114AnIllegalArgumentIsSwallowedToo() {
        assertDoesNotThrow(() -> ThreadPriority.raise(ThreadPriority.AUDIO, priority -> {
            throw new IllegalArgumentException();
        }));
    }

    @Test
    void adr0114TheHostStubFailureDoesNotEscapeTheThread() {
        final ThreadPriority.Setter stub = priority -> {
            throw new RuntimeException("Stub!");
        };
        assertDoesNotThrow(() -> ThreadPriority.raise(ThreadPriority.VIDEO, stub));
        assertTrue(log().contains("DEBUG: could not raise the thread priority to -10: java.lang.RuntimeException"));
    }

    @Test
    void adr0114AHousekeepingThreadStartsAtTheDefaultPriorityAndStillRuns() {
        final boolean[] ran = {false};
        assertDoesNotThrow(() -> ThreadPriority.atDefault(() -> ran[0] = true).run());
        assertTrue(ran[0]);
        assertEquals(0, ThreadPriority.DEFAULT);
    }
}
