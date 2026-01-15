#!/usr/bin/env python3
"""
ML2 Visualizer - For Rerun v0.28.2 + Python 3.14

Features:
- Works with rerun 0.28+ timing API
- Supports grayscale & RGB & RGBA/BGRA RGB frames (auto-detect)
- Draws visible axes + points for headpose & cvpose
- Safe scalar logging (no NaN/Inf crashes)
- Fixes WorldCam negative size crash (treat size as unsigned + sanity checks)
- Logs worldcam per camera_id so streams don't overwrite each other
"""

import struct
import numpy as np
import rerun as rr
from pathlib import Path
from dataclasses import dataclass
from typing import Optional
import argparse


# ============================================================
# Data Classes
# ============================================================

@dataclass
class IMUSample:
    frame_index: int
    accel: np.ndarray
    accel_timestamp_ns: int
    has_accel: bool
    gyro: np.ndarray
    gyro_timestamp_ns: int
    has_gyro: bool


@dataclass
class HeadPoseSample:
    frame_index: int
    timestamp_ns: int
    rotation: np.ndarray
    position: np.ndarray
    confidence: float


@dataclass
class CVPoseSample:
    record_index: int
    timestamp_ns: int
    result_code: int
    rotation: np.ndarray
    position: np.ndarray


@dataclass
class RGBFrameSample:
    frame_index: int
    timestamp_ns: int
    width: int
    height: int
    stride: int
    format_val: int
    has_valid_pose: bool
    rotation: np.ndarray
    position: np.ndarray
    image_data: bytes


@dataclass
class DepthFrameSample:
    frame_index: int
    timestamp_ns: int
    width: int
    height: int
    depth_data: np.ndarray


@dataclass
class WorldCamSample:
    frame_index: int
    timestamp_ns: int
    camera_id: int
    width: int
    height: int
    image_data: bytes


# ============================================================
# Rerun Helpers
# ============================================================

def rr_set_time_ns(ns: int, frame: Optional[int] = None):
    rr.set_time("device_time", duration=float(ns) * 1e-9)
    if frame is not None:
        rr.set_time("frame", sequence=int(frame))


def log_axes(entity_path: str, axis_len: float = 0.15):
    rr.log(
        entity_path,
        rr.Arrows3D(
            origins=[[0, 0, 0]] * 3,
            vectors=[
                [axis_len, 0, 0],
                [0, axis_len, 0],
                [0, 0, axis_len],
            ],
        ),
    )


# ============================================================
# Parsers
# ============================================================

def parse_imu_file(filepath: Path):
    samples = []
    with open(filepath, "rb") as f:
        f.read(8)
        version = struct.unpack("<i", f.read(4))[0]
        rate = struct.unpack("<i", f.read(4))[0]
        print(f"[IMU] version={version}, rate={rate}Hz")

        while True:
            data = f.read(54)
            if len(data) < 54:
                break

            off = 0
            frame = struct.unpack_from("<I", data, off)[0]; off += 4
            ax, ay, az = struct.unpack_from("<fff", data, off); off += 12
            ats = struct.unpack_from("<q", data, off)[0]; off += 8
            has_acc = struct.unpack_from("<?", data, off)[0]; off += 1
            gx, gy, gz = struct.unpack_from("<fff", data, off); off += 12
            gts = struct.unpack_from("<q", data, off)[0]; off += 8
            has_gyro = struct.unpack_from("<?", data, off)[0]

            samples.append(
                IMUSample(
                    frame_index=int(frame),
                    accel=np.array([ax, ay, az], dtype=np.float32),
                    accel_timestamp_ns=int(ats),
                    has_accel=bool(has_acc),
                    gyro=np.array([gx, gy, gz], dtype=np.float32),
                    gyro_timestamp_ns=int(gts),
                    has_gyro=bool(has_gyro),
                )
            )

    print(f"[IMU] Loaded {len(samples)} samples")
    return rate, samples


