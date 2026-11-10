package com.gamescrcpy.server.input;

import static com.gamescrcpy.server.input.TouchPlanner.ACTION_CANCEL;
import static com.gamescrcpy.server.input.TouchPlanner.ACTION_DOWN;
import static com.gamescrcpy.server.input.TouchPlanner.ACTION_MOVE;
import static com.gamescrcpy.server.input.TouchPlanner.ACTION_POINTER_DOWN;
import static com.gamescrcpy.server.input.TouchPlanner.ACTION_POINTER_INDEX_SHIFT;
import static com.gamescrcpy.server.input.TouchPlanner.ACTION_POINTER_UP;
import static com.gamescrcpy.server.input.TouchPlanner.ACTION_UP;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.TouchAction;
import com.gamescrcpy.protocol.Messages.TouchFrame;
import com.gamescrcpy.protocol.Messages.TouchPoint;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.jupiter.api.Test;

class TouchPlannerTest {

    private static final TouchGeometry SESSION_1 = new TouchGeometry(1, 0, 1080, 2400);
    private static final TouchGeometry SESSION_2 = new TouchGeometry(2, 0, 2400, 1080);
    private static final float PIXEL = 0.05f;

    private final TouchPlanner planner = new TouchPlanner();

    private static int at(double fraction) {
        return (int) Math.round(fraction * 0xFFFF);
    }

    private static TouchFrame frame(int session, TouchPoint... points) {
        final TouchFrame frame = new TouchFrame();
        frame.videoSession = session;
        for (TouchPoint point : points) {
            frame.points.add(point);
        }
        return frame;
    }

    private static TouchPoint down(int id, int x, int y) {
        return new TouchPoint(TouchAction.DOWN, id, x, y, 0xFFFF);
    }

    private static TouchPoint move(int id, int x, int y) {
        return new TouchPoint(TouchAction.MOVE, id, x, y, 0xFFFF);
    }

    private static TouchPoint up(int id, int x, int y) {
        return new TouchPoint(TouchAction.UP, id, x, y, 0);
    }

    private static TouchPoint cancel(int id) {
        return new TouchPoint(TouchAction.CANCEL, id, 0, 0, 0);
    }

    private List<String> plan(TouchFrame frame, TouchGeometry current) {
        final int count = planner.plan(frame, current);
        return describe(count);
    }

    private List<String> describe(int count) {
        final List<String> events = new ArrayList<>();
        for (int i = 0; i < count; ++i) {
            final TouchPlanner.Event event = planner.event(i);
            final int masked = event.action & 0xFF;
            final String name = switch (masked) {
                case ACTION_DOWN -> "DOWN";
                case ACTION_UP -> "UP";
                case ACTION_MOVE -> "MOVE";
                case ACTION_CANCEL -> "CANCEL";
                case ACTION_POINTER_DOWN -> "POINTER_DOWN#" + (event.action >> ACTION_POINTER_INDEX_SHIFT);
                case ACTION_POINTER_UP -> "POINTER_UP#" + (event.action >> ACTION_POINTER_INDEX_SHIFT);
                default -> "?" + event.action;
            };
            events.add(name + (event.canceled ? "!" : "")
                    + Arrays.toString(Arrays.copyOf(event.ids, event.pointerCount)).replace(" ", ""));
        }
        return events;
    }

    @Test
    void tapIsOneDownAndOneUpAtDisplayCoordinates() {
        assertEquals(List.of("DOWN[0]"), plan(frame(1, down(0, at(0.5), at(0.5))), SESSION_1));
        final TouchPlanner.Event press = planner.event(0);
        assertTrue(press.gestureStart);
        assertEquals(540f, press.xs[0], PIXEL);
        assertEquals(1200f, press.ys[0], PIXEL);
        assertEquals(1f, press.pressures[0]);
        assertEquals(List.of("UP[0]"), plan(frame(1, up(0, at(0.5), at(0.5))), SESSION_1));
        assertFalse(planner.anyDown());
    }

