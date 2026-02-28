import ctypes
import numpy as np

from win32con import SM_CXSCREEN, SM_CYSCREEN
from win32api import GetSystemMetrics
from numpy.typing import NDArray
from pathlib import Path


SCRDLL_PATH: Path = Path(__file__).parent / "dlls" / "screen_capture.dll"
SCRDLL: ctypes.CDLL = ctypes.CDLL(SCRDLL_PATH)

# --- API Definition --- #

# HRESULT create_screen_capture_object(ScreenCapture**)
SCRDLL.create_screen_capture_object.restype = ctypes.c_long
SCRDLL.create_screen_capture_object.argtypes = (ctypes.c_void_p,)

# HRESULT capture_frame(ScreenCapture*, void*, int, int, int)
SCRDLL.capture_frame.restype = ctypes.c_long
SCRDLL.capture_frame.argtypes = (
    ctypes.c_void_p,  # Class pointer.
    ctypes.c_void_p,  # Data pointer.
    ctypes.c_int,  # Width.
    ctypes.c_int,  # Height.
    ctypes.c_int,  # Depth / Channels (Must be 4 for BGRA).
)

# void destroy_screen_capture_object(ScreenCapture**)
SCRDLL.destroy_screen_capture_object.restype = None
SCRDLL.destroy_screen_capture_object.argtypes = (ctypes.c_void_p,)

N_CHANNELS: int = 4


class ScreenCapture:
    """
    Wrapper around a custom screen-capture DLL with a DXGI Desktop-Duplication backend.
    - This does not support monitors with scaling other than 100%. 
    - A `DXGI_ERROR_LOST` will result in undefined behavior (UAC prompt, Ctrl+Alt+Del, etc.)
    - Performance differs heavily under memory pressure.
      Expect the delay for each call to reach ~5ms at ~90% memory usage,
      and beyond that if page-swapping occurs. (>20ms)
    - Average-case capture performance, ~2ms from GPU to return to caller.
    - Tested under: (Windows 11, Ryzen 7 8845HS, LPDDR5X-7500, 1080P60).
    """
    _class_handle: ctypes.c_void_p = ctypes.c_void_p()
    _buffer: NDArray = np.zeros((), dtype=np.uint8)
    _sx: int = 0
    _sy: int = 0

    def __init__(self) -> None:
        hr: int = SCRDLL.create_screen_capture_object(ctypes.byref(self._class_handle))
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")
        self._sx = GetSystemMetrics(SM_CXSCREEN)
        self._sy = GetSystemMetrics(SM_CYSCREEN)
        self._buffer = np.resize(self._buffer, (self._sy, self._sx, N_CHANNELS))
        self._buffer.flags.writeable = False

    def capture(self) -> NDArray:
        """
        Captures a frame and returns a read-only reference to its
        internal buffer.
        """
        hr: int = SCRDLL.capture_frame(
            self._class_handle,
            self._buffer.ctypes.data_as(ctypes.c_void_p),
            self._sx,
            self._sy,
            N_CHANNELS
        )
        if hr < 0 and hr != 0x887a0027: # DXGI_ERROR_WAIT_TIMEOUT
            raise SystemError(f"HRESULT: {hr:#x}")
        return self._buffer

    def __del__(self) -> None:
        SCRDLL.destroy_screen_capture_object(ctypes.byref(self._class_handle))
