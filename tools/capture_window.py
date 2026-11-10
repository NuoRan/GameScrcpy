"""Saves screenshots of GameScrcpy windows as PNG and drives them with posted messages, without ever
reading the screen.

The window draws itself into a bitmap (PrintWindow with PW_RENDERFULLCONTENT), which is cropped to
the visible frame (DWMWA_EXTENDED_FRAME_BOUNDS). Reading the screen is forbidden: with the window
in the background it would record whatever else the user has open.

    python tools/capture_window.py --launch build/msvc-debug/bin/GameScrcpy.exe --out build/smoke/app.png
    python tools/capture_window.py --pid 1234 --out shot.png
    python tools/capture_window.py --launch GameScrcpy.exe --steps "lower;wait:3;shot:a.png;click:900,160;wait:2;shot:b.png"
    python tools/capture_window.py --launch GameScrcpy.exe --title GameScrcpy --arg=--demo-device "--arg=D:/my videos/demo.media" --steps "lower;window:Demo device;lower;hover:300,40;wait:0.5;shot:bar.png;window:main;shot:hub.png"

Each --arg= passes one argument to the launched program, spaces included.

Steps act on the target window. It starts as the main window: the one whose title contains --title,
or else the largest window of the process when it first shows up. "window:<text>" makes the target
the visible top-level window of the same process whose title contains the text, waiting up to
--timeout for it to appear; "window:main" goes back to the main window; "windows" lists the visible
top-level windows of the process. The target is looked up again for every step, so a step on a
window that has closed fails with a message and a non-zero exit. With --launch, every window of the
program is closed at the end (a mirror window left open keeps the program running).

Steps click by posting mouse messages to the window, at pixel positions of the screenshots (client
coordinates with --client); the user's mouse pointer never moves. "lower" puts the window behind all
others without activating it, so it stays out of the user's way; screenshots and taps on TextButton
still work there, but a MouseArea drops clicks while another window covers the click point. Qt
raises a window that enters full screen to the top (without activating it), so follow F11 with
"lower". "hover" posts mouse moves without buttons, which Qt delivers as hover; but on entering a
window Qt asks Windows to report the pointer leaving it (TrackMouseEvent), and as the real pointer
is elsewhere Windows reports that at once (WM_MOUSELEAVE), so the hover lasts only a moment: enough
for a handler that reacts to the hover starting, not for one that waits for the pointer to stay.
"leave" posts WM_MOUSELEAVE, which Qt acts on only while it believes the pointer is in the window.
"wheel:x,y,notches" posts a mouse move to the point and then that many wheel notches there,
positive to scroll down and negative to scroll up. "drag:x1,y1,x2,y2" posts a left press at the
first point, eight moves with the button held and the release at the second point, all at once.
"press:x,y", "hold:x,y" and "release:x,y" post the same three parts as separate steps, so a
"shot" between them captures what shows only while the button is down.
"dblclick:x,y" posts a press, a release, a double-click press and a release at once, as Windows
does for a real double click; two "click" steps in a row are not taken as a double click.
"type" and "key" post key messages to the window alone, for its focused item; the user's keyboard
and foreground window are not involved. In a native dialog (a file or folder picker) they go to its
focused control, such as the file name box. A window that was never activated has no focused item, so a
click on a text field does not give it the keyboard; "focus" posts WM_SETFOCUS, after which Qt treats
the window as focused (Windows' own keyboard focus and foreground window stay where they are). Qt
takes the next character message in the queue as the text of each key press, so a "key" step eats
the characters of a "type" step queued right after it: put a "wait" between them. A plain posted key holds no modifier
(the thread's key state stays as it is); "key:Ctrl+V" or "key:Ctrl+Shift+Z" attaches to the window's thread
(AttachThreadInput), marks Ctrl / Shift down in that shared key state alone, posts the key and puts the state back,
so neither the user's keyboard nor other programs see the modifier. Alt chords still need a binding to a plain key;
"key:Alt" presses and releases Alt by itself (WM_SYSKEYDOWN, WM_SYSKEYUP).
"""

