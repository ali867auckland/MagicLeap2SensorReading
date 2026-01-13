import struct

def detect_bin_type(path):
    with open(path, "rb") as f:
        magic = f.read(8)

    if magic.startswith(b"CVPOSE"):
        return "cvpose"
    if magic.startswith(b"HEADPOSE"):
        return "headpose"
    if magic.startswith(b"RGBPOSE"):
        return "rgbpose"
    if magic.startswith(b"DEPTHRAW"):
        return "depth"
    if magic.startswith(b"WORLDCAM"):
        return "worldcam"

    raise ValueError(f"Unknown bin format: {magic}")
