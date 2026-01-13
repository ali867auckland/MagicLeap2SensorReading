#!/usr/bin/env python3
"""
ML2 Correctness Report (all sensors)

Usage:
  python3 ml2_correctness_report.py /path/to/file.bin
  python3 ml2_correctness_report.py /path/to/export_folder

It auto-detects:
  - CVPOSE (v1 or v2)
  - HEADPOSE (v2)
  - RGBPOSE (v1)
  - DEPTHRAW (v1)
  - WORLDCAM (v1)

Outputs:
  - terminal health report
  - matplotlib graphs (dt, pos xyz, quat norm)
"""

import os
import sys
import struct
from dataclasses import dataclass
from typing import Optional, List, Tuple, Dict

import numpy as np
import matplotlib.pyplot as plt


# ----------------------------
# Helpers
# ----------------------------

def read_exact(f, n: int) -> Optional[bytes]:
    b = f.read(n)
    if len(b) != n:
        return None
    return b

def is_monotonic_non_decreasing(a: np.ndarray) -> bool:
    if len(a) < 2:
        return True
    return np.all(np.diff(a) >= 0)

def robust_dt_ms(ts_ns: np.ndarray) -> np.ndarray:
    if len(ts_ns) < 2:
        return np.array([], dtype=np.float64)
    return np.diff(ts_ns).astype(np.float64) / 1e6  # ns -> ms

def fps_from_dt_ms(dt_ms: np.ndarray) -> float:
    if len(dt_ms) == 0:
        return 0.0
    med = float(np.median(dt_ms))
    if med <= 0:
        return 0.0
    return 1000.0 / med

def quat_norm(qx, qy, qz, qw) -> np.ndarray:
    q = np.vstack([qx, qy, qz, qw]).T.astype(np.float64)
    return np.sqrt(np.sum(q*q, axis=1))

def safe_print_stats(name: str, arr: np.ndarray, unit: str = ""):
    if arr is None or len(arr) == 0:
        print(f"  {name}: (no data)")
        return
    print(f"  {name}: n={len(arr)}  min={arr.min():.3f}{unit}  "
          f"p50={np.median(arr):.3f}{unit}  p95={np.percentile(arr,95):.3f}{unit}  max={arr.max():.3f}{unit}")


def detect_type(path: str) -> str:
    with open(path, "rb") as f:
        magic = f.read(8)
    if magic == b"CVPOSE\x00\x00":
        return "cvpose"
    if magic == b"HEADPOSE":
        return "headpose"
    if magic == b"RGBPOSE\x00":
        return "rgbpose"
    if magic == b"DEPTHRAW":
        return "depth"
    if magic == b"WORLDCAM":
        return "worldcam"
    return "unknown"


# ----------------------------
# CVPOSE
# ----------------------------
# v1 record:
#   u32 frameIndex, float unityTime, i64 tsNs, i32 resultCode, 4f quat, 3f pos
# v2 record:
#   u32 recordIndex, float unityTime, u32 rgbFrameIndex, i64 tsNs, i32 resultCode, 4f quat, 3f pos

CV_V1 = struct.Struct("<Ifqi4f3f")
CV_V2 = struct.Struct("<IfIqi4f3f")

