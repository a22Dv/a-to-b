import ctypes
import numpy as np

from numba import njit, prange
from numpy.typing import NDArray
from pathlib import Path

OVERLAY_DLL_LOC: Path = Path(__file__).parent / "dlls" / "overlay_window.dll"
OVERLAY_DLL: ctypes.CDLL = ctypes.CDLL(OVERLAY_DLL_LOC)

# HRESULT create_overlay(Overlay **objptr, int x, int y, int sx, int sy)
OVERLAY_DLL.create_overlay.argtypes = (
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
)
OVERLAY_DLL.create_overlay.restype = ctypes.c_long

# void destroy_overlay(Overlay **objptr)
OVERLAY_DLL.destroy_overlay.argtypes = (ctypes.c_void_p,)
OVERLAY_DLL.destroy_overlay.restype = None

# HRESULT update_overlay(Overlay *obj, void *data, int dx, int dy, int dz)
OVERLAY_DLL.update_overlay.argtypes = (
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
)
OVERLAY_DLL.update_overlay.restype = ctypes.c_long

# HRESULT reposition_overlay(Overlay *obj, int x, int y)
OVERLAY_DLL.reposition_overlay.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
OVERLAY_DLL.reposition_overlay.restype = ctypes.c_long

# HRESULT resize_overlay(Overlay *obj, int sx, int sy)
OVERLAY_DLL.resize_overlay.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
OVERLAY_DLL.resize_overlay.restype = ctypes.c_long

CHANNEL_COUNT_BGRA: int = 4


class Overlay:
    """
    Wrapper around custom Overlay DLL. Uses a DXGI Flip-Sequential, DirectComposition backend.
    - Does not respond to scales other than 100%.
    - A `DXGI_ERROR_LOST` will result in undefined behavior (UAC prompt, Ctrl+Alt+Del, etc.)
    - Baseline performance (No .append(), direct .update() every iteration) ~0.3ms per (1280x720) frame.
    - Memory pressure / GPU usage heavily affects performance.
    - Tested under: (Windows 11, Ryzen 7 8845HS / Radeon 780M, LPDDR5X-7500, 1080P60)
    """

    _handle: ctypes.c_void_p = ctypes.c_void_p()
    _buffer_u8: NDArray = np.zeros((), dtype=np.uint8)
    _cx: int = 0
    _cy: int = 0
    _csx: int = 0
    _csy: int = 0

    def __init__(self, x=0, y=0, sx=854, sy=480) -> None:
        hr: int = OVERLAY_DLL.create_overlay(ctypes.byref(self._handle), x, y, sx, sy)
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")
        self._cx = x
        self._cy = y
        self._csx = sx
        self._csy = sy
        self._buffer_u8 = np.zeros((sy, sx, CHANNEL_COUNT_BGRA), dtype=np.uint8)

    def append(self, frame: NDArray, nx=0, ny=0, minmax=False, overwrite=False) -> None:
        """
        Appends the given frame onto the specified x,y (top-left) coordinate of the
        buffer. Enabling minmax allows for greater performance by picking the frame
        color when alpha > 0. Useful if all you need is transparency for the window
        itself but not for each individual element. (Also known as 1-bit alpha).
        Setting overwrite will do a direct copy operation and overwrite the pixels resulting
        in the highest performance. Note that when overwrite and minmax are both set, minmax
        will be prioritized.
        """
        fbx, fby = max(-nx, 0), max(-ny, 0)
        bbx, bby = max(nx, 0), max(ny, 0)
        fw: int = min(frame.shape[1] - fbx, self._buffer_u8.shape[1] - bbx)
        fh: int = min(frame.shape[0] - fby, self._buffer_u8.shape[0] - bby)
        if fw <= 0 or fh <= 0:
            return
        fex, fey = fbx + fw, fby + fh
        bex, bey = bbx + fw, bby + fh

        fsection: NDArray = frame[fby:fey, fbx:fex, :]
        bsection: NDArray = self._buffer_u8[bby:bey, bbx:bex, :]

        match minmax | (overwrite << 1):
            case 0:
                self._alpha_blend_bgra(fsection, bsection)
            case 1 | 3:
                self._minmax_bgra(fsection, bsection)
            case 2:
                bsection[:] = fsection

    def update(self) -> None:
        """
        Uses the frame that has been appended internally
        and updates the display.
        """
        hr: int = OVERLAY_DLL.update_overlay(
            self._handle,
            self._buffer_u8.ctypes.data_as(ctypes.c_void_p),
            self._csx,
            self._csy,
            CHANNEL_COUNT_BGRA,
        )
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")

    def resize(self, nx: int, ny: int) -> None:
        """
        Resizes the overlay accordingly.
        """
        hr: int = OVERLAY_DLL.resize_overlay(self._handle, nx, ny)
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")
        self._buffer_u8 = np.zeros((ny, nx, CHANNEL_COUNT_BGRA), dtype=np.uint8)
        self._csx = nx
        self._csy = ny

    def reposition(self, nx: int, ny: int) -> None:
        """
        Repositions the overlay window accordingly.
        """
        hr: int = OVERLAY_DLL.reposition_overlay(self._handle, nx, ny)
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")
        self._cx = nx
        self._cy = ny

    def clear(self) -> None:
        """
        Clears the entire buffer to full transparency.
        """
        self._buffer_u8[:] = 0

    @staticmethod
    @njit(parallel=True, cache=True, fastmath=True, boundscheck=False)
    def _minmax_bgra(frame_section: NDArray, buffer_section: NDArray) -> None:
        h, w, _ = frame_section.shape
        for i in prange(h):
            for j in range(w):
                a: int = frame_section[i, j, 3]
                if a > 0:
                    for k in range(3):
                        buffer_section[i, j, k] = frame_section[i, j, k]
                    buffer_section[i, j, 3] = 255

    @staticmethod
    @njit(parallel=True, cache=True, fastmath=True, boundscheck=False)
    def _alpha_blend_bgra(frame_section: NDArray, buffer_section: NDArray) -> None:
        h, w, _ = frame_section.shape
        for i in prange(h):
            for j in range(w):
                af: int = frame_section[i, j, 3]
                if not af:
                    continue
                elif af == 255:
                    for k in range(4):
                        buffer_section[i, j, k] = frame_section[i, j, k]
                    continue
                inv_a: int = 255 - af
                ab: int = buffer_section[i, j, 3]
                for k in range(3):
                    cf: int = frame_section[i, j, k]
                    cb: int = buffer_section[i, j, k]
                    buffer_section[i, j, k] = (cf * af + cb * inv_a) // 255
                buffer_section[i, j, 3] = (af * 255 + ab * inv_a) // 255

    def __del__(self) -> None:
        OVERLAY_DLL.destroy_overlay(ctypes.byref(self._handle))
