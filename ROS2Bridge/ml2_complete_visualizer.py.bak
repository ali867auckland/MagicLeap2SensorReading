#!/usr/bin/env python3
"""
ML2 Visualizer - For Rerun v0.28.2 + Python 3.14
Simplified API that actually works
"""

import struct
import numpy as np
import rerun as rr
from pathlib import Path
from dataclasses import dataclass
from typing import List, Tuple, Optional
import argparse


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


def parse_imu_file(filepath: Path) -> Tuple[int, List[IMUSample]]:
    """Parse IMU binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        header = f.read(8)
        if header != b'IMURAW\x00\x00':
            raise ValueError(f"Invalid IMU header")
        
        version = struct.unpack('i', f.read(4))[0]
        sample_rate_hz = struct.unpack('i', f.read(4))[0]
        print(f"[IMU] version={version}, rate={sample_rate_hz}Hz")
        
        sample_size = 54
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            frame_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            
            accel_x, accel_y, accel_z = struct.unpack('fff', data[offset:offset+12])
            offset += 12
            accel_ts = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            has_accel = struct.unpack('?', data[offset:offset+1])[0]
            offset += 1
            
            gyro_x, gyro_y, gyro_z = struct.unpack('fff', data[offset:offset+12])
            offset += 12
            gyro_ts = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            has_gyro = struct.unpack('?', data[offset:offset+1])[0]
            
            samples.append(IMUSample(
                frame_index=frame_index,
                accel=np.array([accel_x, accel_y, accel_z]),
                accel_timestamp_ns=accel_ts,
                has_accel=has_accel,
                gyro=np.array([gyro_x, gyro_y, gyro_z]),
                gyro_timestamp_ns=gyro_ts,
                has_gyro=has_gyro
            ))
    
    print(f"[IMU] Loaded {len(samples)} samples")
    return sample_rate_hz, samples


def parse_headpose_file(filepath: Path) -> List[HeadPoseSample]:
    """Parse head pose binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        header = f.read(8)
        if header != b'HEADPOSE':
            raise ValueError(f"Invalid head pose header")
        
        version = struct.unpack('i', f.read(4))[0]
        print(f"[HeadPose] version={version}")
        
        sample_size = 77
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            frame_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 8
            timestamp_ns = struct.unpack('q', data[offset:offset+8])[0]
            offset += 12
            
            rot_x, rot_y, rot_z, rot_w = struct.unpack('ffff', data[offset:offset+16])
            offset += 16
            pos_x, pos_y, pos_z = struct.unpack('fff', data[offset:offset+12])
            offset += 16
            confidence = struct.unpack('f', data[offset:offset+4])[0]
            
            samples.append(HeadPoseSample(
                frame_index=frame_index,
                timestamp_ns=timestamp_ns,
                rotation=np.array([rot_x, rot_y, rot_z, rot_w]),
                position=np.array([pos_x, pos_y, pos_z]),
                confidence=confidence
            ))
    
    print(f"[HeadPose] Loaded {len(samples)} samples")
    return samples


def parse_cvpose_file(filepath: Path) -> List[CVPoseSample]:
    """Parse CV camera pose binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        header = f.read(8)
        if header != b'CVPOSE\x00\x00':
            raise ValueError(f"Invalid CV pose header")
        
        version = struct.unpack('i', f.read(4))[0]
        print(f"[CVPose] version={version}")
        
        sample_size = 56
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            record_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 12
            timestamp_ns = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            result_code = struct.unpack('i', data[offset:offset+4])[0]
            offset += 4
            
            rot_x, rot_y, rot_z, rot_w = struct.unpack('ffff', data[offset:offset+16])
            offset += 16
            pos_x, pos_y, pos_z = struct.unpack('fff', data[offset:offset+12])
            
            samples.append(CVPoseSample(
                record_index=record_index,
                timestamp_ns=timestamp_ns,
                result_code=result_code,
                rotation=np.array([rot_x, rot_y, rot_z, rot_w]),
                position=np.array([pos_x, pos_y, pos_z])
            ))
    
    print(f"[CVPose] Loaded {len(samples)} samples")
    return samples


def parse_rgbpose_file(filepath: Path, max_frames: int = 100) -> List[RGBFrameSample]:
    """Parse RGB camera with pose binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        header = f.read(8)
        if header != b'RGBPOSE\x00':
            raise ValueError(f"Invalid RGB pose header")
        
        version = struct.unpack('i', f.read(4))[0]
        capture_mode = struct.unpack('i', f.read(4))[0]
        print(f"[RGBPose] version={version}, mode={capture_mode}")
        
        frame_count = 0
        
        while frame_count < max_frames:
            try:
                frame_idx = struct.unpack('I', f.read(4))[0]
            except struct.error:
                break
            
            unity_time = struct.unpack('f', f.read(4))[0]
            timestamp_ns = struct.unpack('q', f.read(8))[0]
            width = struct.unpack('i', f.read(4))[0]
            height = struct.unpack('i', f.read(4))[0]
            stride = struct.unpack('i', f.read(4))[0]
            format_val = struct.unpack('i', f.read(4))[0]
            
            pose_valid = struct.unpack('B', f.read(1))[0]
            pose_result = struct.unpack('i', f.read(4))[0]
            
            rot_x, rot_y, rot_z, rot_w = struct.unpack('ffff', f.read(16))
            pos_x, pos_y, pos_z = struct.unpack('fff', f.read(12))
            
            bytes_written = struct.unpack('i', f.read(4))[0]
            image_data = f.read(bytes_written)
            
            samples.append(RGBFrameSample(
                frame_index=frame_idx,
                timestamp_ns=timestamp_ns,
                width=width,
                height=height,
                has_valid_pose=(pose_valid != 0),
                rotation=np.array([rot_x, rot_y, rot_z, rot_w]),
                position=np.array([pos_x, pos_y, pos_z]),
                image_data=image_data
            ))
            
            frame_count += 1
    
    print(f"[RGBPose] Loaded {len(samples)} frames (max={max_frames})")
    return samples


