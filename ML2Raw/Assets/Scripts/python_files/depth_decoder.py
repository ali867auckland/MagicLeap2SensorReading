#!/usr/bin/env python3
"""
ML2 DepthNativeConsumer .bin decoder

File format (little-endian), as written by your C# DepthNativeConsumer:
- 8 bytes  : ASCII magic "DEPTHRAW"
- int32    : version (currently 1)
Then repeated frames until EOF:
- uint32   : frame_index
- int64    : captureTimeNs
Then 5 blocks, each written (even if empty):
For each block:
- uint8    : block_type
- int32    : width
- int32    : height
- int32    : strideBytes
- int32    : bytesPerPixel
- int32    : nbytes
- nbytes   : payload

Block meaning (per MLDepthCamera docs):
1 = processed depth image      (float32 meters)
2 = confidence buffer          (float32, not normalized; higher=better)
3 = depth flags buffer         (uint32 bitmask per pixel)
4 = raw depth image            (float32 intensity, with illuminator)
5 = ambient raw depth image    (float32 intensity, without illuminator)

Usage examples:
  python3 ml2_depth_decoder.py /path/to/depth_raw_xxx.bin --summary --one-frame
  python3 ml2_depth_decoder.py depth.bin --frame 0 --export-npz out_frame0.npz
  python3 ml2_depth_decoder.py depth.bin --frame 0 --save-png outdir
"""

from __future__ import annotations
import argparse
import os
import struct
from dataclasses import dataclass
from typing import BinaryIO, Dict, Iterator, Optional, Tuple

import numpy as np

# Optional PNG saving (matplotlib only if requested)
def _maybe_import_matplotlib():
    import matplotlib.pyplot as plt  # noqa
    return plt

MAGIC = b"DEPTHRAW"
SUPPORTED_VERSION = 1

BLOCK_NAMES = {
    1: "depth_meters",
    2: "confidence",
    3: "depth_flags",
    4: "raw_intensity",
    5: "ambient_raw_intensity",
}

# MLDepthCameraDepthFlags bits (per docs)
FLAG_BITS = {
    "Invalid": 0,
    "Saturated": 1,
    "Inconsistent": 2,
    "LowSignal": 3,
    "FlyingPixel": 4,
    "Masked": 5,
    "SBI": 8,
    "StrayLight": 9,
    "ConnectedComponent": 10,
}


@dataclass
class Block:
    block_type: int
    width: int
    height: int
    stride: int
    bpp: int
    nbytes: int
    payload: bytes  # raw bytes

    def is_empty(self) -> bool:
        return self.nbytes <= 0 or self.width <= 0 or self.height <= 0

    def decode(self) -> Optional[np.ndarray]:
        """
        Decode the payload into a numpy array with shape (H, W), using correct dtype
        based on block_type. Returns None if empty.
        """
        if self.is_empty():
            return None

        # Basic sanity: expected bytes = stride * height for tightly-packed rows.
        # Some devices may pad rows, so we respect stride.
        if self.stride <= 0:
            raise ValueError(f"Invalid stride={self.stride} for block {self.block_type}")

        expected_min = self.stride * self.height
        if self.nbytes < expected_min:
            raise ValueError(
                f"Block {self.block_type} too small: nbytes={self.nbytes} < stride*height={expected_min}"
            )

        # Choose dtype by block type (per ML docs)
        if self.block_type == 3:
            dtype = np.uint32
        else:
            dtype = np.float32

        # We must interpret row-by-row using stride, not just nbytes == w*h*bpp
        # bytes_per_element derived from dtype, not from header bpp (bpp should match though).
        elem_size = np.dtype(dtype).itemsize

        # Number of elements per row in bytes: stride / elem_size
        if self.stride % elem_size != 0:
            raise ValueError(
                f"Stride {self.stride} not divisible by element size {elem_size} for dtype {dtype}"
            )
        elems_per_row = self.stride // elem_size

        # Read full padded array [H, elems_per_row], then slice [:, :W]
        full = np.frombuffer(self.payload[: self.stride * self.height], dtype=dtype)
        full = full.reshape((self.height, elems_per_row))
        arr = full[:, : self.width].copy()  # copy makes it independent of backing bytes

        return arr