import argparse
import ctypes
import struct
import subprocess
import sys
import time
import zlib
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
dwmapi = ctypes.WinDLL("dwmapi")

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.IsWindow.argtypes = [wintypes.HWND]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ScreenToClient.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
user32.MapVirtualKeyW.restype = wintypes.UINT
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.WindowFromPoint.argtypes = [wintypes.POINT]
user32.WindowFromPoint.restype = wintypes.HWND
user32.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
user32.GetAncestor.restype = wintypes.HWND
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, wintypes.UINT]
dwmapi.DwmGetWindowAttribute.argtypes = [wintypes.HWND, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
kernel32.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenThread.restype = wintypes.HANDLE
kernel32.SuspendThread.argtypes = [wintypes.HANDLE]
kernel32.SuspendThread.restype = wintypes.DWORD
kernel32.ResumeThread.argtypes = [wintypes.HANDLE]
kernel32.ResumeThread.restype = wintypes.DWORD
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("flags", wintypes.DWORD), ("hwndActive", wintypes.HWND),
                ("hwndFocus", wintypes.HWND), ("hwndCapture", wintypes.HWND), ("hwndMenuOwner", wintypes.HWND),
                ("hwndMoveSize", wintypes.HWND), ("hwndCaret", wintypes.HWND), ("rcCaret", wintypes.RECT)]


user32.GetGUIThreadInfo.argtypes = [wintypes.DWORD, ctypes.POINTER(GUITHREADINFO)]
user32.IsChild.argtypes = [wintypes.HWND, wintypes.HWND]
user32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
user32.GetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
user32.SetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
user32.PeekMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT,
                                wintypes.UINT]
kernel32.GetCurrentThreadId.restype = wintypes.DWORD

PW_RENDERFULLCONTENT = 0x2
DWMWA_EXTENDED_FRAME_BOUNDS = 9
DIB_RGB_COLORS = 0
BI_RGB = 0
WM_SETFOCUS = 0x0007
WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_LBUTTONDBLCLK = 0x0203
WM_RBUTTONDOWN = 0x0204
WM_RBUTTONUP = 0x0205
WM_MOUSELEAVE = 0x02A3
WM_MOUSEWHEEL = 0x020A
WHEEL_DELTA = 120
MK_LBUTTON = 0x0001
MK_RBUTTON = 0x0002
KEY_EXTENDED = 1 << 24
KEY_ALT_DOWN = 1 << 29
KEY_WAS_DOWN = 1 << 30
KEY_RELEASED = 1 << 31
THREAD_SUSPEND_RESUME = 0x0002
GA_ROOT = 2
HWND_BOTTOM = 1
SWP_NOSIZE = 0x0001
SWP_NOMOVE = 0x0002
SWP_NOZORDER = 0x0004
SWP_NOACTIVATE = 0x0010
MAPVK_VK_TO_VSC = 0
VK_MENU = 0x12
VIRTUAL_KEYS = {"Backspace": 0x08, "Tab": 0x09, "Return": 0x0D, "Escape": 0x1B, "Space": 0x20,
                "PageUp": 0x21, "PageDown": 0x22, "End": 0x23, "Home": 0x24,
                "Left": 0x25, "Up": 0x26, "Right": 0x27, "Down": 0x28, "Delete": 0x2E, "Alt": VK_MENU,
                "Backquote": 0xC0,
                **{f"F{n}": 0x6F + n for n in range(1, 13)},
                **{chr(code): code for code in range(ord("A"), ord("Z") + 1)},
                **{str(digit): 0x30 + digit for digit in range(10)}}
MODIFIER_KEYS = {"Ctrl": (0x11, 0xA2), "Shift": (0x10, 0xA0)}
EXTENDED_KEYS = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2E}
PLAIN_STEPS = {"leave", "lower", "rect", "close", "windows", "type", "focus"}


class StepError(Exception):
    pass


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


def owned_by(hwnd: int, pid: int) -> bool:
    owner = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
    return owner.value == pid


def visible_windows_of(pid: int) -> list[int]:
    found = []

    @WNDENUMPROC
    def collect(hwnd, _):
        if hwnd and owned_by(hwnd, pid) and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(collect, 0)
    return found


def title_of(hwnd: int) -> str:
    buffer = ctypes.create_unicode_buffer(512)
    user32.GetWindowTextW(hwnd, buffer, len(buffer))
    return buffer.value


