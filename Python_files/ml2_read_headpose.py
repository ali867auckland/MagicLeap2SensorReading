import struct
import numpy as np

# Record layout matches HeadTrackingNativeConsumer.cs bw.Write order:
# uint32 frameIndex
# float  unityTime
# int64  timestampNs
# int32  resultCode
# float  qx,qy,qz,qw
# float  px,py,pz
# uint32 status
# float  confidence
# uint32 errorFlags
# uint8  hasMapEvent
# uint64 mapEventsMask
REC = struct.Struct("<Ifqiffff fffIfIBQ".replace(" ", ""))

def read_headpose(path):
    with open(path, "rb") as f:
        magic = f.read(8)
        if magic != b"HEADPOSE":
            raise ValueError(f"Bad magic: {magic!r}")

        version = struct.unpack("<i", f.read(4))[0]
        if version != 2:
            raise ValueError(f"Unsupported version: {version}")

        ts = []
        pos = []
        quat = []
        conf = []
        result = []
        status = []
        err = []
        mapmask = []
        has_evt = []

        while True:
            buf = f.read(REC.size)
            if len(buf) == 0:
                break
            if len(buf) != REC.size:
                # partial tail record -> stop (file is otherwise OK)
                break

            r = REC.unpack(buf)

            ts.append(r[2])
            result.append(r[3])
            quat.append(r[4:8])
            pos.append(r[8:11])
            status.append(r[11])
            conf.append(r[12])
            err.append(r[13])
            has_evt.append(r[14])
            mapmask.append(r[15])

    return (
        np.array(ts, dtype=np.int64),
        np.array(pos, dtype=np.float32),
        np.array(quat, dtype=np.float32),
        np.array(conf, dtype=np.float32),
        np.array(result, dtype=np.int32),
        np.array(status, dtype=np.uint32),
        np.array(err, dtype=np.uint32),
        np.array(has_evt, dtype=np.uint8),
        np.array(mapmask, dtype=np.uint64),
    )