    @Test
    void coordinatesAreFractionsOfThePicture() {
        plan(frame(1, down(0, 0, 0), down(1, at(0.25), at(0.75))), SESSION_1);
        assertEquals(0f, planner.event(0).xs[0]);
        assertEquals(0f, planner.event(0).ys[0]);
        assertEquals(270f, planner.event(1).xs[1], PIXEL);
        assertEquals(1800f, planner.event(1).ys[1], PIXEL);
        planner.cancelAll();
        assertEquals(List.of("DOWN[0]"), plan(frame(2, down(2, at(0.25), at(0.75))), SESSION_2));
        assertEquals(600f, planner.event(0).xs[0], PIXEL);
        assertEquals(810f, planner.event(0).ys[0], PIXEL);
    }

    @Test
    void severalPressesInOneFrame() {
        assertEquals(List.of("DOWN[0]", "POINTER_DOWN#1[0,1]", "POINTER_DOWN#2[0,1,2]"),
                plan(frame(1, down(3, 10, 10), down(1, 20, 20), down(7, 30, 30)), SESSION_1));
        assertFalse(planner.event(1).gestureStart);
    }

    @Test
    void movesFirstThenLiftsThenPresses() {
        plan(frame(1, down(0, at(0.1), at(0.1)), down(1, at(0.2), at(0.2))), SESSION_1);
        assertEquals(List.of("MOVE[0,1]", "POINTER_UP#1[0,1]", "POINTER_DOWN#1[0,1]"),
                plan(frame(1, move(0, at(0.125), at(0.1)), up(1, at(0.25), at(0.2)), down(2, at(0.3), at(0.3))),
                        SESSION_1));
        final TouchPlanner.Event moveEvent = planner.event(0);
        assertEquals(135f, moveEvent.xs[0], PIXEL);
        assertEquals(270f, moveEvent.xs[1], PIXEL);
        assertEquals(270f, planner.event(1).xs[1], PIXEL);
        assertEquals(324f, planner.event(2).xs[1], PIXEL);
    }

    @Test
    void eachNewFingerTakesTheLowestFreeAndroidIdLikeATouchscreen() {
        for (int id = 0; id < 32; ++id) {
            assertEquals(List.of("DOWN[0]"), plan(frame(1, down(id, at(0.5), at(0.5))), SESSION_1));
            assertEquals(List.of("UP[0]"), plan(frame(1, up(id, at(0.5), at(0.5))), SESSION_1));
        }
        assertEquals(List.of("DOWN[0]"), plan(frame(1, down(15, at(0.1), at(0.1))), SESSION_1));
        assertEquals(List.of("POINTER_DOWN#1[0,1]"), plan(frame(1, down(20, at(0.2), at(0.2))), SESSION_1));
        assertEquals(List.of("POINTER_UP#0[0,1]"), plan(frame(1, up(15, at(0.1), at(0.1))), SESSION_1));
        assertEquals(List.of("POINTER_DOWN#0[0,1]"), plan(frame(1, down(27, at(0.3), at(0.3))), SESSION_1));
        final TouchPlanner.Event press = planner.event(0);
        assertEquals(324f, press.xs[0], PIXEL);
        assertEquals(216f, press.xs[1], PIXEL);
        assertEquals(List.of("MOVE[0,1]"), plan(frame(1, move(20, at(0.25), at(0.2))), SESSION_1));
        assertEquals(270f, planner.event(0).xs[1], PIXEL);
        assertEquals(List.of("POINTER_UP#1[0,1]"), plan(frame(1, up(20, at(0.25), at(0.2))), SESSION_1));
        assertEquals(List.of("UP[0]"), plan(frame(1, up(27, at(0.3), at(0.3))), SESSION_1));
        assertFalse(planner.anyDown());
    }