def titled(windows: list[int], text: str) -> int | None:
    return next((hwnd for hwnd in windows if text in title_of(hwnd)), None)


def largest(windows: list[int]) -> int | None:
    def area(hwnd):
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        return (rect.right - rect.left) * (rect.bottom - rect.top)
    return max(windows, key=area, default=None)


def frame_of(hwnd: int) -> wintypes.RECT:
    frame = wintypes.RECT()
    dwmapi.DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, ctypes.byref(frame), ctypes.sizeof(frame))
    return frame


def window_line(hwnd: int) -> str:
    frame = frame_of(hwnd)
    client = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(client))
    return (f'0x{hwnd:X} "{title_of(hwnd)}": frame {frame.left},{frame.top} '
            f'{frame.right - frame.left}x{frame.bottom - frame.top}, client {client.right}x{client.bottom}')


class Targets:
    def __init__(self, pid: int, process: subprocess.Popen | None):
        self.pid = pid
        self.process = process
        self.main: int | None = None
        self.title: str | None = None
        self.hwnd: int | None = None

    def ended(self) -> bool:
        return self.process is not None and self.process.poll() is not None

    def note(self) -> str:
        return f"; the program has exited with code {self.process.returncode}" if self.ended() else ""

    def matches(self, hwnd: int | None, title: str | None) -> bool:
        return (bool(hwnd) and bool(user32.IsWindow(hwnd)) and owned_by(hwnd, self.pid)
                and bool(user32.IsWindowVisible(hwnd)) and (title is None or title in title_of(hwnd)))

    def current(self) -> int:
        if self.matches(self.hwnd, self.title):
            return self.hwnd
        if self.title is None:
            raise StepError(f"the main window is closed or hidden{self.note()}")
        hwnd = titled(visible_windows_of(self.pid), self.title)
        if hwnd is None:
            raise StepError(f'no visible window whose title contains "{self.title}" is open now{self.note()}')
        self.hwnd = hwnd
        return hwnd

    def switch(self, text: str, timeout: float, settle: float) -> None:
        if text == "main":
            self.title, self.hwnd = None, self.main
        else:
            deadline = time.monotonic() + timeout
            hwnd = titled(visible_windows_of(self.pid), text)
            if hwnd is None:
                while hwnd is None:
                    if self.ended() or time.monotonic() >= deadline:
                        others = ", ".join(f'"{title_of(other)}"' for other in visible_windows_of(self.pid))
                        raise StepError(f'no visible window whose title contains "{text}" appeared within '
                                        f'{timeout:g} s{self.note()}; open windows: {others or "none"}')
                    time.sleep(0.1)
                    hwnd = titled(visible_windows_of(self.pid), text)
                time.sleep(settle)
            self.title, self.hwnd = text, hwnd
        print(f"target: {window_line(self.current())}")


def list_windows(targets: Targets) -> None:
    windows = visible_windows_of(targets.pid)
    print(f"{len(windows)} visible window(s) of process {targets.pid}:")
    for hwnd in windows:
        marks = "".join(mark for mark, hit in ((" [main]", hwnd == targets.main),
                                               (" [target]", hwnd == targets.hwnd)) if hit)
        print(f"  {window_line(hwnd)}{marks}")


def capture(hwnd: int, client_only: bool = False) -> tuple[int, int, bytes]:
    window = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    frame = frame_of(hwnd)
    if client_only:
        client = wintypes.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(client))
        origin = wintypes.POINT(0, 0)
        user32.ClientToScreen(hwnd, ctypes.byref(origin))
        frame = wintypes.RECT(origin.x, origin.y, origin.x + client.right, origin.y + client.bottom)
    width, height = window.right - window.left, window.bottom - window.top

    screen_dc = user32.GetDC(None)
    memory_dc = gdi32.CreateCompatibleDC(screen_dc)
    bitmap = gdi32.CreateCompatibleBitmap(screen_dc, width, height)
    previous = gdi32.SelectObject(memory_dc, bitmap)
    printed = user32.PrintWindow(hwnd, memory_dc, PW_RENDERFULLCONTENT)
    header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0)
    pixels = ctypes.create_string_buffer(width * height * 4)
    if printed:
        gdi32.GetDIBits(memory_dc, bitmap, 0, height, pixels, ctypes.byref(header), DIB_RGB_COLORS)
    gdi32.SelectObject(memory_dc, previous)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory_dc)
    user32.ReleaseDC(None, screen_dc)
    if not printed:
        raise StepError("PrintWindow failed")

    left, top = frame.left - window.left, frame.top - window.top
    crop_width, crop_height = frame.right - frame.left, frame.bottom - frame.top
    rows = []
    for y in range(top, top + crop_height):
        start = (y * width + left) * 4
        row = pixels.raw[start:start + crop_width * 4]
        rgb = bytearray(crop_width * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]
        rows.append(b"\x00" + bytes(rgb))
    return crop_width, crop_height, b"".join(rows)


