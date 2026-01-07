import os, socket, struct

OUTDIR = "depth_recv"
os.makedirs(OUTDIR, exist_ok=True)

HOST = "0.0.0.0"
PORT = 5001

# Header: <4sIIQIIII
HDR_FMT = "<4sIIQIIII"
HDR_SIZE = struct.calcsize(HDR_FMT)

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
                magic, ver, frame_idx, ts_ns, w, h, dtype, payload_len = struct.unpack(HDR_FMT, hdr)

                if magic != b"DEP0":
                    raise ValueError(f"bad magic: {magic!r}")

                payload = recvn(conn, payload_len)

                fname = os.path.join(OUTDIR, f"depth_{frame_idx:06d}_{w}x{h}_{ts_ns}.bin")
                with open(fname, "wb") as f:
                    f.write(payload)

                if frame_idx % 30 == 0:
                    print(f"[recv] saved frame {frame_idx} ({w}x{h}) bytes={payload_len}")

if __name__ == "__main__":
    main()
