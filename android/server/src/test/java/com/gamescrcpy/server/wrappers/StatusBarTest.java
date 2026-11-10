package com.gamescrcpy.server.wrappers;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.SystemPanelAction;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Messages.SystemPanel;
import com.gamescrcpy.server.device.SystemPanels;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class StatusBarTest {

    public static final class CurrentService {
        public final List<String> calls = new ArrayList<>();

        public void expandNotificationsPanel() {
            calls.add("notifications");
        }

        public void expandSettingsPanel(String subPanel) {
            calls.add("settings " + subPanel);
        }

        public void collapsePanels() {
            calls.add("collapse");
        }

        public void togglePanel() {
            calls.add("toggle");
        }
    }

    public static final class OlderService {
        public final List<String> calls = new ArrayList<>();

        public void expandNotificationsPanel() {
            calls.add("notifications");
        }

        public void expandSettingsPanel() {
            calls.add("settings");
        }

        public void collapsePanels() {
            calls.add("collapse");
        }
    }

    public static final class PartialService {
        public final List<String> calls = new ArrayList<>();

        public void expandNotificationsPanel() {
            calls.add("notifications");
        }

        public void collapsePanels() {
            calls.add("collapse");
        }
    }

    public static final class RefusingService {
        public void expandNotificationsPanel() {
            throw new IllegalStateException("the system UI is not running");
        }

        public void expandSettingsPanel(String subPanel) {
            throw new SecurityException("needs android.permission.EXPAND_STATUS_BAR");
        }

        public void collapsePanels() {
            throw new SecurityException("needs android.permission.STATUS_BAR");
        }
    }

    @Test
    void androidSixToSixteenOpensEachPanelWithItsOwnMethod() throws SystemPanels.Failure {
        final CurrentService service = new CurrentService();
        final StatusBar bar = StatusBar.of(service);
        bar.show(SystemPanelAction.EXPAND_NOTIFICATIONS);
        bar.show(SystemPanelAction.EXPAND_QUICK_SETTINGS);
        bar.show(SystemPanelAction.COLLAPSE);
        assertEquals(List.of("notifications", "settings null", "collapse"), service.calls);
    }

    @Test
    void anOlderQuickSettingsMethodWithoutASubPanelIsCalledWithoutOne() throws SystemPanels.Failure {
        final OlderService service = new OlderService();
        final StatusBar bar = StatusBar.of(service);
        bar.show(SystemPanelAction.EXPAND_QUICK_SETTINGS);
        bar.show(SystemPanelAction.EXPAND_NOTIFICATIONS);
        assertEquals(List.of("settings", "notifications"), service.calls);
    }

    @Test
    void aPanelThisAndroidCannotOpenFailsAsUnsupportedAndTheOthersStillWork() throws SystemPanels.Failure {
        final PartialService service = new PartialService();
        final StatusBar bar = StatusBar.of(service);
        final SystemPanels.Failure failure = assertThrows(SystemPanels.Failure.class,
                () -> bar.show(SystemPanelAction.EXPAND_QUICK_SETTINGS));
        assertEquals(ErrorCode.UNSUPPORTED_ANDROID, failure.code());
        assertTrue(failure.getMessage().contains("quick settings"));
        bar.show(SystemPanelAction.COLLAPSE);
        assertEquals(List.of("collapse"), service.calls);
    }

    @Test
    void aRefusalIsReportedAsDeniedAndAnyOtherErrorAsInternal() {
        final StatusBar bar = StatusBar.of(new RefusingService());
        assertEquals(ErrorCode.PERMISSION_DENIED,
                assertThrows(SystemPanels.Failure.class, () -> bar.show(SystemPanelAction.COLLAPSE)).code());
        assertEquals(ErrorCode.PERMISSION_DENIED,
                assertThrows(SystemPanels.Failure.class,
                        () -> bar.show(SystemPanelAction.EXPAND_QUICK_SETTINGS)).code());
        assertEquals(ErrorCode.INTERNAL_ERROR,
                assertThrows(SystemPanels.Failure.class,
                        () -> bar.show(SystemPanelAction.EXPAND_NOTIFICATIONS)).code());
    }

    @Test
    void aDecodedSystemPanelMessageReachesTheMatchingStatusBarMethod() {
        final CurrentService service = new CurrentService();
        final List<String> notices = new ArrayList<>();
        final SystemPanels panels = new SystemPanels(StatusBar.of(service),
                (code, message) -> notices.add(code + " " + message));
        for (int action = 2; action >= 0; --action) {
            final byte[] frame = {0x03, 0x06, 0, 0, 0, 1, (byte) action};
            panels.handle((SystemPanel) Codec.decodeFrame(FrameContext.CONTROL, Direction.TO_SERVER, frame, 0,
                    frame.length));
        }
        assertEquals(List.of("collapse", "settings null", "notifications"), service.calls);
        assertEquals(List.of(), notices);
    }

    @Test
    void aFailedDecodedRequestIsToldToTheComputer() {
        final List<String> notices = new ArrayList<>();
        final SystemPanels panels = new SystemPanels(StatusBar.of(new PartialService()),
                (code, message) -> notices.add(code + " " + message));
        final byte[] frame = {0x03, 0x06, 0, 0, 0, 1, 1};
        panels.handle((SystemPanel) Codec.decodeFrame(FrameContext.CONTROL, Direction.TO_SERVER, frame, 0,
                frame.length));
        assertEquals(1, notices.size());
        assertTrue(notices.get(0).startsWith("UNSUPPORTED_ANDROID " + SystemPanels.NOTICE_PREFIX));
    }
}
