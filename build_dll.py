import subprocess
from typing import List

ROOT_SRC: str = "./src/c/"
ROOT_DLL: str = "./src/a_to_b/dlls/"
source_files: List[str] = [ROOT_SRC + "overlay_window.cpp"]
output_files: List[str] = [ROOT_DLL + "overlay_window.dll"]

for sfile, ofile in zip(source_files, output_files):
    result: int = subprocess.call(
        [
            "clang-cl",
            "/O2",
            "/DNDEBUG",
            "/LD",
            "/EHsc",
            sfile,
            f"/Fe{ofile}",
            "d3d11.lib",
            "dxgi.lib",
            "dcomp.lib",
            "user32.lib",
        ]
    )
    if result:
        break
    print(f"Building {sfile}... -> Return Code: {result}")
