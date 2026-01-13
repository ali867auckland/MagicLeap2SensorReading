import struct
import numpy as np

def read_depth(path):
    frames = []

    with open(path, "rb") as f:
        magic = f.read(8)
        version = struct.unpack("<i", f.read(4))[0]

        while True:
            hdr = f.read(4+8)
            if not hdr:
                break

            idx, ts = struct.unpack("<Iq", hdr)

            blocks = []
            for _ in range(5):
                meta = struct.unpack("<Biiiii", f.read(21))
                nbytes = meta[-1]
                payload = f.read(nbytes)
                blocks.append((meta, payload))

            frames.append((idx, ts, blocks))

    return frames
