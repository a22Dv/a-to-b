import a_to_b.screen_capture as sc
import cv2


def main() -> None:
    scr: sc.ScreenCapture = sc.ScreenCapture()
    cv2.imshow("SHOT", scr.capture_frame())
    cv2.waitKey(0)
    
if __name__ == "__main__":
    main()
