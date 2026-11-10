package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.SystemPanelAction;
import com.gamescrcpy.protocol.Messages.SystemPanel;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class SystemPanelsTest {

    private record Told(ErrorCode code, String message) {
    }

    private static SystemPanel request(SystemPanelAction action) {
        final SystemPanel request = new SystemPanel();
        request.action = action;
        return request;
    }

    @Test
    void eachRequestAsksThePlatformForItsPanelAndTellsNothing() {
        final List<SystemPanelAction> shown = new ArrayList<>();
        final List<Told> told = new ArrayList<>();
        final SystemPanels panels = new SystemPanels(shown::add, (code, message) -> told.add(new Told(code, message)));
        panels.handle(request(SystemPanelAction.EXPAND_NOTIFICATIONS));
        panels.handle(request(SystemPanelAction.EXPAND_QUICK_SETTINGS));
        panels.handle(request(SystemPanelAction.COLLAPSE));
        assertEquals(List.of(SystemPanelAction.EXPAND_NOTIFICATIONS, SystemPanelAction.EXPAND_QUICK_SETTINGS,
                SystemPanelAction.COLLAPSE), shown);
        assertEquals(List.of(), told);
    }

    @Test
    void aFailureIsToldWithItsCodeAndAMessageTheComputerRecognizes() {
        final List<Told> told = new ArrayList<>();
        final SystemPanels panels = new SystemPanels(action -> {
            throw new SystemPanels.Failure(ErrorCode.PERMISSION_DENIED, "refused");
        }, (code, message) -> told.add(new Told(code, message)));
        panels.handle(request(SystemPanelAction.COLLAPSE));
        assertEquals(List.of(new Told(ErrorCode.PERMISSION_DENIED, SystemPanels.NOTICE_PREFIX + "refused")), told);
    }

    @Test
    void anUnexpectedErrorIsToldAsInternal() {
        final List<Told> told = new ArrayList<>();
        final SystemPanels panels = new SystemPanels(action -> {
            throw new IllegalStateException("broken");
        }, (code, message) -> told.add(new Told(code, message)));
        panels.handle(request(SystemPanelAction.EXPAND_NOTIFICATIONS));
        assertEquals(1, told.size());
        assertEquals(ErrorCode.INTERNAL_ERROR, told.get(0).code());
        assertTrue(told.get(0).message().startsWith(SystemPanels.NOTICE_PREFIX));
        assertTrue(told.get(0).message().contains("broken"));
    }

    @Test
    void withoutTheStatusBarServiceEveryPanelIsUnsupported() {
        final List<Told> told = new ArrayList<>();
        final SystemPanels panels = new SystemPanels(SystemPanels.unavailable("system service statusbar is not running"),
                (code, message) -> told.add(new Told(code, message)));
        for (SystemPanelAction action : SystemPanelAction.values()) {
            panels.handle(request(action));
        }
        assertEquals(SystemPanelAction.values().length, told.size());
        for (Told each : told) {
            assertEquals(ErrorCode.UNSUPPORTED_ANDROID, each.code());
            assertTrue(each.message().startsWith(SystemPanels.NOTICE_PREFIX));
            assertTrue(each.message().contains("statusbar is not running"));
        }
    }
}
