import cv2 as cv
import a_to_b.capture as cap
import time

def main() -> None:
    capture: cap.Capture = cap.Capture()
    while True:
        st = time.perf_counter()
        frame = capture.get_frame()
        print(f"{(time.perf_counter() - st):.3f}")
        cv.imshow("Window", frame)
        cv.waitKey(0)

if __name__ == "__main__":
    main()