@dataclass
class Frame:
    frame_index: int
    capture_time_ns: int
    blocks: Dict[int, Block]

    def get(self, block_type: int) -> Optional[Block]:
        return self.blocks.get(block_type)

    def decode_all(self) -> Dict[int, Optional[np.ndarray]]:
        out: Dict[int, Optional[np.ndarray]] = {}
        for bt, blk in self.blocks.items():
            out[bt] = blk.decode()
        return out


def read_exact(f: BinaryIO, n: int) -> bytes:
    b = f.read(n)
    if len(b) != n:
        raise EOFError(f"Unexpected EOF: wanted {n} bytes, got {len(b)}")
    return b


def read_header(f: BinaryIO) -> int:
    magic = read_exact(f, 8)
    if magic != MAGIC:
        raise ValueError(f"Bad magic: expected {MAGIC!r}, got {magic!r}")
    (version,) = struct.unpack("<i", read_exact(f, 4))
    if version != SUPPORTED_VERSION:
        raise ValueError(f"Unsupported version: {version} (expected {SUPPORTED_VERSION})")
    return version


def read_block(f: BinaryIO) -> Block:
    (block_type,) = struct.unpack("<B", read_exact(f, 1))
    width, height, stride, bpp, nbytes = struct.unpack("<iiiii", read_exact(f, 20))
    payload = read_exact(f, nbytes) if nbytes > 0 else b""
    return Block(block_type, width, height, stride, bpp, nbytes, payload)


def iter_frames(path: str) -> Iterator[Frame]:
    with open(path, "rb") as f:
        _ = read_header(f)

        while True:
            try:
                # frame header
                (frame_index,) = struct.unpack("<I", read_exact(f, 4))
                (capture_time_ns,) = struct.unpack("<q", read_exact(f, 8))

                blocks: Dict[int, Block] = {}
                # Your writer always outputs blocks 1..5 each frame (but we won't assume order)
                for _i in range(5):
                    blk = read_block(f)
                    blocks[blk.block_type] = blk

                yield Frame(frame_index, capture_time_ns, blocks)

            except EOFError:
                break


def load_frame(path: str, frame_number_zero_based: int) -> Frame:
    for i, fr in enumerate(iter_frames(path)):
        if i == frame_number_zero_based:
            return fr
    raise IndexError(f"Frame {frame_number_zero_based} not found (EOF)")


def summarize_frame(fr: Frame) -> str:
    lines = []
    lines.append(f"Frame#{fr.frame_index}  captureTimeNs={fr.capture_time_ns}")
    for bt in sorted(fr.blocks.keys()):
        blk = fr.blocks[bt]
        name = BLOCK_NAMES.get(bt, f"block_{bt}")
        lines.append(
            f"  Block {bt} ({name}): w={blk.width} h={blk.height} stride={blk.stride} bpp={blk.bpp} nbytes={blk.nbytes}"
        )
    return "\n".join(lines)


def stats_for_array(arr: np.ndarray) -> str:
    # Handle NaNs safely for floats
    if np.issubdtype(arr.dtype, np.floating):
        finite = np.isfinite(arr)
        if finite.any():
            v = arr[finite]
            return f"dtype={arr.dtype} min={v.min():.6g} max={v.max():.6g} mean={v.mean():.6g}"
        return f"dtype={arr.dtype} all-nonfinite"
    else:
        return f"dtype={arr.dtype} min={arr.min()} max={arr.max()} mean={arr.mean():.6g}"


def flags_summary(flags: np.ndarray) -> str:
    if flags.dtype != np.uint32:
        flags = flags.astype(np.uint32, copy=False)
    total = flags.size
    parts = []
    for name, bit in FLAG_BITS.items():
        frac = float(np.count_nonzero(flags & (1 << bit))) / total
        parts.append(f"{name}:{frac*100:.2f}%")
    return "  Flags: " + "  ".join(parts)


