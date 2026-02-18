import ctypes as c
import numpy as np
import numpy.typing as npt

cptr_dll: c.CDLL = c.CDLL("./src/a_to_b/capture.dll")
cptr_dll.capture_state_create.restype = c.c_void_p
cptr_dll.capture_state_height.argtypes = [c.c_void_p]
cptr_dll.capture_state_height.restype = c.c_int
cptr_dll.capture_state_width.argtypes = [c.c_void_p]
cptr_dll.capture_state_width.restype = c.c_int
cptr_dll.capture_state_get_frame.argtypes = [c.c_void_p, c.c_void_p]
cptr_dll.capture_state_get_frame.restype = c.c_long  # HRESULT
cptr_dll.capture_state_destroy.argtypes = [c.c_void_p]
cptr_dll.capture_state_destroy.restype = None


class Capture:
    _handle: c.c_void_p = c.c_void_p()
    _buffer: npt.NDArray = np.zeros(shape=(0), dtype=np.uint8)
    _buffer_internal: c.c_void_p = c.c_void_p()

    def __init__(self) -> None:
        self.handle: c.c_void_p = cptr_dll.capture_state_create()
        if not self.handle:
            raise RuntimeError("[ERROR] Failed to initialize Capture object.")
        height: int = cptr_dll.capture_state_height(self.handle)
        width: int = cptr_dll.capture_state_width(self.handle)
        self._buffer = np.full(
            shape=(height, width, 4), fill_value=[0, 0, 255, 255], dtype=np.uint8
        )
        self._buffer_internal = self._buffer.ctypes.data_as(c.c_void_p)
        self.get_frame()  # Primer.

    def get_frame(self) -> npt.NDArray:
        """
        Captures the latest frame and returns a pointer to its internal buffer.

        Returns:
            npt.NDArray: Internal buffer after frame capture update. BGRA format.
        Warning:
            Modifying the reference will mutate the object's internal state.
        Notes:
            Call takes ~1-4ms on a 1920x1080p display. (Ryzen 7 8845HS/Radeon 780M)
        """
        cptr_dll.capture_state_get_frame(self.handle, self._buffer_internal)
        return self._buffer

    def __del__(self) -> None:
        cptr_dll.capture_state_destroy(self.handle)