def parse_depth_file(filepath: Path, max_frames: int = 50) -> List[DepthFrameSample]:
    """Parse depth camera binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        header = f.read(8)
        if header != b'DEPTHRAW':
            raise ValueError(f"Invalid depth header")
        
        version = struct.unpack('i', f.read(4))[0]
        print(f"[Depth] version={version}")
        
        frame_count = 0
        
        while frame_count < max_frames:
            try:
                frame_idx = struct.unpack('I', f.read(4))[0]
            except struct.error:
                break
            
            timestamp_ns = struct.unpack('q', f.read(8))[0]
            stream_id = struct.unpack('B', f.read(1))[0]
            width = struct.unpack('i', f.read(4))[0]
            height = struct.unpack('i', f.read(4))[0]
            stride = struct.unpack('i', f.read(4))[0]
            bytes_per_pixel = struct.unpack('i', f.read(4))[0]
            bytes_written = struct.unpack('i', f.read(4))[0]
            
            depth_bytes = f.read(bytes_written)
            depth_array = np.frombuffer(depth_bytes, dtype=np.float32)
            depth_array = depth_array.reshape((height, width))
            
            samples.append(DepthFrameSample(
                frame_index=frame_idx,
                timestamp_ns=timestamp_ns,
                width=width,
                height=height,
                depth_data=depth_array
            ))
            
            frame_count += 1
    
    print(f"[Depth] Loaded {len(samples)} frames (max={max_frames})")
    return samples


def parse_worldcam_file(filepath: Path, max_frames: int = 50) -> List[WorldCamSample]:
    """Parse world camera binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        header = f.read(8)
        if header != b'WORLDCAM':
            raise ValueError(f"Invalid world cam header")
        
        version = struct.unpack('i', f.read(4))[0]
        identifiers_mask = struct.unpack('I', f.read(4))[0]
        print(f"[WorldCam] version={version}, mask={identifiers_mask}")
        
        frame_count = 0
        
        while frame_count < max_frames:
            try:
                frame_idx = struct.unpack('I', f.read(4))[0]
            except struct.error:
                break
            
            timestamp_ns = struct.unpack('q', f.read(8))[0]
            cam_id = struct.unpack('I', f.read(4))[0]
            frame_type = struct.unpack('I', f.read(4))[0]
            width = struct.unpack('i', f.read(4))[0]
            height = struct.unpack('i', f.read(4))[0]
            stride = struct.unpack('i', f.read(4))[0]
            bytes_per_pixel = struct.unpack('i', f.read(4))[0]
            bytes_written = struct.unpack('i', f.read(4))[0]
            
            image_data = f.read(bytes_written)
            
            samples.append(WorldCamSample(
                frame_index=frame_idx,
                timestamp_ns=timestamp_ns,
                camera_id=cam_id,
                width=width,
                height=height,
                image_data=image_data
            ))
            
            frame_count += 1
    
    print(f"[WorldCam] Loaded {len(samples)} frames (max={max_frames})")
    return samples


