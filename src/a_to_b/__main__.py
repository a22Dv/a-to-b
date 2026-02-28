import a_to_b.tracked_overlay as to
import numpy as np
import cv2
import time


def main() -> None:
    overlay: to.TrackedOverlay = to.TrackedOverlay("War Thunder")

    arr = np.zeros((1200, 1920, 4), np.uint8)
    while True:
        pos, _, found = overlay.track()
        _, _, w, h = pos # pos is (0,0,0,0) if not found.

        if found:
            arr[:] = 0
            cv2.rectangle(arr, (0, 0), (w - 1, h - 1), (0, 255, 0, 255), 1)
        else:
            print("NOT FOUND", end="\r")
            overlay.clear()

        overlay.append(arr[:h, :w, :], overwrite=True)
        overlay.update()
        time.sleep(0.03)


if __name__ == "__main__":
    main()
