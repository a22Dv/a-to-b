import a_to_b.overlay_window as ow
import numpy as np
import time
import cv2


def main() -> None:
    overlay = ow.Overlay()
    overlay.reposition((1920 - 1280) // 2, (1200-720) // 2)
    overlay.resize(1280, 720)
    avg_ts: float = 0
    tcount: int = 0
    start = time.perf_counter()
    text = np.zeros((720, 1280, 4), dtype=np.uint8)
    while True:
        ts = time.perf_counter() - start
        start = time.perf_counter()
        tcount += 1
        avg_ts += (ts - avg_ts) / tcount
        if not tcount & 511:
            text[:] = (0, 0, 0, 255)
            fps = 1 / avg_ts
            cv2.putText(
                text,
                f"FPS: {fps:.2f}",
                (10, 20),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (0, 255, 0, 255),
                1,
                cv2.LINE_AA,
            )
            cv2.rectangle(text, (0,0), (150, 30), (0,255,0,255), 1)
        overlay.append(text)
        overlay.update()    


if __name__ == "__main__":
    main()
