import os
import shutil

os.system("clang-cl /Zi /Od /LD ./src/c/overlay_window.c /Feoverlay_window.dll d3d11.lib dxgi.lib user32.lib dxguid.lib")

try:
    os.remove("./src/a_to_b/dlls/overlay_window.dll")
    os.remove("./src/a_to_b/dlls/overlay_window.exp")
    os.remove("./src/a_to_b/dlls/overlay_window.ilk")
    os.remove("./src/a_to_b/dlls/overlay_window.lib")
    os.remove("./src/a_to_b/dlls/overlay_window.pdb")
except Exception as e:
    pass


shutil.copyfile("./overlay_window.dll", "./src/a_to_b/dlls/overlay_window.dll")
shutil.copyfile("./overlay_window.exp", "./src/a_to_b/dlls/overlay_window.exp")
shutil.copyfile("./overlay_window.ilk", "./src/a_to_b/dlls/overlay_window.ilk")
shutil.copyfile("./overlay_window.lib", "./src/a_to_b/dlls/overlay_window.lib")
shutil.copyfile("./overlay_window.pdb", "./src/a_to_b/dlls/overlay_window.pdb")