def describe(hwnd: int) -> None:
    frame = frame_of(hwnd)
    window = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    print(f"rect: frame {frame.left},{frame.top} {frame.right - frame.left}x{frame.bottom - frame.top}, "
          f"window {window.left},{window.top} {window.right - window.left}x{window.bottom - window.top}")


def target_point(hwnd: int, x: int, y: int, client_only: bool) -> tuple[wintypes.POINT, wintypes.POINT]:
    if client_only:
        client = wintypes.POINT(x, y)
        screen = wintypes.POINT(x, y)
        user32.ClientToScreen(hwnd, ctypes.byref(screen))
    else:
        frame = frame_of(hwnd)
        screen = wintypes.POINT(frame.left + x, frame.top + y)
        client = wintypes.POINT(screen.x, screen.y)
        user32.ScreenToClient(hwnd, ctypes.byref(client))
    return screen, client


def position_of(x: int, y: int) -> int:
    return ((y & 0xFFFF) << 16) | (x & 0xFFFF)


def pointer_over(hwnd: int) -> bool:
    pointer = wintypes.POINT()
    user32.GetCursorPos(ctypes.byref(pointer))
    return user32.GetAncestor(user32.WindowFromPoint(pointer), GA_ROOT) == hwnd


def post_together(hwnd: int, messages: list[tuple[int, int, int]]) -> None:
    thread = kernel32.OpenThread(THREAD_SUSPEND_RESUME, False, user32.GetWindowThreadProcessId(hwnd, None))
    if not thread:
        raise ctypes.WinError(ctypes.get_last_error())
    if kernel32.SuspendThread(thread) == 0xFFFFFFFF:
        error = ctypes.WinError(ctypes.get_last_error())
        kernel32.CloseHandle(thread)
        raise error
    try:
        for message, wparam, lparam in messages:
            user32.PostMessageW(hwnd, message, wparam, lparam)
    finally:
        kernel32.ResumeThread(thread)
        kernel32.CloseHandle(thread)


def click(hwnd: int, x: int, y: int, client_only: bool) -> None:
    screen, client = target_point(hwnd, x, y, client_only)
    covered = user32.GetAncestor(user32.WindowFromPoint(screen), GA_ROOT) != hwnd
    print(f"click {x},{y}: point {'covered by another window' if covered else 'visible'}, "
          f"pointer {'over' if pointer_over(hwnd) else 'outside'} the window")
    position = position_of(client.x, client.y)
    post_together(hwnd, [(WM_MOUSEMOVE, 0, position), (WM_LBUTTONDOWN, MK_LBUTTON, position),
                         (WM_LBUTTONUP, 0, position)])


def double_click(hwnd: int, x: int, y: int, client_only: bool) -> None:
    _, client = target_point(hwnd, x, y, client_only)
    position = position_of(client.x, client.y)
    post_together(hwnd, [(WM_MOUSEMOVE, 0, position), (WM_LBUTTONDOWN, MK_LBUTTON, position),
                         (WM_LBUTTONUP, 0, position), (WM_LBUTTONDBLCLK, MK_LBUTTON, position),
                         (WM_LBUTTONUP, 0, position)])
    print(f"double click {x},{y}")


