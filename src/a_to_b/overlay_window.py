import ctypes
import numpy as np
import numba

from numpy.typing import NDArray
from pathlib import Path

OVERLAY_DLL_LOC: Path = Path(__file__).parent / "dlls" / "overlay_window.dll"
OVERLAY_DLL: ctypes.CDLL = ctypes.CDLL(OVERLAY_DLL_LOC)

OVERLAY_DLL.create_overlay.argtypes = (
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
)
OVERLAY_DLL.create_overlay.restype = ctypes.c_long
OVERLAY_DLL.destroy_overlay.argtypes = (ctypes.c_void_p,)
OVERLAY_DLL.destroy_overlay.restype = None
OVERLAY_DLL.update_overlay.argtypes = (
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
)
OVERLAY_DLL.update_overlay.restype = ctypes.c_long
OVERLAY_DLL.reposition_overlay.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
OVERLAY_DLL.reposition_overlay.restype = ctypes.c_long
OVERLAY_DLL.resize_overlay.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
OVERLAY_DLL.resize_overlay.restype = ctypes.c_long

CHANNEL_COUNT_BGRA: int = 4


class Overlay:
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

    def append(self, frame: NDArray, nx=0, ny=0) -> None:
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
        self._alpha_blend_bgra(fsection, bsection)

    def update(self) -> None:
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
        hr: int = OVERLAY_DLL.resize_overlay(self._handle, nx, ny)
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")
        self._buffer_u8 = np.zeros((ny, nx, CHANNEL_COUNT_BGRA), dtype=np.uint8)
        self._csx = nx
        self._csy = ny

    def reposition(self, nx: int, ny: int) -> None:
        hr: int = OVERLAY_DLL.reposition_overlay(self._handle, nx, ny)
        if hr < 0:
            raise SystemError(f"HRESULT: {hr:#x}")
        self._cx = nx
        self._cy = ny

    @staticmethod
    @numba.njit(parallel=False, cache=True, fastmath=True, boundscheck=False)
    def _alpha_blend_bgra(frame_section: NDArray, buffer_section: NDArray) -> None:
        h, w, _ = frame_section.shape
        for i in range(h):
            for j in range(w):
                af = frame_section[i,j,3]
                if not af:
                    continue
                elif af == 255:
                    for k in range(4):
                        buffer_section[i,j,k] = frame_section[i,j,k]
                    continue
                inv_a = 255 - af
                ab = buffer_section[i,j,3]
                for k in range(3):
                    cf = frame_section[i,j,k]
                    cb = buffer_section[i,j,k]
                    buffer_section[i,j,k] = (cf * af + cb * inv_a) // 255
                buffer_section[i,j,3] = (af * 255 + ab * inv_a) // 255

    def __del__(self) -> None:
        OVERLAY_DLL.destroy_overlay(ctypes.byref(self._handle))