def visualize_in_rerun(
    imu_data: Optional[Tuple[int, List[IMUSample]]] = None,
    headpose_data: Optional[List[HeadPoseSample]] = None,
    cvpose_data: Optional[List[CVPoseSample]] = None,
    rgb_data: Optional[List[RGBFrameSample]] = None,
    depth_data: Optional[List[DepthFrameSample]] = None,
    worldcam_data: Optional[List[WorldCamSample]] = None,
    output_rrd: str = None
):
    """Visualize all sensor data in Rerun v0.28+."""
    
    # Simple init for v0.28
    rr.init("magic_leap_2_complete", spawn=True)
    
    print("\n" + "="*60)
    print("VISUALIZING IN RERUN")
    print("="*60)
    
    # Log IMU data
    if imu_data:
        sample_rate_hz, imu_samples = imu_data
        print(f"Logging IMU data ({len(imu_samples)} samples @ {sample_rate_hz}Hz)...")
        
        for i, sample in enumerate(imu_samples):
            if sample.has_accel:
                rr.set_time_sequence("device_time", sample.accel_timestamp_ns)
                rr.log(
                    "sensors/imu/accelerometer",
                    rr.Arrows3D(
                        origins=[[0, 0, 0]],
                        vectors=[sample.accel.tolist()],
                        colors=[[255, 0, 0]]
                    )
                )
                rr.log(
                    "sensors/imu/accel_magnitude",
                    rr.Scalar(float(np.linalg.norm(sample.accel)))
                )
            
            if sample.has_gyro:
                rr.set_time_sequence("device_time", sample.gyro_timestamp_ns)
                rr.log(
                    "sensors/imu/gyroscope",
                    rr.Arrows3D(
                        origins=[[0, 0, 0]],
                        vectors=[sample.gyro.tolist()],
                        colors=[[0, 255, 0]]
                    )
                )
                rr.log(
                    "sensors/imu/gyro_magnitude",
                    rr.Scalar(float(np.linalg.norm(sample.gyro)))
                )
            
            if i % 1000 == 0:
                print(f"  {i}/{len(imu_samples)}...")
    
    # Log head pose trajectory
    if headpose_data:
        print(f"Logging head tracking ({len(headpose_data)} samples)...")
        head_positions = []
        
        for i, sample in enumerate(headpose_data):
            rr.set_time_sequence("device_time", sample.timestamp_ns)
            
            rr.log(
                "world/head_tracking",
                rr.Transform3D(
                    translation=sample.position,
                    rotation=rr.Quaternion(xyzw=sample.rotation),
                    from_parent=False
                )
            )
            
            rr.log(
                "sensors/head_tracking/confidence",
                rr.Scalar(sample.confidence)
            )
            head_positions.append(sample.position)
        
        # Log trajectory
        if head_positions:
            step = max(1, len(head_positions) // 1000)
            rr.set_time_sequence("device_time", headpose_data[-1].timestamp_ns)
            rr.log(
                "world/head_trajectory",
                rr.LineStrips3D([head_positions[::step]], colors=[[0, 255, 255]])
            )
    
    # Log CV camera poses
    if cvpose_data:
        print(f"Logging CV camera pose ({len(cvpose_data)} samples)...")
        cv_positions = []
        
        for sample in cvpose_data:
            if sample.result_code == 0:
                rr.set_time_sequence("device_time", sample.timestamp_ns)
                
                rr.log(
                    "world/cv_camera",
                    rr.Transform3D(
                        translation=sample.position,
                        rotation=rr.Quaternion(xyzw=sample.rotation),
                        from_parent=False
                    )
                )
                
                cv_positions.append(sample.position)
        
        if cv_positions:
            step = max(1, len(cv_positions) // 1000)
            rr.set_time_sequence("device_time", cvpose_data[-1].timestamp_ns)
            rr.log(
                "world/cv_camera_trajectory",
                rr.LineStrips3D([cv_positions[::step]], colors=[[255, 255, 0]])
            )
    
    # Log RGB frames
    if rgb_data:
        print(f"Logging RGB frames ({len(rgb_data)} frames)...")
        for i, sample in enumerate(rgb_data):
            rr.set_time_sequence("device_time", sample.timestamp_ns)
            
            # Decode image
            try:
                from PIL import Image
                import io
                img = Image.open(io.BytesIO(sample.image_data))
                img_array = np.array(img)
                rr.log("cameras/rgb", rr.Image(img_array))
            except Exception as e:
                if i == 0:
                    print(f"  Warning: Could not decode RGB image: {e}")
            
            if sample.has_valid_pose:
                rr.log(
                    "world/rgb_camera",
                    rr.Transform3D(
                        translation=sample.position,
                        rotation=rr.Quaternion(xyzw=sample.rotation),
                        from_parent=False
                    )
                )
    
    # Log depth frames
    if depth_data:
        print(f"Logging depth frames ({len(depth_data)} frames)...")
        for i, sample in enumerate(depth_data):
            rr.set_time_sequence("device_time", sample.timestamp_ns)
            
            rr.log("cameras/depth", rr.DepthImage(sample.depth_data, meter=1.0))
            
            valid_depth = sample.depth_data[(sample.depth_data > 0.01) & (sample.depth_data < 10.0)]
            if len(valid_depth) > 0:
                rr.log("sensors/depth/mean", rr.Scalar(float(np.mean(valid_depth))))
                rr.log("sensors/depth/min", rr.Scalar(float(np.min(valid_depth))))
                rr.log("sensors/depth/max", rr.Scalar(float(np.max(valid_depth))))
    
    # Log world camera frames
    if worldcam_data:
        print(f"Logging world camera frames ({len(worldcam_data)} frames)...")
        for sample in worldcam_data:
            rr.set_time_sequence("device_time", sample.timestamp_ns)
            
            cam_name = {1: "left", 2: "right", 4: "center"}.get(sample.camera_id, f"cam{sample.camera_id}")
            
            img_array = np.frombuffer(sample.image_data, dtype=np.uint8)
            img_array = img_array.reshape((sample.height, sample.width))
            
            rr.log(f"cameras/worldcam_{cam_name}", rr.Image(img_array))
    
    # Save to .rrd file if requested
    if output_rrd:
        rr.save(output_rrd)
        print(f"\n✓ Saved recording to: {output_rrd}")
    
    print("\n" + "="*60)
    print("✓ VISUALIZATION COMPLETE!")
    print("="*60)
    print("Rerun viewer should be open. If not, connect with:")
    print("  rerun --connect")


def main():
    parser = argparse.ArgumentParser(description='Visualize ML2 data in Rerun v0.28+')
    parser.add_argument('--data-dir', type=str, help='Directory containing .bin files')
    parser.add_argument('--imu', type=str, help='Path to imu_raw_*.bin')
    parser.add_argument('--headpose', type=str, help='Path to headpose_*.bin')
    parser.add_argument('--cvpose', type=str, help='Path to cvpose_*.bin')
    parser.add_argument('--rgb', type=str, help='Path to rgbpose_*.bin')
    parser.add_argument('--depth', type=str, help='Path to depth_raw_*.bin')
    parser.add_argument('--worldcam', type=str, help='Path to worldcam_raw_*.bin')
    parser.add_argument('--output', type=str, help='Save to .rrd file')
    parser.add_argument('--max-rgb-frames', type=int, default=100)
    parser.add_argument('--max-depth-frames', type=int, default=50)
    parser.add_argument('--max-worldcam-frames', type=int, default=50)
    
    args = parser.parse_args()
    
    if args.data_dir:
        data_dir = Path(args.data_dir)
        args.imu = args.imu or str(next(data_dir.glob("imu_raw_*.bin"), None))
        args.headpose = args.headpose or str(next(data_dir.glob("headpose_*.bin"), None))
        args.cvpose = args.cvpose or str(next(data_dir.glob("cvpose_*.bin"), None))
        args.rgb = args.rgb or str(next(data_dir.glob("rgbpose_*.bin"), None))
        args.depth = args.depth or str(next(data_dir.glob("depth_raw_*.bin"), None))
        args.worldcam = args.worldcam or str(next(data_dir.glob("worldcam_raw_*.bin"), None))
    
    print("="*60)
    print("MAGIC LEAP 2 COMPLETE DATA VISUALIZER")
    print("="*60)
    
    imu_data = None
    headpose_data = None
    cvpose_data = None
    rgb_data = None
    depth_data = None
    worldcam_data = None
    
    if args.imu and Path(args.imu).exists():
        imu_data = parse_imu_file(Path(args.imu))
    
    if args.headpose and Path(args.headpose).exists():
        headpose_data = parse_headpose_file(Path(args.headpose))
    
    if args.cvpose and Path(args.cvpose).exists():
        cvpose_data = parse_cvpose_file(Path(args.cvpose))
    
    if args.rgb and Path(args.rgb).exists():
        rgb_data = parse_rgbpose_file(Path(args.rgb), max_frames=args.max_rgb_frames)
    
    if args.depth and Path(args.depth).exists():
        depth_data = parse_depth_file(Path(args.depth), max_frames=args.max_depth_frames)
    
    if args.worldcam and Path(args.worldcam).exists():
        worldcam_data = parse_worldcam_file(Path(args.worldcam), max_frames=args.max_worldcam_frames)
    
    visualize_in_rerun(
        imu_data=imu_data,
        headpose_data=headpose_data,
        cvpose_data=cvpose_data,
        rgb_data=rgb_data,
        depth_data=depth_data,
        worldcam_data=worldcam_data,
        output_rrd=args.output
    )


if __name__ == '__main__':
    main()