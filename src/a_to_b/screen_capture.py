import ctypes
import numpy as np
from winerror import S_OK
from pathlib import Path
from numpy.typing import NDArray

SCREEN_CAPTURE_DLL_LOC: Path = Path(__file__).parent / "dlls" / "screen_capture.dll"
SCREEN_CAPTURE_DLL: ctypes.CDLL = ctypes.CDLL(SCREEN_CAPTURE_DLL_LOC)

SCREEN_CAPTURE_DLL.capture_state_create.argtypes = ()
SCREEN_CAPTURE_DLL.capture_state_create.restype = ctypes.c_void_p
SCREEN_CAPTURE_DLL.capture_state_destroy.argtypes = (ctypes.c_void_p,)
SCREEN_CAPTURE_DLL.capture_state_destroy.restype = None
SCREEN_CAPTURE_DLL.capture_state_height.argtypes = (ctypes.c_void_p,)
SCREEN_CAPTURE_DLL.capture_state_height.restype = ctypes.c_int
SCREEN_CAPTURE_DLL.capture_state_width.argtypes = (ctypes.c_void_p,)
SCREEN_CAPTURE_DLL.capture_state_width.restype = ctypes.c_int
SCREEN_CAPTURE_DLL.capture_state_get_frame.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
SCREEN_CAPTURE_DLL.capture_state_get_frame.restype = ctypes.c_long
FRAME_CHANNEL_COUNT: int = 4


class ScreenCapture:
    _frame: NDArray = np.zeros((), dtype=np.uint8)
    _fctvp: ctypes.c_void_p = ctypes.c_void_p()
    _height: int = 0
    _width: int = 0
    _handle: ctypes.c_void_p = ctypes.c_void_p()

    def __init__(self) -> None:
        self._handle = SCREEN_CAPTURE_DLL.capture_state_create()
        self._height = SCREEN_CAPTURE_DLL.capture_state_height(self._handle)
        self._width = SCREEN_CAPTURE_DLL.capture_state_width(self._handle)
        self._frame = np.zeros(
            (self._height, self._width, FRAME_CHANNEL_COUNT), dtype=np.uint8
        )
        self._fctvp = self._frame.ctypes.data_as(ctypes.c_void_p)
        hr: ctypes.c_long = SCREEN_CAPTURE_DLL.capture_state_get_frame(
            self._handle, self._fctvp
        )
        if hr != S_OK:
            raise SystemError()
        self._frame.flags.writeable = False

    def capture_frame(self) -> NDArray:
        hr: int = SCREEN_CAPTURE_DLL.capture_state_get_frame(
            self._handle, self._frame.ctypes.data
        )
        if hr != S_OK:
            raise SystemError()
        return self._frame

    def __del__(self) -> None:
        SCREEN_CAPTURE_DLL.capture_state_destroy(self._handle)
