import a_to_b.visual_source as vs
import a_to_b.capture as cpt
from numpy.typing import NDArray
from typing import List
import win32.win32gui as win32gui
import cv2 as cv

class WTVisualSource(vs.VisualSource):
    capture: cpt.Capture = cpt.Capture()
    target_list: List[vs.TargetRect] = []
    game_window_loc: vs.TargetRect | None = None

    def __init__(self) -> None:
        super().__init__()

    def get_frame(self) -> NDArray:
        frame: NDArray = self.capture.get_frame()
        window_loc: vs.TargetRect | None = self.game_window_loc
        if window_loc is not None:
            [x, y, w, h] = window_loc

            # 28 is the title bar height for windowed applications.
            frame = frame[y+28:y+h, x:x+w,:]
        frame.flags.writeable = False
        return frame

    def scan_initial_targets(self, frame: NDArray) -> List[vs.TargetRect]:
        self.game_window_loc = self._find_game_window()
        if self.game_window_loc is None:
            return []
        rsub = cv.subtract(frame[:,:,2], cv.max(frame[:,:,0], frame[:,:,1]))
        _, rmask = cv.threshold(rsub[:,:], 85, 255, cv.THRESH_BINARY) 
        contours, _ = cv.findContours(rmask, cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE)

        # Detect criteria for target acquisition.
        return []
    
    @staticmethod
    def _find_game_window() -> vs.TargetRect | None: 
        def _callback_handler(hwnd: int, results: List) -> int:
            title: str = win32gui.GetWindowText(hwnd)
            if "War Thunder" in title:
                [x1, y1, x2, y2] = win32gui.GetWindowRect(hwnd)
                results.append((x1, y1, x2 - x1, y2 - y1))
                return 0
            return 1 
        windows: List[vs.TargetRect] = []
        win32gui.EnumWindows(_callback_handler, windows)
        if not windows:
            return None
        return windows[0]
