import abc
import cv2
import numpy as np
import win32gui as w32g
import win32con as w32c
from numpy.typing import NDArray
from typing import ClassVar


class OverlayWindow(abc.ABC):
    window_n: ClassVar[int] = 0
    buffer: NDArray = np.zeros((), dtype=np.uint8)
    wname: str = ""
    hwnd: int = 0

    def __init__(self) -> None:
        """
        Must be called before any other initialization setup via
        super.__init__().
        """
        self.wname = f"WINDOW_{OverlayWindow.window_n}"
        cv2.namedWindow(self.wname)
        self.hwnd = w32g.FindWindow(None, self.wname)
        if not self.hwnd:
            raise SystemError()
        OverlayWindow.window_n += 1
        cv2.imshow(self.wname, self.buffer)
        cv2.waitKey(1)

    @abc.abstractmethod
    def update(self) -> None:
        """
        Update the window and show it to the screen.
        """
        pass

    @abc.abstractmethod
    def append(self, frame: NDArray, x=0, y=0) -> None:
        """
        Appends a given frame starting at an x/y offset from the top-left (0,0).
        Note that if the frame cannot fit, it is sliced to fit in the frame.
        """
        pass

    def _apply_default_style(self) -> None:
        """
        Applies the default overlay style.
        Call after super.__init__() if class
        is inherited from.
        """
        w32g.SetWindowLong(self.hwnd, w32c.GWL_STYLE, w32c.WS_POPUP | w32c.WS_VISIBLE)
        w32g.SetWindowLong(
            self.hwnd,
            w32c.GWL_EXSTYLE,
            w32c.WS_EX_TRANSPARENT
            | w32c.WS_EX_NOACTIVATE
            | w32c.WS_EX_TOPMOST
            | w32c.WS_EX_LAYERED,
        )
        w32g.SetWindowPos(  # Applies the settings immediately. Removing this leaves a gray "deadzone".
            self.hwnd, w32c.HWND_TOPMOST, 0, 0, 0, 0, w32c.SWP_NOMOVE | w32c.SWP_NOSIZE
        )
        w32g.SetLayeredWindowAttributes(self.hwnd, 0, 0, w32c.LWA_COLORKEY)

    def __del__(self) -> None:
        try:
            # window_n is not decreased to prevent name collisions under reuse.
            cv2.destroyWindow(self.wname)
        except:
            pass