def parse_headpose_file(filepath: Path):
    samples = []
    with open(filepath, "rb") as f:
        f.read(12)
        while True:
            data = f.read(77)
            if len(data) < 77:
                break
            off = 0
            frame = struct.unpack_from("<I", data, off)[0]; off += 8
            ts = struct.unpack_from("<q", data, off)[0]; off += 12
            rx, ry, rz, rw = struct.unpack_from("<ffff", data, off); off += 16
            px, py, pz = struct.unpack_from("<fff", data, off); off += 16
            conf = struct.unpack_from("<f", data, off)[0]

            samples.append(
                HeadPoseSample(
                    frame_index=int(frame),
                    timestamp_ns=int(ts),
                    rotation=np.array([rx, ry, rz, rw], dtype=np.float32),
                    position=np.array([px, py, pz], dtype=np.float32),
                    confidence=float(conf),
                )
            )
    print(f"[HeadPose] Loaded {len(samples)} samples")
    return samples


def parse_cvpose_file(filepath: Path):
    samples = []
    with open(filepath, "rb") as f:
        f.read(12)
        while True:
            data = f.read(56)
            if len(data) < 56:
                break
            off = 0
            idx = struct.unpack_from("<I", data, off)[0]; off += 12
            ts = struct.unpack_from("<q", data, off)[0]; off += 8
            res = struct.unpack_from("<i", data, off)[0]; off += 4
            rx, ry, rz, rw = struct.unpack_from("<ffff", data, off); off += 16
            px, py, pz = struct.unpack_from("<fff", data, off)

            samples.append(
                CVPoseSample(
                    record_index=int(idx),
                    timestamp_ns=int(ts),
                    result_code=int(res),
                    rotation=np.array([rx, ry, rz, rw], dtype=np.float32),
                    position=np.array([px, py, pz], dtype=np.float32),
                )
            )
    print(f"[CVPose] Loaded {len(samples)} samples")
    return samples


def parse_rgbpose_file(filepath: Path, max_frames=100):
    samples = []
    with open(filepath, "rb") as f:
        f.read(16)
        for _ in range(max_frames):
            raw = f.read(4)
            if len(raw) < 4:
                break
            idx = struct.unpack("<I", raw)[0]
            f.read(4)  # unity_time
            ts = struct.unpack("<q", f.read(8))[0]
            w = struct.unpack("<i", f.read(4))[0]
            h = struct.unpack("<i", f.read(4))[0]
            stride = struct.unpack("<i", f.read(4))[0]
            fmt = struct.unpack("<i", f.read(4))[0]
            valid = struct.unpack("<B", f.read(1))[0]
            f.read(4)  # pose_result
            rx, ry, rz, rw = struct.unpack("<ffff", f.read(16))
            px, py, pz = struct.unpack("<fff", f.read(12))
            size = struct.unpack("<i", f.read(4))[0]
            img = f.read(size) if size > 0 else b""

            samples.append(
                RGBFrameSample(
                    frame_index=int(idx),
                    timestamp_ns=int(ts),
                    width=int(w),
                    height=int(h),
                    stride=int(stride),
                    format_val=int(fmt),
                    has_valid_pose=(valid != 0),
                    rotation=np.array([rx, ry, rz, rw], dtype=np.float32),
                    position=np.array([px, py, pz], dtype=np.float32),
                    image_data=img,
                )
            )
    print(f"[RGBPose] Loaded {len(samples)} frames")
    return samples


def parse_depth_file(filepath: Path, max_frames=50):
    samples = []
    with open(filepath, "rb") as f:
        f.read(12)
        for _ in range(max_frames):
            raw = f.read(4)
            if len(raw) < 4:
                break
            idx = struct.unpack("<I", raw)[0]
            ts = struct.unpack("<q", f.read(8))[0]
            f.read(1)
            w = struct.unpack("<i", f.read(4))[0]
            h = struct.unpack("<i", f.read(4))[0]
            f.read(8)  # stride + bytes_per_pixel (ignored)
            size = struct.unpack("<i", f.read(4))[0]
            depth_bytes = f.read(size) if size > 0 else b""
            if len(depth_bytes) != size:
                break
            depth = np.frombuffer(depth_bytes, dtype=np.float32).reshape(h, w)
            samples.append(DepthFrameSample(int(idx), int(ts), int(w), int(h), depth))
    print(f"[Depth] Loaded {len(samples)} frames")
    return samples


