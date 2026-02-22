import ctypes
import numpy as np
from abc import ABC
from pathlib import Path
from typing import Tuple
from numpy.typing import NDArray

_OVERLAY_WINDOW_DLL_LOC: Path = Path(__file__).parent / "dlls" / "overlay_window.dll"
_OVERLAY_WINDOW_DLL: ctypes.CDLL = ctypes.CDLL(_OVERLAY_WINDOW_DLL_LOC)

_OVERLAY_WINDOW_DLL.ovlw_wnclass_init.argtypes = ()
_OVERLAY_WINDOW_DLL.ovlw_wnclass_init.restype = None
_OVERLAY_WINDOW_DLL.ovlw_wnclass_uninit.argtypes = ()
_OVERLAY_WINDOW_DLL.ovlw_wnclass_uninit.restype = None
_OVERLAY_WINDOW_DLL.ovlw_create.argtypes = (
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
)
_OVERLAY_WINDOW_DLL.ovlw_create.restype = ctypes.c_void_p
_OVERLAY_WINDOW_DLL.ovlw_destroy.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_destroy.restype = None
_OVERLAY_WINDOW_DLL.ovlw_update.argtypes = (
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
)
_OVERLAY_WINDOW_DLL.ovlw_update.restype = ctypes.c_long
_OVERLAY_WINDOW_DLL.ovlw_set_position.argtypes = (
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
)
_OVERLAY_WINDOW_DLL.ovlw_set_position.restype = ctypes.c_long
_OVERLAY_WINDOW_DLL.ovlw_set_window_size.argtypes = (
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_int,
)
_OVERLAY_WINDOW_DLL.ovlw_set_window_size.restype = ctypes.c_long
_OVERLAY_WINDOW_DLL.ovlw_get_position_x.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_get_position_x.restype = ctypes.c_int
_OVERLAY_WINDOW_DLL.ovlw_get_position_y.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_get_position_y.restype = ctypes.c_int
_OVERLAY_WINDOW_DLL.ovlw_get_window_height.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_get_window_height.restype = ctypes.c_int
_OVERLAY_WINDOW_DLL.ovlw_get_window_width.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_get_window_width.restype = ctypes.c_int
_OVERLAY_WINDOW_DLL.ovlw_get_display_height.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_get_display_height.restype = ctypes.c_int
_OVERLAY_WINDOW_DLL.ovlw_get_display_width.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_get_display_width.restype = ctypes.c_int
_OVERLAY_WINDOW_DLL.ovlw_poll_messages.argtypes = (ctypes.c_void_p,)
_OVERLAY_WINDOW_DLL.ovlw_poll_messages.restype = None


OVERLAY_WINDOW_CHANNEL_COUNT: int = 4


class OverlayWindow:
    _handle: ctypes.c_void_p = ctypes.c_void_p()
    _buffer: NDArray = np.zeros((), dtype=np.uint8)
    _wx: int = 0
    _wy: int = 0
    _wsx: int = 0
    _wsy: int = 0

    def __init__(self, tx=0, ty=0, tsx=854, tsy=480) -> None:
        _OVERLAY_WINDOW_DLL.ovlw_wnclass_init()
        self._handle = _OVERLAY_WINDOW_DLL.ovlw_create(tx, ty, tsx, tsy)
        if self._handle:
            print(self._handle)
            raise SystemError()
        self._wx, self._wy, self._wsx, self._wsy = tx, ty, tsx, tsy
        self._buffer = np.zeros((ty, tx, OVERLAY_WINDOW_CHANNEL_COUNT), dtype=np.uint8)

    def set_size(self, sx: int, sy: int) -> None:
        hr: int = _OVERLAY_WINDOW_DLL.ovlw_set_size(self._handle, sx, sy)
        if hr:
            raise SystemError()
        self._wsx = sx
        self._wsy = sy
        self._buffer = np.resize(self._buffer, (sy, sx, OVERLAY_WINDOW_CHANNEL_COUNT))

    def append(self, frame: NDArray, cx: int, cy: int) -> None:
        fbx, fby = max(-cx, 0), max(-cy, 0)
        fex: int = min(frame.shape[1], self._buffer.shape[1] - cx)
        fey: int = min(frame.shape[0], self._buffer.shape[0] - cy)
        if fbx >= fex or fby >= fey:  # Out-of-bounds.
            return
        bbx, bby = max(cx, 0), max(cy, 0)
        bex: int = bbx + (fex - fbx)
        bey: int = bby + (fey - fby)

        frame_view: NDArray = frame[fby:fey, fbx:fex, :]
        buffer_view: NDArray = self._buffer[bby:bey, bbx:bex, :]

        norm_alpha: NDArray = (frame_view[:, :, 3] / 255.0)[..., None]
        inv_alpha: NDArray = 1.0 - norm_alpha
        foreground: NDArray = frame_view[:, :, :3] * norm_alpha
        background: NDArray = buffer_view[:, :, :3] * inv_alpha

        buffer_view[:, :, :3] = (background + foreground).astype(np.uint8)

    def update(self) -> None:
        _OVERLAY_WINDOW_DLL.ovlw_update(
            self._handle,
            self._buffer.ctypes.data_as(ctypes.c_void_p),
            self._buffer.shape[1],  # X
            self._buffer.shape[0],  # Y
            self._buffer.shape[2],  # Z (Channels)
        )

        # Call polling automatically.
        _OVERLAY_WINDOW_DLL.ovlw_poll_messages(self._handle)

    def set_position(self, x: int, y: int) -> None:
        hr: int = _OVERLAY_WINDOW_DLL.ovlw_set_position(self._handle, x, y)
        if hr:
            raise SystemError()
        self._wx = x
        self._wy = y

    def get_position(self) -> Tuple[int, int]:
        return (self._wx, self._wy)

    def get_size(self) -> Tuple[int, int]:
        return (self._wsx, self._wsy)

    def get_display_size(self) -> Tuple[int, int]:
        return (
            _OVERLAY_WINDOW_DLL.ovlw_get_display_width(self._handle),
            _OVERLAY_WINDOW_DLL.ovlw_get_display_height(self._handle),
        )

    def __del__(self) -> None:
        _OVERLAY_WINDOW_DLL.ovlw_destroy(self._handle)
        _OVERLAY_WINDOW_DLL.ovlw_wnclass_uninit()