def right_click(hwnd: int, x: int, y: int, client_only: bool) -> None:
    _, client = target_point(hwnd, x, y, client_only)
    position = position_of(client.x, client.y)
    post_together(hwnd, [(WM_MOUSEMOVE, 0, position), (WM_RBUTTONDOWN, MK_RBUTTON, position),
                         (WM_RBUTTONUP, 0, position)])
    print(f"right click {x},{y}")


def mouse_part(hwnd: int, kind: str, x: int, y: int, client_only: bool) -> None:
    _, client = target_point(hwnd, x, y, client_only)
    position = position_of(client.x, client.y)
    parts = {"press": [(WM_MOUSEMOVE, 0, position), (WM_LBUTTONDOWN, MK_LBUTTON, position)],
             "hold": [(WM_MOUSEMOVE, MK_LBUTTON, position)],
             "release": [(WM_LBUTTONUP, 0, position)]}
    post_together(hwnd, parts[kind])
    print(f"{kind} {x},{y}")


def hover(hwnd: int, x: int, y: int, client_only: bool) -> None:
    _, client = target_point(hwnd, x, y, client_only)
    beside = client.x - 1 if client.x > 0 else client.x + 1
    inside = pointer_over(hwnd)
    post_together(hwnd, [(WM_MOUSEMOVE, 0, position_of(beside, client.y)),
                         (WM_MOUSEMOVE, 0, position_of(client.x, client.y))])
    print(f"hover {x},{y}: pointer " + ("over the window, its next move takes over" if inside
                                        else "outside the window, so the hover ends at once"))


def drag(hwnd: int, x1: int, y1: int, x2: int, y2: int, client_only: bool) -> None:
    _, start = target_point(hwnd, x1, y1, client_only)
    _, end = target_point(hwnd, x2, y2, client_only)
    steps = 8
    messages = [(WM_MOUSEMOVE, 0, position_of(start.x, start.y)),
                (WM_LBUTTONDOWN, MK_LBUTTON, position_of(start.x, start.y))]
    for step in range(1, steps + 1):
        x = start.x + (end.x - start.x) * step // steps
        y = start.y + (end.y - start.y) * step // steps
        messages.append((WM_MOUSEMOVE, MK_LBUTTON, position_of(x, y)))
    messages.append((WM_LBUTTONUP, 0, position_of(end.x, end.y)))
    post_together(hwnd, messages)
    print(f"drag {x1},{y1} to {x2},{y2}")


def wheel(hwnd: int, x: int, y: int, notches: int, client_only: bool) -> None:
    screen, client = target_point(hwnd, x, y, client_only)
    delta = (-WHEEL_DELTA * notches) & 0xFFFF
    post_together(hwnd, [(WM_MOUSEMOVE, 0, position_of(client.x, client.y)),
                         (WM_MOUSEWHEEL, delta << 16, position_of(screen.x, screen.y))])
    print(f"wheel {x},{y}: {notches} notch(es) {'down' if notches > 0 else 'up'}")


def focus_in(hwnd: int) -> int:
    info = GUITHREADINFO()
    info.cbSize = ctypes.sizeof(GUITHREADINFO)
    thread = user32.GetWindowThreadProcessId(hwnd, None)
    if user32.GetGUIThreadInfo(thread, ctypes.byref(info)) and info.hwndFocus and user32.IsChild(hwnd, info.hwndFocus):
        return info.hwndFocus
    return hwnd


def type_text(hwnd: int, text: str) -> None:
    target = focus_in(hwnd)
    for character in text:
        user32.PostMessageW(target, WM_CHAR, ord(character), 1)
    print(f"typed {len(text)} character(s)" + (" into the focused control" if target != hwnd else ""))


def press_key(hwnd: int, name: str) -> None:
    hwnd = focus_in(hwnd)
    virtual = VIRTUAL_KEYS[name]
    flags = 1 | (user32.MapVirtualKeyW(virtual, MAPVK_VK_TO_VSC) << 16) | (KEY_EXTENDED if virtual in EXTENDED_KEYS else 0)
    if virtual == VK_MENU:
        user32.PostMessageW(hwnd, WM_SYSKEYDOWN, virtual, flags | KEY_ALT_DOWN)
        user32.PostMessageW(hwnd, WM_SYSKEYUP, virtual, flags | KEY_WAS_DOWN | KEY_RELEASED)
    else:
        user32.PostMessageW(hwnd, WM_KEYDOWN, virtual, flags)
        user32.PostMessageW(hwnd, WM_KEYUP, virtual, flags | KEY_WAS_DOWN | KEY_RELEASED)
    print(f"pressed {name}")


