import struct
import numpy as np

REC = struct.Struct("<IfIq i ffff fff")

def read_cvpose(path):
    with open(path, "rb") as f:
        magic = f.read(8)
        version = struct.unpack("<i", f.read(4))[0]

        ts = []
        pos = []
        quat = []
        result = []

        while True:
            buf = f.read(REC.size)
            if not buf:
                break
            r = REC.unpack(buf)

            ts.append(r[3])
            result.append(r[4])
            quat.append(r[5:9])
            pos.append(r[9:12])

    return np.array(ts), np.array(pos), np.array(quat), np.array(result)