def read_cvpose(path: str):
    with open(path, "rb") as f:
        magic = read_exact(f, 8)
        if magic != b"CVPOSE\x00\x00":
            raise ValueError("Not CVPOSE")
        ver_b = read_exact(f, 4)
        if ver_b is None:
            raise ValueError("CVPOSE missing version")
        ver = struct.unpack("<i", ver_b)[0]

        # Your older script wrote version=1; your newer writes version=2.
        # But we also handle "version says 1 but file is v2" by probing record sizes.
        data = f.read()

    # Try strict by version first, then fall back by size probe
    def parse_with(fmt: struct.Struct):
        rec_size = fmt.size
        n = len(data) // rec_size
        rem = len(data) % rec_size
        if n <= 0:
            return None
        if rem != 0:
            # allow truncation at end, but only if remainder is small
            pass
        rows = []
        off = 0
        for _ in range(n):
            chunk = data[off:off+rec_size]
            if len(chunk) != rec_size:
                break
            rows.append(fmt.unpack(chunk))
            off += rec_size
        return rows

    rows = None
    mode = None

    if ver == 2:
        rows = parse_with(CV_V2)
        mode = "v2"
    elif ver == 1:
        # It might still actually be v2; try v2 first if it fits nicely.
        r2 = parse_with(CV_V2)
        r1 = parse_with(CV_V1)
        # choose the one that yields more records and less leftover
        if r2 and (not r1 or len(r2) >= len(r1)):
            rows = r2
            mode = "v2(prob)"
        else:
            rows = r1
            mode = "v1"
    else:
        # unknown version: probe
        r2 = parse_with(CV_V2)
        r1 = parse_with(CV_V1)
        rows = r2 if (r2 and (not r1 or len(r2) >= len(r1))) else r1
        mode = "probe"

    if not rows:
        raise ValueError("Failed to parse CVPOSE records")

    if "v2" in mode:
        rec_idx = np.array([r[0] for r in rows], dtype=np.uint32)
        unity_t = np.array([r[1] for r in rows], dtype=np.float32)
        rgb_idx = np.array([r[2] for r in rows], dtype=np.uint32)
        ts = np.array([r[3] for r in rows], dtype=np.int64)
        rc = np.array([r[4] for r in rows], dtype=np.int32)
        qx,qy,qz,qw = (np.array([r[5] for r in rows], dtype=np.float32),
                       np.array([r[6] for r in rows], dtype=np.float32),
                       np.array([r[7] for r in rows], dtype=np.float32),
                       np.array([r[8] for r in rows], dtype=np.float32))
        px,py,pz = (np.array([r[9] for r in rows], dtype=np.float32),
                    np.array([r[10] for r in rows], dtype=np.float32),
                    np.array([r[11] for r in rows], dtype=np.float32))
        return dict(version=ver, mode=mode, index=rec_idx, unity_t=unity_t, rgb_index=rgb_idx,
                    ts=ts, rc=rc, q=(qx,qy,qz,qw), p=(px,py,pz))
    else:
        frame_idx = np.array([r[0] for r in rows], dtype=np.uint32)
        unity_t = np.array([r[1] for r in rows], dtype=np.float32)
        ts = np.array([r[2] for r in rows], dtype=np.int64)
        rc = np.array([r[3] for r in rows], dtype=np.int32)
        qx,qy,qz,qw = (np.array([r[4] for r in rows], dtype=np.float32),
                       np.array([r[5] for r in rows], dtype=np.float32),
                       np.array([r[6] for r in rows], dtype=np.float32),
                       np.array([r[7] for r in rows], dtype=np.float32))
        px,py,pz = (np.array([r[8] for r in rows], dtype=np.float32),
                    np.array([r[9] for r in rows], dtype=np.float32),
                    np.array([r[10] for r in rows], dtype=np.float32))
        return dict(version=ver, mode=mode, index=frame_idx, unity_t=unity_t, ts=ts, rc=rc,
                    q=(qx,qy,qz,qw), p=(px,py,pz))


# ----------------------------
# HEADPOSE (version 2)
# ----------------------------
# Header: "HEADPOSE"(8) + int32 version
# Record:
#   u32 frameIndex
#   float unityTime
#   i64 timestampNs
#   i32 resultCode
#   4f quat
#   3f pos
#   u32 status
#   float confidence
#   u32 errorFlags
#   u8 hasMapEvent
#   u64 mapEventsMask
HEAD_REC_V2 = struct.Struct("<Ifqi4f3fIfIBQ")

