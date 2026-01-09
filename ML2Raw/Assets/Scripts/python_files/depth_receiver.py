import os, socket, struct

OUTDIR = "depth_recv"
os.makedirs(OUTDIR, exist_ok=True)

HOST = "0.0.0.0"
PORT = 5001

# New multiplex header (40 bytes), little-endian:
# magic(4s), ver(u32), sensorType(u16), streamId(u16),
# frame_idx(u32), ts_ns(u64), w(u32), h(u32), dtype(u32), payload_len(u32)
HDR_FMT = "<4sIHHIQIIII"
HDR_SIZE = struct.calcsize(HDR_FMT)

MAGIC = b"ML2S"

# Sensor types (must match C# enum)
SENSOR_DEPTH = 1
SENSOR_WORLDCAM = 2
SENSOR_IMU = 3

def recvn(conn, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("socket closed")
        buf += chunk
    return buf

def main():
    print(f"[recv] writing to: {os.path.abspath(OUTDIR)}")
    print(f"[recv] listening on {HOST}:{PORT} ...")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(1)

        conn, addr = s.accept()
        print(f"[recv] accepted connection from {addr}")

        with conn:
            while True:
                hdr = recvn(conn, HDR_SIZE)
                (magic, ver, sensor_type, stream_id,
                 frame_idx, ts_ns, w, h, dtype, payload_len) = struct.unpack(HDR_FMT, hdr)

                if magic != MAGIC:
                    raise ValueError(f"bad magic: {magic!r}")
                if ver != 1:
                    raise ValueError(f"unsupported version: {ver}")

                payload = recvn(conn, payload_len)

                # Route by sensor type
                if sensor_type == SENSOR_DEPTH:
                    fname = os.path.join(
                        OUTDIR,
                        f"depth_s{stream_id}_{frame_idx:06d}_{w}x{h}_{ts_ns}.bin"
                    )
                elif sensor_type == SENSOR_WORLDCAM:
                    fname = os.path.join(
                        OUTDIR,
                        f"worldcam_s{stream_id}_{frame_idx:06d}_{w}x{h}_{ts_ns}.bin"
                    )
                elif sensor_type == SENSOR_IMU:
                    fname = os.path.join(
                        OUTDIR,
                        f"imu_s{stream_id}_{frame_idx:06d}_{ts_ns}.bin"
                    )
                else:
                    fname = os.path.join(
                        OUTDIR,
                        f"unknown{sensor_type}_s{stream_id}_{frame_idx:06d}_{ts_ns}.bin"
                    )

                with open(fname, "wb") as f:
                    f.write(payload)

                if frame_idx % 30 == 0:
                    print(f"[recv] saved type={sensor_type} stream={stream_id} frame={frame_idx} bytes={payload_len} w={w} h={h} ts={ts_ns}")

if __name__ == "__main__":
    main()