def save_npz(fr: Frame, out_path: str) -> None:
    decoded = fr.decode_all()
    npz_dict = {
        "frame_index": np.array([fr.frame_index], dtype=np.uint32),
        "capture_time_ns": np.array([fr.capture_time_ns], dtype=np.int64),
    }
    for bt, arr in decoded.items():
        key = BLOCK_NAMES.get(bt, f"block_{bt}")
        if arr is None:
            continue
        npz_dict[key] = arr
    np.savez_compressed(out_path, **npz_dict)


def save_pngs(fr: Frame, out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    plt = _maybe_import_matplotlib()

    decoded = fr.decode_all()

    # Save depth/conf/raw/ambient as grayscale images (auto scale)
    for bt in (1, 2, 4, 5):
        arr = decoded.get(bt)
        if arr is None:
            continue
        name = BLOCK_NAMES.get(bt, f"block_{bt}")
        out = os.path.join(out_dir, f"{name}_frame{fr.frame_index}.png")
        plt.figure()
        plt.imshow(arr)  # do not force colors; default colormap is fine for debugging
        plt.title(f"{name} frame={fr.frame_index}")
        plt.colorbar()
        plt.savefig(out, dpi=150, bbox_inches="tight")
        plt.close()

    # Flags: visualize as integers (still useful)
    flags = decoded.get(3)
    if flags is not None:
        out = os.path.join(out_dir, f"depth_flags_frame{fr.frame_index}.png")
        plt.figure()
        plt.imshow(flags.astype(np.uint32))
        plt.title(f"depth_flags frame={fr.frame_index}")
        plt.colorbar()
        plt.savefig(out, dpi=150, bbox_inches="tight")
        plt.close()


def main():
    ap = argparse.ArgumentParser(description="Decode ML2 depth_raw_*.bin files produced by DepthNativeConsumer.")
    ap.add_argument("input", help="Path to depth_raw_*.bin")
    ap.add_argument("--frame", type=int, default=0, help="Zero-based frame number to load (default 0)")
    ap.add_argument("--one-frame", action="store_true", help="Print a detailed summary + stats for one frame")
    ap.add_argument("--summary", action="store_true", help="Print file summary (frame count, timestamps)")
    ap.add_argument("--export-npz", default="", help="Write selected frame to .npz (path)")
    ap.add_argument("--save-png", default="", help="Write debug PNGs for selected frame to directory")
    args = ap.parse_args()

    if args.summary:
        count = 0
        first_ts = None
        last_ts = None
        last_frame_index = None
        for fr in iter_frames(args.input):
            count += 1
            if first_ts is None:
                first_ts = fr.capture_time_ns
            last_ts = fr.capture_time_ns
            last_frame_index = fr.frame_index
        print(f"Frames: {count}")
        if count > 0:
            print(f"First captureTimeNs: {first_ts}")
            print(f"Last  captureTimeNs: {last_ts}")
            print(f"Last frame_index: {last_frame_index}")

        # If only summary was requested, we can exit early
        if not (args.one_frame or args.export_npz or args.save_png):
            return

    fr = load_frame(args.input, args.frame)

    if args.one_frame:
        print(summarize_frame(fr))
        decoded = fr.decode_all()
        for bt in sorted(decoded.keys()):
            arr = decoded[bt]
            name = BLOCK_NAMES.get(bt, f"block_{bt}")
            if arr is None:
                print(f"  {name}: empty")
                continue
            print(f"  {name}: {stats_for_array(arr)}")
            if bt == 3:
                print(flags_summary(arr))

    if args.export_npz:
        save_npz(fr, args.export_npz)
        print(f"Wrote NPZ: {args.export_npz}")

    if args.save_png:
        save_pngs(fr, args.save_png)
        print(f"Wrote PNGs to: {args.save_png}")


if __name__ == "__main__":
    main()