def parse_worldcam_file(filepath: Path, max_frames=50):
    samples = []
    with open(filepath, "rb") as f:
        header = f.read(8)
        if header != b"WORLDCAM":
            raise ValueError(f"Invalid worldcam header: {header!r}")

        version = struct.unpack("<i", f.read(4))[0]
        identifiers_mask = struct.unpack("<I", f.read(4))[0]
        print(f"[WorldCam] version={version}, mask={identifiers_mask}")

        for _ in range(max_frames):
            raw = f.read(4)
            if len(raw) < 4:
                break

            frame_idx = struct.unpack("<I", raw)[0]
            timestamp_ns = struct.unpack("<q", f.read(8))[0]

            cam_id = struct.unpack("<I", f.read(4))[0]
            frame_type = struct.unpack("<I", f.read(4))[0]

            width = struct.unpack("<i", f.read(4))[0]
            height = struct.unpack("<i", f.read(4))[0]
            stride = struct.unpack("<i", f.read(4))[0]
            bpp = struct.unpack("<i", f.read(4))[0]
            bytes_written = struct.unpack("<i", f.read(4))[0]

            # sanity checks to avoid going off the rails
            if width <= 0 or height <= 0 or width > 10000 or height > 10000:
                print(f"[WorldCam] Bad dimensions w={width}, h={height}. Stopping.")
                break
            if bytes_written < 0 or bytes_written > 200_000_000:
                print(f"[WorldCam] Bad bytes_written={bytes_written}. Stopping.")
                break

            image_data = f.read(bytes_written)
            if len(image_data) < bytes_written:
                print(f"[WorldCam] Truncated frame: wanted {bytes_written}, got {len(image_data)}. Stopping.")
                break

            samples.append(
                WorldCamSample(
                    frame_index=int(frame_idx),
                    timestamp_ns=int(timestamp_ns),
                    camera_id=int(cam_id),
                    width=int(width),
                    height=int(height),
                    image_data=image_data,
                )
            )

    print(f"[WorldCam] Loaded {len(samples)} frames (max={max_frames})")
    return samples




# ============================================================
# Visualization
# ============================================================

