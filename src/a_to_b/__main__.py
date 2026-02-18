import cv2 as cv
import a_to_b.wt_visual_source as wt
import time

def main() -> None:
    vs: wt.WTVisualSource = wt.WTVisualSource()
    
    while True:
        frame = vs.get_frame()
        targets = vs.scan_initial_targets(frame)

if __name__ == "__main__":
    main()
