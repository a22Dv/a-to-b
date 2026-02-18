from abc import abstractmethod, ABC
from numpy.typing import NDArray
from typing import List, Tuple

type TargetRect = Tuple[int, int, int, int]


class VisualSource(ABC):
    @abstractmethod
    def get_frame(self) -> NDArray | None:
        """
        Abstract class method.
        Must provide image data from a given source.

        Returns:
            NDArray | None: Image data in an NDArray. Must be in a BGRA format, or 
        """
        pass

    @abstractmethod
    def scan_initial_targets(self, frame: NDArray) -> List[TargetRect]:
        """
        Abstract class method.
        Implementation must do an initial target count for a given frame.

        Parameters:
            NDArray: Input image data.
        Returns:
            List[TargetRect]: A list of target bounding boxes for further processing.
        Notes:
            Whether targets have been found or not determines
            if the input data will be passed downstream or discarded. If so,
            the main loop will try again at a later time.
        """
        pass
