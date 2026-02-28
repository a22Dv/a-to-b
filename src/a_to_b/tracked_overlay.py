from a_to_b.overlay_window import Overlay
from typing import Tuple
from win32gui import FindWindow, GetWindowRect, IsWindow
from numpy.typing import NDArray

type Box = Tuple[int, int, int, int]


class TrackedOverlay:
    _window_name: str = ""
    _hwnd: int = 0
    _overlay_window: Overlay | None = None
    _cx: int = 0
    _cy: int = 0
    _cw: int = 0
    _ch: int = 0

    def __init__(self, window_name: str) -> None:
        x, y, w, h = self._find_game_window(window_name)
        if not any((x, y, w, h)):
            self._overlay_window = Overlay()  # Spawns default overlay at (0,0) 854x480.
            self._cx = 0
            self._cy = 0
            self._cw = 854
            self._ch = 480
        else:
            self._overlay_window = Overlay(x, y, w, h)
            self._cx = x
            self._cy = y
            self._cw = w
            self._ch = h
        self._window_name = window_name

    def track(self) -> Tuple[Box, bool, bool]:
        """
        Tracks the game window and applies the overlay.
        Returns (dimensions, has_changed, has_been_found).
        """
        changed: bool = False
        x, y, w, h = self._find_game_window(self._window_name)
        if not any((x, y, w, h)):
            changed = any((self._cx, self._cy, self._cw, self._ch))
            return ((0, 0, 0, 0), changed, False)

        if not (x == self._cx and y == self._cy):
            self._overlay.reposition(x, y)
            self._cx = x
            self._cy = y
            changed = True

        if not (w == self._cw and h == self._ch):
            self._overlay.resize(w, h)
            self._cw = w
            self._ch = h
            changed = True

        return ((x, y, w, h), changed, True)

    @property
    def _overlay(self) -> Overlay:
        if self._overlay_window is None:
            raise RuntimeError()
        return self._overlay_window

    def _find_game_window(self, window_name: str) -> Box:
        if not self._hwnd:
            self._hwnd = FindWindow(None, window_name)

        # Manual alignment when window is on "Windowed" mode
        # might be required.
        hwnd: int = self._hwnd
        if IsWindow(hwnd):
            x1, y1, x2, y2 = GetWindowRect(hwnd)
            return (x1, y1, x2 - x1, y2 - y1)
        else:
            self._hwnd = 0
        return (0, 0, 0, 0)

    def append(self, frame: NDArray, nx=0, ny=0, minmax=False, overwrite=False) -> None:
        self._overlay.append(frame, nx, ny, minmax, overwrite)

    def update(self) -> None:
        self._overlay.update()
    
    def clear(self) -> None:
        self._overlay.clear()