    @Test
    void unchangedPositionsNeedNoMove() {
        plan(frame(1, down(0, 100, 100)), SESSION_1);
        assertEquals(List.of(), plan(frame(1, move(0, 100, 100)), SESSION_1));
        assertEquals(List.of("UP[0]"), plan(frame(1, up(0, 100, 100)), SESSION_1));
    }

    @Test
    void pressingAPointerThatIsDownMovesIt() {
        plan(frame(1, down(0, 100, 100)), SESSION_1);
        assertEquals(List.of("MOVE[0]"), plan(frame(1, down(0, 104, 100)), SESSION_1));
    }

    @Test
    void pointersThatAreNotDownAreIgnored() {
        assertEquals(List.of(), plan(frame(1, move(5, 1, 1), up(6, 1, 1), cancel(7)), SESSION_1));
        assertFalse(planner.anyDown());
    }

    @Test
    void staleSessionOnlyLiftsAtTheLastPosition() {
        plan(frame(1, down(0, at(0.1), at(0.1)), down(1, at(0.2), at(0.2))), SESSION_1);
        assertEquals(List.of("POINTER_UP#0[0,1]"),
                plan(frame(1, up(0, at(0.9), at(0.9)), move(1, at(0.5), at(0.5)), down(2, 10, 10)), SESSION_2));
        assertEquals(108f, planner.event(0).xs[0], PIXEL);
        assertEquals(List.of("CANCEL[1]"), plan(frame(1, cancel(1)), SESSION_2));
        assertFalse(planner.anyDown());
        assertEquals(List.of("DOWN[0]"), plan(frame(2, down(4, at(0.5), at(0.5))), SESSION_2));
        assertEquals(1200f, planner.event(0).xs[0], PIXEL);
    }

    @Test
    void withoutVideoNothingIsPressed() {
        assertEquals(List.of(), plan(frame(1, down(0, 1, 1)), null));
    }

    @Test
    void cancelLiftsWithoutATap() {
        plan(frame(1, down(0, 100, 100), down(1, 200, 200)), SESSION_1);
        assertEquals(List.of("POINTER_UP#1![0,1]"), plan(frame(1, cancel(1)), SESSION_1));
        assertEquals(List.of("CANCEL[0]"), plan(frame(1, cancel(0)), SESSION_1));
    }

    @Test
    void cancelAllReleasesEveryPointer() {
        plan(frame(1, down(2, 1, 1), down(9, 2, 2), down(30, 3, 3)), SESSION_1);
        assertEquals(List.of("POINTER_UP#0![0,1,2]", "POINTER_UP#0![1,2]", "CANCEL[2]"),
                describe(planner.cancelAll()));
        assertFalse(planner.anyDown());
        assertEquals(0, planner.cancelAll());
    }

    @Test
    void thirtyTwoFingers() {
        final TouchPoint[] presses = new TouchPoint[32];
        final TouchPoint[] moves = new TouchPoint[32];
        for (int id = 0; id < 32; ++id) {
            presses[id] = down(id, id * 20, 100);
            moves[id] = move(id, id * 20, 150);
        }
        final List<String> pressed = plan(frame(1, presses), SESSION_1);
        assertEquals(32, pressed.size());
        assertEquals("DOWN[0]", pressed.get(0));
        assertTrue(pressed.get(31).startsWith("POINTER_DOWN#31["));
        assertEquals(1, planner.plan(frame(1, moves), SESSION_1));
        final TouchPlanner.Event moved = planner.event(0);
        assertEquals(32, moved.pointerCount);
        final int[] ids = new int[32];
        Arrays.setAll(ids, i -> i);
        assertArrayEquals(ids, Arrays.copyOf(moved.ids, 32));
    }

    @Test
    void coordinatesAreClampedToTheDisplay() {
        plan(frame(1, down(0, 0xFFFF, 0xFFFF)), SESSION_1);
        assertEquals(1079f, planner.event(0).xs[0]);
        assertEquals(2399f, planner.event(0).ys[0]);
    }
}