def read_headpose(path: str):
    with open(path, "rb") as f:
        magic = read_exact(f, 8)
        if magic != b"HEADPOSE":
            raise ValueError("Not HEADPOSE")
        ver_b = read_exact(f, 4)
        if ver_b is None:
            raise ValueError("HEADPOSE missing version")
        ver = struct.unpack("<i", ver_b)[0]
        if ver != 2:
            raise ValueError(f"Unsupported HEADPOSE version {ver}")

        frames = []
        while True:
            b = read_exact(f, HEAD_REC_V2.size)
            if b is None:
                break
            frames.append(HEAD_REC_V2.unpack(b))

    if not frames:
        raise ValueError("No headpose records")

    idx = np.array([r[0] for r in frames], dtype=np.uint32)
    unity_t = np.array([r[1] for r in frames], dtype=np.float32)
    ts = np.array([r[2] for r in frames], dtype=np.int64)
    rc = np.array([r[3] for r in frames], dtype=np.int32)
    qx,qy,qz,qw = (np.array([r[4] for r in frames], dtype=np.float32),
                   np.array([r[5] for r in frames], dtype=np.float32),
                   np.array([r[6] for r in frames], dtype=np.float32),
                   np.array([r[7] for r in frames], dtype=np.float32))
    px,py,pz = (np.array([r[8] for r in frames], dtype=np.float32),
                np.array([r[9] for r in frames], dtype=np.float32),
                np.array([r[10] for r in frames], dtype=np.float32))
    status = np.array([r[11] for r in frames], dtype=np.uint32)
    conf = np.array([r[12] for r in frames], dtype=np.float32)
    err = np.array([r[13] for r in frames], dtype=np.uint32)
    has_evt = np.array([r[14] for r in frames], dtype=np.uint8)
    evt_mask = np.array([r[15] for r in frames], dtype=np.uint64)

    return dict(version=ver, index=idx, unity_t=unity_t, ts=ts, rc=rc, q=(qx,qy,qz,qw), p=(px,py,pz),
                status=status, conf=conf, err=err, has_evt=has_evt, evt_mask=evt_mask)


# ----------------------------
# RGBPOSE
# ----------------------------
# Header: "RGBPOSE\0"(8) + int32 version + int32 captureMode
# Frame:
#   u32 frameIndex
#   float unityTime
#   i64 timestampNs
#   i32 width, i32 height, i32 strideBytes, i32 format
#   bool pose_valid (BinaryWriter -> 1 byte)
#   i32 pose_result_code
#   4f rot + 3f pos
#   i32 bytesWritten
#   bytes imageData
RGB_HDR = struct.Struct("<ii")
RGB_FIXED_PREFIX = struct.Struct("<Ifq4i")          # frameIndex, unityTime, ts, width,height,stride,format
RGB_AFTER_BOOL = struct.Struct("<i4f3fI")           # pose_result, quat, pos, bytesWritten

def read_rgbpose(path: str, max_frames: int = 5000):
    with open(path, "rb") as f:
        magic = read_exact(f, 8)
        if magic != b"RGBPOSE\x00":
            raise ValueError("Not RGBPOSE")
        hdr = read_exact(f, RGB_HDR.size)
        if hdr is None:
            raise ValueError("RGBPOSE missing header fields")
        ver, cap_mode = RGB_HDR.unpack(hdr)

        idxs = []
        ts = []
        rc = []
        pose_valid = []
        widths = []
        heights = []
        bytes_written = []

        # optional pose arrays
        pxs,pys,pzs = [],[],[]
        qx,qy,qz,qw = [],[],[],[]

        n = 0
        while n < max_frames:
            a = read_exact(f, RGB_FIXED_PREFIX.size)
            if a is None:
                break
            frameIndex, unityTime, tns, w, h, stride, fmt = RGB_FIXED_PREFIX.unpack(a)

            bbool = read_exact(f, 1)
            if bbool is None:
                break
            pv = 1 if bbool[0] != 0 else 0

            b2 = read_exact(f, RGB_AFTER_BOOL.size)
            if b2 is None:
                break
            pose_rc, rq_x, rq_y, rq_z, rq_w, rp_x, rp_y, rp_z, nbytes = RGB_AFTER_BOOL.unpack(b2)

            payload = read_exact(f, nbytes)
            if payload is None:
                break

            idxs.append(frameIndex)
            ts.append(tns)
            rc.append(pose_rc)
            pose_valid.append(pv)
            widths.append(w)
            heights.append(h)
            bytes_written.append(nbytes)

            qx.append(rq_x); qy.append(rq_y); qz.append(rq_z); qw.append(rq_w)
            pxs.append(rp_x); pys.append(rp_y); pzs.append(rp_z)

            n += 1

    if len(ts) == 0:
        raise ValueError("No RGBPOSE frames parsed")

    return dict(version=ver, captureMode=cap_mode,
                index=np.array(idxs, dtype=np.uint32),
                ts=np.array(ts, dtype=np.int64),
                rc=np.array(rc, dtype=np.int32),
                pose_valid=np.array(pose_valid, dtype=np.uint8),
                w=np.array(widths, dtype=np.int32),
                h=np.array(heights, dtype=np.int32),
                nbytes=np.array(bytes_written, dtype=np.int32),
                q=(np.array(qx, dtype=np.float32),np.array(qy, dtype=np.float32),np.array(qz, dtype=np.float32),np.array(qw, dtype=np.float32)),
                p=(np.array(pxs, dtype=np.float32),np.array(pys, dtype=np.float32),np.array(pzs, dtype=np.float32))
                )


