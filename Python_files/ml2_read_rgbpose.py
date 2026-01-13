import struct
import numpy as np

HEADER = struct.Struct("<8sii")

FRAME_HDR = struct.Struct("<Ifqiiiiii ffff fff i")

def read_rgbpose(path):
    frames = []

    with open(path, "rb") as f:
        magic, version, mode = HEADER.unpack(f.read(16))

        while True:
            hdr = f.read(FRAME_HDR.size)
            if not hdr:
                break

            h = FRAME_HDR.unpack(hdr)
            img_size = h[-1]
            img = f.read(img_size)

            frames.append((h, img))

    return frames
