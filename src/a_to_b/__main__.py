import a_to_b.screen_capture as sc
import a_to_b.overlay_window as ow
import cv2 as cv
import numpy as np


def main() -> None:
    window: ow.OverlayWindow = ow.OverlayWindow(
        (1920 - 854) // 2, (1080 - 480) // 2, 854, 480
    )
    while True:
        frame = np.zeros((200, 200, 4), dtype=np.uint8)
        cv.rectangle(frame, (0, 0), (199, 199), (0, 255, 0, 255), 1)
        window.append(frame, 100, 100)
        window.update()


if __name__ == "__main__":
    main()