def split_key(value: str) -> tuple[list[str], str]:
    *modifiers, name = value.split("+")
    return modifiers, name


def press_chord(hwnd: int, modifiers: list[str], name: str) -> None:
    thread = user32.GetWindowThreadProcessId(hwnd, None)
    own = kernel32.GetCurrentThreadId()
    message = wintypes.MSG()
    user32.PeekMessageW(ctypes.byref(message), None, 0, 0, 0)
    if not user32.AttachThreadInput(own, thread, True):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        state = (ctypes.c_ubyte * 256)()
        user32.GetKeyboardState(state)
        saved = bytes(state)
        for modifier in modifiers:
            for virtual in MODIFIER_KEYS[modifier]:
                state[virtual] |= 0x80
        user32.SetKeyboardState(state)
        try:
            press_key(hwnd, name)
            time.sleep(0.8)
        finally:
            user32.SetKeyboardState((ctypes.c_ubyte * 256).from_buffer_copy(saved))
    finally:
        user32.AttachThreadInput(own, thread, False)
    print(f"held {'+'.join(modifiers)} in that window's thread only")


def write_png(path: str, width: int, height: int, rows: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as png:
        png.write(b"\x89PNG\r\n\x1a\n")
        png.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        png.write(chunk(b"IDAT", zlib.compress(rows, 6)))
        png.write(chunk(b"IEND", b""))


def numbers(value: str, count: int, kind: type = int) -> list:
    try:
        parsed = [kind(part) for part in value.split(",")]
    except ValueError:
        parsed = []
    if len(parsed) != count:
        raise StepError(f'expects {count} comma-separated number(s), got "{value}"')
    return parsed


def check(step: str) -> None:
    kind, _, value = step.partition(":")
    if kind == "wait":
        numbers(value, 1, float)
    elif kind in ("click", "dblclick", "rclick", "hover", "press", "hold", "release"):
        numbers(value, 2)
    elif kind == "wheel":
        numbers(value, 3)
    elif kind in ("move", "drag"):
        numbers(value, 4)
    elif kind == "key" and (split_key(value)[1] not in VIRTUAL_KEYS
                            or any(modifier not in MODIFIER_KEYS for modifier in split_key(value)[0])):
        raise StepError(f"unknown key; one of {', '.join(VIRTUAL_KEYS)}, optionally after Ctrl+ or Shift+")
    elif kind in ("shot", "window") and not value.strip():
        raise StepError("needs a file name" if kind == "shot" else "needs title text, or main")
    elif kind not in PLAIN_STEPS | {"key", "shot", "window", "wheel", "drag", "dblclick", "rclick", "press", "hold",
                                    "release"}:
        raise StepError("unknown step")


def perform(step: str, targets: Targets, args: argparse.Namespace) -> None:
    kind, _, value = step.partition(":")
    if kind == "wait":
        time.sleep(numbers(value, 1, float)[0])
        return
    if kind == "window":
        targets.switch(value.strip(), args.timeout, args.settle)
        return
    if kind == "windows":
        list_windows(targets)
        return
    hwnd = targets.current()
    if kind == "shot":
        width, height, rows = capture(hwnd, args.client)
        write_png(value, width, height, rows)
        print(f"{value}: {width}x{height}")
    elif kind == "click":
        click(hwnd, *numbers(value, 2), args.client)
    elif kind == "dblclick":
        double_click(hwnd, *numbers(value, 2), args.client)
    elif kind == "rclick":
        right_click(hwnd, *numbers(value, 2), args.client)
    elif kind == "hover":
        hover(hwnd, *numbers(value, 2), args.client)
    elif kind == "wheel":
        wheel(hwnd, *numbers(value, 3), args.client)
    elif kind == "drag":
        drag(hwnd, *numbers(value, 4), args.client)
    elif kind in ("press", "hold", "release"):
        mouse_part(hwnd, kind, *numbers(value, 2), args.client)
    elif kind == "leave":
        user32.PostMessageW(hwnd, WM_MOUSELEAVE, 0, 0)
        print("posted WM_MOUSELEAVE")
    elif kind == "type":
        type_text(hwnd, value)
    elif kind == "key":
        modifiers, name = split_key(value)
        if modifiers:
            press_chord(hwnd, modifiers, name)
        else:
            press_key(hwnd, name)
    elif kind == "lower":
        user32.SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)
    elif kind == "rect":
        describe(hwnd)
    elif kind == "move":
        x, y, width, height = numbers(value, 4)
        user32.SetWindowPos(hwnd, None, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE)
    elif kind == "close":
        user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    elif kind == "focus":
        user32.PostMessageW(hwnd, WM_SETFOCUS, 0, 0)
        print("posted WM_SETFOCUS")