# ----------------------------
# DEPTHRAW
# ----------------------------
# From your depth decoder notes:
# Header: "DEPTHRAW"(7 bytes? actually 8 bytes in your writer: ASCII "DEPTHRAW" is 7, but you wrote 8 bytes magic in python docs earlier.
# In your C# it is: bw.Write(new byte[] { 'D','E','P','T','H','R','A','W' }) -> 8 bytes
# then int32 version
# Each frame:
#   u32 frame_index
#   i64 captureTimeNs
# Then N blocks written (your file used 5 blocks, each has header):
#   u8  block_type
#   i32 width,height,strideBytes,bytesPerPixel,nbytes
#   payload[nbytes]
DEPTH_MAGIC = b"DEPTHRAW"
DEPTH_BLOCK_HDR = struct.Struct("<Biiiii")  # type, w,h,stride,bpp,nbytes
DEPTH_FRAME_PREFIX = struct.Struct("<Iq")

def read_depthraw(path: str, max_frames: int = 5000):
    with open(path, "rb") as f:
        magic = read_exact(f, 8)
        if magic != DEPTH_MAGIC:
            raise ValueError("Not DEPTHRAW")
        ver_b = read_exact(f, 4)
        if ver_b is None:
            raise ValueError("DEPTHRAW missing version")
        ver = struct.unpack("<i", ver_b)[0]

        frame_idx = []
        ts = []
        # availability counters
        blocks_seen = {1:0,2:0,3:0,4:0,5:0}
        processed_depth_samples = []  # a few float32 samples for sanity

        n = 0
        while n < max_frames:
            pfx = read_exact(f, DEPTH_FRAME_PREFIX.size)
            if pfx is None:
                break
            fi, tns = DEPTH_FRAME_PREFIX.unpack(pfx)
            frame_idx.append(fi)
            ts.append(tns)

            # There are typically 5 blocks in your format; we parse until we've read 5 blocks or EOF.
            for _ in range(5):
                bh = read_exact(f, DEPTH_BLOCK_HDR.size)
                if bh is None:
                    # truncated, stop parsing
                    n = max_frames
                    break
                btype, w, h, stride, bpp, nbytes = DEPTH_BLOCK_HDR.unpack(bh)
                payload = read_exact(f, nbytes)
                if payload is None:
                    n = max_frames
                    break

                if btype in blocks_seen and nbytes > 0:
                    blocks_seen[btype] += 1

                # Sample processed depth floats (type=1) for sanity
                if btype == 1 and nbytes >= 4:
                    # take up to first 256 floats
                    m = min(nbytes // 4, 256)
                    arr = np.frombuffer(payload[:m*4], dtype=np.float32)
                    processed_depth_samples.append(arr)

            n += 1

    if len(ts) == 0:
        raise ValueError("No DEPTH frames parsed")

    sample = None
    if len(processed_depth_samples) > 0:
        sample = np.concatenate(processed_depth_samples)

    return dict(version=ver,
                index=np.array(frame_idx, dtype=np.uint32),
                ts=np.array(ts, dtype=np.int64),
                blocks_seen=blocks_seen,
                depth_sample=sample)


# ----------------------------
# WORLDCAM
# ----------------------------
# Header: "WORLDCAM"(8) + int32 version
# Frame:
#   u32 frameIndex
#   float unityTime
#   i64 timestampNs
#   i32 cameraId
#   i32 width,height,stride,format
#   i32 bytesWritten
#   payload[bytesWritten]
WORLDCAM_MAGIC = b"WORLDCAM"
WC_HDR = struct.Struct("<i")
WC_FRAME_PREFIX = struct.Struct("<Ifqi4iI")  # idx, unityTime, ts, camId,w,h,stride,format,bytesWritten

def read_worldcam(path: str, max_frames: int = 5000):
    with open(path, "rb") as f:
        magic = read_exact(f, 8)
        if magic != WORLDCAM_MAGIC:
            raise ValueError("Not WORLDCAM")
        ver_b = read_exact(f, 4)
        if ver_b is None:
            raise ValueError("WORLDCAM missing version")
        ver = struct.unpack("<i", ver_b)[0]

        idxs = []
        ts = []
        camId = []
        widths = []
        heights = []
        nbytes = []

        n = 0
        while n < max_frames:
            p = read_exact(f, WC_FRAME_PREFIX.size)
            if p is None:
                break
            fi, ut, tns, cid, w, h, stride, fmt, nb = WC_FRAME_PREFIX.unpack(p)
            payload = read_exact(f, nb)
            if payload is None:
                break

            idxs.append(fi)
            ts.append(tns)
            camId.append(cid)
            widths.append(w)
            heights.append(h)
            nbytes.append(nb)
            n += 1

    if len(ts) == 0:
        raise ValueError("No WORLDCAM frames parsed")

    return dict(version=ver,
                index=np.array(idxs, dtype=np.uint32),
                ts=np.array(ts, dtype=np.int64),
                camId=np.array(camId, dtype=np.int32),
                w=np.array(widths, dtype=np.int32),
                h=np.array(heights, dtype=np.int32),
                nbytes=np.array(nbytes, dtype=np.int32))


# ----------------------------
# Reporting + plots
# ----------------------------

def report_pose_stream(name: str, ts: np.ndarray, rc: Optional[np.ndarray], p: Optional[Tuple[np.ndarray,np.ndarray,np.ndarray]],
                       q: Optional[Tuple[np.ndarray,np.ndarray,np.ndarray,np.ndarray]]):
    print(f"\n==================== {name.upper()} ====================")
    print(f"Records: {len(ts)}")
    print(f"Timestamps monotonic: {is_monotonic_non_decreasing(ts)}")

    dt = robust_dt_ms(ts)
    safe_print_stats("dt", dt, "ms")
    print(f"Estimated FPS (median): {fps_from_dt_ms(dt):.2f}")

    if rc is not None:
        ok_rate = float(np.mean(rc == 0)) * 100.0
        print(f"resultCode==0 rate: {ok_rate:.1f}%")
        # show top non-zero codes
        bad = rc[rc != 0]
        if len(bad) > 0:
            vals, cnt = np.unique(bad, return_counts=True)
            top = sorted(zip(vals, cnt), key=lambda x: -x[1])[:5]
            print("Top non-zero resultCodes:", ", ".join([f"{v}:{c}" for v,c in top]))

    if q is not None:
        qn = quat_norm(*q)
        safe_print_stats("quat_norm", qn, "")
        frac_bad = float(np.mean((qn < 0.98) | (qn > 1.02))) * 100.0
        print(f"quat norm outside [0.98,1.02]: {frac_bad:.2f}%")

    if p is not None:
        px,py,pz = p
        # movement speed sanity (m/frame)
        if len(px) > 1:
            dp = np.sqrt(np.diff(px)**2 + np.diff(py)**2 + np.diff(pz)**2)
            safe_print_stats("pos_step", dp, "m")


def plot_dt(title: str, ts: np.ndarray):
    dt = robust_dt_ms(ts)
    if len(dt) == 0:
        return
    plt.figure()
    plt.title(f"{title} frame dt (ms)")
    plt.plot(dt)
    plt.xlabel("frame")
    plt.ylabel("ms")
    plt.show()

def plot_pos(title: str, p):
    if p is None:
        return
    px,py,pz = p
    if len(px) == 0:
        return
    plt.figure()
    plt.title(f"{title} position (m)")
    plt.plot(px, label="x")
    plt.plot(py, label="y")
    plt.plot(pz, label="z")
    plt.xlabel("frame")
    plt.ylabel("m")
    plt.legend()
    plt.show()

def plot_quat_norm(title: str, q):
    if q is None:
        return
    qn = quat_norm(*q)
    plt.figure()
    plt.title(f"{title} quaternion norm")
    plt.plot(qn)
    plt.xlabel("frame")
    plt.ylabel("norm")
    plt.show()


def analyze_one(path: str):
    kind = detect_type(path)
    print(f"\n\n=== {os.path.basename(path)} ===")
    print(f"Detected: {kind}")

    if kind == "cvpose":
        d = read_cvpose(path)
        report_pose_stream("cvpose", d["ts"], d["rc"], d["p"], d["q"])
        plot_dt("CVPOSE", d["ts"])
        plot_pos("CVPOSE", d["p"])
        plot_quat_norm("CVPOSE", d["q"])

    elif kind == "headpose":
        d = read_headpose(path)
        report_pose_stream("headpose", d["ts"], d["rc"], d["p"], d["q"])
        safe_print_stats("confidence", d["conf"], "")
        plot_dt("HEADPOSE", d["ts"])
        plot_pos("HEADPOSE", d["p"])
        plot_quat_norm("HEADPOSE", d["q"])

    elif kind == "rgbpose":
        d = read_rgbpose(path)
        # For RGB we treat pose_result_code as rc, plus pose_valid
        print(f"\n==================== RGBPOSE ====================")
        print(f"Frames: {len(d['ts'])}  version={d['version']}  captureMode={d['captureMode']}")
        print(f"Timestamps monotonic: {is_monotonic_non_decreasing(d['ts'])}")
        dt = robust_dt_ms(d["ts"])
        safe_print_stats("dt", dt, "ms")
        print(f"Estimated FPS (median): {fps_from_dt_ms(dt):.2f}")
        print(f"pose_valid rate: {float(np.mean(d['pose_valid'] == 1))*100.0:.1f}%")
        print(f"pose_result_code==0 rate: {float(np.mean(d['rc'] == 0))*100.0:.1f}%")
        safe_print_stats("bytesWritten", d["nbytes"].astype(np.float64), "B")
        plot_dt("RGBPOSE", d["ts"])
        plot_pos("RGBPOSE pose", d["p"])
        plot_quat_norm("RGBPOSE pose", d["q"])

    elif kind == "depth":
        d = read_depthraw(path)
        print(f"\n==================== DEPTHRAW ====================")
        print(f"Frames: {len(d['ts'])}  version={d['version']}")
        print(f"Timestamps monotonic: {is_monotonic_non_decreasing(d['ts'])}")
        dt = robust_dt_ms(d["ts"])
        safe_print_stats("dt", dt, "ms")
        print(f"Estimated FPS (median): {fps_from_dt_ms(dt):.2f}")
        print("Non-empty blocks seen:", d["blocks_seen"])
        if d["depth_sample"] is not None and len(d["depth_sample"]) > 0:
            s = d["depth_sample"]
            # Basic depth sanity: ignore zeros; check positive range
            nz = s[s > 0]
            print(f"ProcessedDepth sample floats: n={len(s)}  nonzero={len(nz)}")
            if len(nz) > 0:
                safe_print_stats("depth(m) nonzero", nz.astype(np.float64), "m")
        plot_dt("DEPTHRAW", d["ts"])

    elif kind == "worldcam":
        d = read_worldcam(path)
        print(f"\n==================== WORLDCAM ====================")
        print(f"Frames: {len(d['ts'])}  version={d['version']}")
        print(f"Timestamps monotonic: {is_monotonic_non_decreasing(d['ts'])}")
        dt = robust_dt_ms(d["ts"])
        safe_print_stats("dt", dt, "ms")
        print(f"Estimated FPS (median): {fps_from_dt_ms(dt):.2f}")
        # camera distribution
        vals, cnt = np.unique(d["camId"], return_counts=True)
        print("cameraId counts:", ", ".join([f"{v}:{c}" for v,c in zip(vals,cnt)]))
        safe_print_stats("bytesWritten", d["nbytes"].astype(np.float64), "B")
        plot_dt("WORLDCAM", d["ts"])

    else:
        print("Unknown file type (header not recognized).")
        with open(path, "rb") as f:
            print("First 32 bytes:", f.read(32))


def main(arg: str):
    if os.path.isdir(arg):
        # analyze all recognized .bin in folder
        paths = []
        for fn in sorted(os.listdir(arg)):
            if fn.lower().endswith(".bin"):
                paths.append(os.path.join(arg, fn))
        if not paths:
            print("No .bin files found in folder.")
            return
        for p in paths:
            try:
                analyze_one(p)
            except Exception as e:
                print(f"\nERROR analyzing {p}: {e}")
    else:
        analyze_one(arg)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 ml2_correctness_report.py /path/to/file.bin OR /path/to/folder")
        sys.exit(1)
    main(sys.argv[1])