def visualize_in_rerun(imu, head, cv, rgb, depth, worldcam):
    rr.init("magic_leap_2_complete", spawn=True)

    # IMU
    rate, imu_samples = imu
    for s in imu_samples:
        if s.has_accel:
            rr_set_time_ns(s.accel_timestamp_ns, s.frame_index)
            rr.log("imu/accel", rr.Arrows3D(origins=[[0, 0, 0]], vectors=[s.accel.tolist()]))

    # Headpose
    for s in head:
        rr_set_time_ns(s.timestamp_ns, s.frame_index)
        rr.log("head", rr.Transform3D(translation=s.position, rotation=rr.Quaternion(xyzw=s.rotation)))
        log_axes("head/axes")
        rr.log("head/pos", rr.Points3D([s.position.tolist()], radii=0.02))

    # CV Pose
    for s in cv:
        if s.result_code != 0:
            continue
        rr_set_time_ns(s.timestamp_ns, s.record_index)
        rr.log("cv", rr.Transform3D(translation=s.position, rotation=rr.Quaternion(xyzw=s.rotation)))
        log_axes("cv/axes")
        rr.log("cv/pos", rr.Points3D([s.position.tolist()], radii=0.02))

    # RGB (AUTO-DETECT grayscale/RGB/RGBA/BGRA + handles your 16-bit fmt=1 case)
    for i, s in enumerate(rgb):
        rr_set_time_ns(s.timestamp_ns, s.frame_index)

        raw = np.frombuffer(s.image_data, dtype=np.uint8)
        w, h = int(s.width), int(s.height)

        if w <= 0 or h <= 0 or raw.size == 0:
            continue

        # ------------------------------------------------------------
        # SPECIAL CASE: your rgbpose fmt=1 looks like 16-bit (2 bytes/pixel)
        # You measured size=1843198 vs need=1280*720*2=1843200 (missing 2 bytes)
        # We'll pad tiny shortfalls and display as grayscale (u16 -> normalized).
        # ------------------------------------------------------------
        need_u16 = w * h * 2
        if (raw.size == need_u16) or (abs(raw.size - need_u16) <= 16):
            if raw.size < need_u16:
                raw = np.concatenate([raw, np.zeros(need_u16 - raw.size, dtype=np.uint8)])

            img16 = np.frombuffer(raw[:need_u16].tobytes(), dtype=np.uint16).reshape((h, w))

            # normalize u16 -> u8 for display
            mn = int(img16.min())
            mx = int(img16.max())
            if mx > mn:
                img8 = ((img16.astype(np.float32) - mn) / (mx - mn) * 255.0).astype(np.uint8)
            else:
                img8 = np.zeros((h, w), dtype=np.uint8)

            rr.log("camera/rgb_u16_as_gray", rr.Image(img8))

            continue

        # ------------------------------------------------------------
        # Standard packings
        # ------------------------------------------------------------

        # grayscale u8
        if raw.size == w * h:
            img = raw.reshape(h, w)
            rr.log("camera/rgb", rr.Image(img))
            continue

        # RGBA/BGRA (assume BGRA -> convert to RGBA)
        if raw.size >= w * h * 4:
            rgba = raw[: w * h * 4].reshape(h, w, 4)
            rgba = rgba[:, :, [2, 1, 0, 3]]  # BGRA -> RGBA
            rr.log("camera/rgb", rr.Image(rgba))
            continue

        # RGB
        if raw.size >= w * h * 3:
            rgb3 = raw[: w * h * 3].reshape(h, w, 3)
            rr.log("camera/rgb", rr.Image(rgb3))
            continue

        # Debug once
        if i == 0:
            print(
                f"[RGB] Unknown packing: bytes={raw.size}, "
                f"w*h={w*h}, w*h*2={w*h*2}, w*h*3={w*h*3}, w*h*4={w*h*4}, "
                f"stride={s.stride}, fmt={s.format_val}"
            )


    # Depth
    for s in depth:
        rr_set_time_ns(s.timestamp_ns, s.frame_index)
        rr.log("camera/depth", rr.DepthImage(s.depth_data, meter=1.0))

    # WorldCam (log per camera id so streams don't overwrite)
    for s in worldcam:
        rr_set_time_ns(s.timestamp_ns, s.frame_index)

        # Most worldcam frames are grayscale (H*W). If size differs, attempt best-effort.
        raw = np.frombuffer(s.image_data, dtype=np.uint8)

        cam_name = {1: "left", 2: "right", 4: "center"}.get(int(s.camera_id), f"cam{int(s.camera_id)}")
        path = f"camera/worldcam/{cam_name}"

        if raw.size == s.width * s.height:
            img = raw.reshape(s.height, s.width)
            rr.log(path, rr.Image(img))
        elif raw.size >= s.width * s.height * 4:
            img = raw[: s.width * s.height * 4].reshape(s.height, s.width, 4)
            rr.log(path, rr.Image(img))
        elif raw.size >= s.width * s.height * 3:
            img = raw[: s.width * s.height * 3].reshape(s.height, s.width, 3)
            rr.log(path, rr.Image(img))
        else:
            # fallback only if it looks like an integer number of rows
            if s.width > 0 and raw.size % s.width == 0:
                rows = raw.size // s.width
                # guard against nonsense sizes
                if 1 <= rows <= 10000:
                    img = raw.reshape(rows, s.width)
                    rr.log(path, rr.Image(img))


    print("✓ Visualization complete")
    print("Run: rerun --connect if viewer didn't open")


# ============================================================
# Main
# ============================================================

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", required=True)
    args = parser.parse_args()

    d = Path(args.data_dir)

    imu = parse_imu_file(next(d.glob("imu_raw_*.bin")))
    head = parse_headpose_file(next(d.glob("headpose_*.bin")))
    cv = parse_cvpose_file(next(d.glob("cvpose_*.bin")))
    rgb = parse_rgbpose_file(next(d.glob("rgbpose_*.bin")))
    depth = parse_depth_file(next(d.glob("depth_raw_*.bin")))
    worldcam = parse_worldcam_file(next(d.glob("worldcam_raw_*.bin")))

    visualize_in_rerun(imu, head, cv, rgb, depth, worldcam)


if __name__ == "__main__":
    main()