def run(steps: list[str], targets: Targets, args: argparse.Namespace) -> int:
    deadline = time.monotonic() + args.timeout
    while targets.main is None and not targets.ended() and time.monotonic() < deadline:
        windows = visible_windows_of(targets.pid)
        targets.main = titled(windows, args.title) if args.title else largest(windows)
        if targets.main is None:
            time.sleep(0.1)
    if targets.main is None:
        print(f"no visible window{targets.note()}", file=sys.stderr)
        return 1
    targets.hwnd = targets.main
    time.sleep(args.settle)
    for step in steps:
        try:
            perform(step, targets, args)
        except StepError as error:
            print(f"{step}: {error}", file=sys.stderr)
            return 1
    return 0


def shut_down(process: subprocess.Popen, main: int | None) -> bool:
    if process.poll() is None:
        for hwnd in sorted(visible_windows_of(process.pid), key=lambda hwnd: hwnd == main):
            user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("the program was still running 10 s after its windows were closed; ended it", file=sys.stderr)
            return False
    if process.returncode != 0:
        print(f"the program exited with code {process.returncode}", file=sys.stderr)
        return False
    return True


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--launch", help="start this executable, drive its windows, then close them all")
    target.add_argument("--pid", type=int, help="drive the windows of this running process")
    parser.add_argument("--arg", action="append", default=[],
                        help="one argument for the --launch executable, spaces included, repeatable; "
                             "write --arg=--gallery")
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--out", help="capture the main window once into this PNG file")
    output.add_argument("--steps", help='";"-separated steps on the target window: wait:<seconds>, shot:<png>, '
                                        'click:<x>,<y>, rclick:<x>,<y> (right button), '
                                        'hover:<x>,<y> (mouse moves without buttons), '
                                        'leave (WM_MOUSELEAVE), type:<text> (into the focused item), '
                                        f'key:<{"|".join(VIRTUAL_KEYS)}> (pressed and released), focus (Qt treats '
                                        'the window as focused; Windows focus unchanged), lower, close, '
                                        'rect (print the window rectangles), move:<x>,<y>,<width>,<height> '
                                        '(the window rectangle, physical pixels; does not activate it), '
                                        'window:<title text> or window:main (the target of the steps after it), '
                                        'windows (list the visible windows of the process)')
    parser.add_argument("--title", help="the main window is the one whose title contains this text, "
                                        "instead of the largest one")
    parser.add_argument("--client", action="store_true",
                        help="capture the client area only, without the frame; click and hover take client coordinates")
    parser.add_argument("--settle", type=float, default=1.5,
                        help="seconds to let a window that has just appeared render before the next step")
    parser.add_argument("--timeout", type=float, default=20.0,
                        help="seconds to wait for the main window, and for each window:<title text>")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    sys.stdout.reconfigure(errors="backslashreplace")
    steps = [f"shot:{args.out}"] if args.out else [s.strip() for s in args.steps.split(";") if s.strip()]
    for step in steps:
        try:
            check(step)
        except StepError as error:
            print(f"{step}: {error}", file=sys.stderr)
            return 1
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    process = subprocess.Popen([args.launch, *args.arg]) if args.launch else None
    targets = Targets(process.pid if process else args.pid, process)
    status = 1
    try:
        status = run(steps, targets, args)
    finally:
        if process is not None and not shut_down(process, targets.main):
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
