#!/usr/bin/env python3
"""
Magic Leap 2 Data Visualizer for Rerun
Parses .bin files from ML2 native sensors and visualizes them in Rerun.
"""

import struct
import numpy as np
import rerun as rr
from pathlib import Path
from dataclasses import dataclass
from typing import List, Tuple
import argparse


@dataclass
class IMUSample:
    frame_index: int
    accel: np.ndarray  # [x, y, z]
    accel_timestamp_ns: int
    has_accel: bool
    gyro: np.ndarray  # [x, y, z]
    gyro_timestamp_ns: int
    has_gyro: bool


@dataclass
class HeadPoseSample:
    frame_index: int
    unity_time: float
    timestamp_ns: int
    result_code: int
    rotation: np.ndarray  # [x, y, z, w]
    position: np.ndarray  # [x, y, z]
    status: int
    confidence: float
    error_flags: int
    has_map_event: bool
    map_events_mask: int


@dataclass
class CVPoseSample:
    record_index: int
    unity_time: float
    rgb_frame_index: int
    timestamp_ns: int
    result_code: int
    rotation: np.ndarray  # [x, y, z, w]
    position: np.ndarray  # [x, y, z]


def parse_imu_file(filepath: Path) -> Tuple[int, List[IMUSample]]:
    """Parse IMU binary file."""
    samples = []
    
    with open(filepath, 'rb') as f:
        # Read header
        header = f.read(8)
        if header != b'IMURAW\x00\x00':
            raise ValueError(f"Invalid IMU header: {header}")
        
        version = struct.unpack('i', f.read(4))[0]
        sample_rate_hz = struct.unpack('i', f.read(4))[0]
        print(f"IMU: version={version}, sample_rate={sample_rate_hz}Hz")
        
        # Read samples
        sample_size = 4 + 3*4 + 8 + 1 + 3*4 + 8 + 1  # 54 bytes per sample
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            frame_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            
            # Accelerometer
            accel_x = struct.unpack('f', data[offset:offset+4])[0]
            accel_y = struct.unpack('f', data[offset+4:offset+8])[0]
            accel_z = struct.unpack('f', data[offset+8:offset+12])[0]
            offset += 12
            accel_ts = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            has_accel = struct.unpack('?', data[offset:offset+1])[0]
            offset += 1
            
            # Gyroscope
            gyro_x = struct.unpack('f', data[offset:offset+4])[0]
            gyro_y = struct.unpack('f', data[offset+4:offset+8])[0]
            gyro_z = struct.unpack('f', data[offset+8:offset+12])[0]
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
    
    print(f"Loaded {len(samples)} IMU samples")
    return sample_rate_hz, samples


def parse_headpose_file(filepath: Path) -> List[HeadPoseSample]:
    """Parse head pose binary file (v2 format)."""
    samples = []
    
    with open(filepath, 'rb') as f:
        # Read header
        header = f.read(8)
        if header != b'HEADPOSE':
            raise ValueError(f"Invalid head pose header: {header}")
        
        version = struct.unpack('i', f.read(4))[0]
        print(f"HeadPose: version={version}")
        
        # Record size: 4 + 4 + 8 + 4 + 4*4 + 3*4 + 4 + 4 + 4 + 1 + 8 = 77 bytes
        sample_size = 77
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            frame_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            unity_time = struct.unpack('f', data[offset:offset+4])[0]
            offset += 4
            timestamp_ns = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            result_code = struct.unpack('i', data[offset:offset+4])[0]
            offset += 4
            
            # Rotation (quaternion: x, y, z, w)
            rot_x = struct.unpack('f', data[offset:offset+4])[0]
            rot_y = struct.unpack('f', data[offset+4:offset+8])[0]
            rot_z = struct.unpack('f', data[offset+8:offset+12])[0]
            rot_w = struct.unpack('f', data[offset+12:offset+16])[0]
            offset += 16
            
            # Position
            pos_x = struct.unpack('f', data[offset:offset+4])[0]
            pos_y = struct.unpack('f', data[offset+4:offset+8])[0]
            pos_z = struct.unpack('f', data[offset+8:offset+12])[0]
            offset += 12
            
            # Status info
            status = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            confidence = struct.unpack('f', data[offset:offset+4])[0]
            offset += 4
            error_flags = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            has_map_event = struct.unpack('?', data[offset:offset+1])[0]
            offset += 1
            map_events_mask = struct.unpack('Q', data[offset:offset+8])[0]
            
            samples.append(HeadPoseSample(
                frame_index=frame_index,
                unity_time=unity_time,
                timestamp_ns=timestamp_ns,
                result_code=result_code,
                rotation=np.array([rot_x, rot_y, rot_z, rot_w]),
                position=np.array([pos_x, pos_y, pos_z]),
                status=status,
                confidence=confidence,
                error_flags=error_flags,
                has_map_event=has_map_event,
                map_events_mask=map_events_mask
            ))
    
    print(f"Loaded {len(samples)} head pose samples")
    return samples


def parse_cvpose_file(filepath: Path) -> List[CVPoseSample]:
    """Parse CV camera pose binary file (v2 format)."""
    samples = []
    
    with open(filepath, 'rb') as f:
        # Read header
        header = f.read(8)
        if header != b'CVPOSE\x00\x00':
            raise ValueError(f"Invalid CV pose header: {header}")
        
        version = struct.unpack('i', f.read(4))[0]
        print(f"CVPose: version={version}")
        
        # Record size: 4 + 4 + 4 + 8 + 4 + 4*4 + 3*4 = 56 bytes
        sample_size = 56
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            record_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            unity_time = struct.unpack('f', data[offset:offset+4])[0]
            offset += 4
            rgb_frame_index = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            timestamp_ns = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            result_code = struct.unpack('i', data[offset:offset+4])[0]
            offset += 4
            
            # Rotation (quaternion: x, y, z, w)
            rot_x = struct.unpack('f', data[offset:offset+4])[0]
            rot_y = struct.unpack('f', data[offset+4:offset+8])[0]
            rot_z = struct.unpack('f', data[offset+8:offset+12])[0]
            rot_w = struct.unpack('f', data[offset+12:offset+16])[0]
            offset += 16
            
            # Position
            pos_x = struct.unpack('f', data[offset:offset+4])[0]
            pos_y = struct.unpack('f', data[offset+4:offset+8])[0]
            pos_z = struct.unpack('f', data[offset+8:offset+12])[0]
            
            samples.append(CVPoseSample(
                record_index=record_index,
                unity_time=unity_time,
                rgb_frame_index=rgb_frame_index,
                timestamp_ns=timestamp_ns,
                result_code=result_code,
                rotation=np.array([rot_x, rot_y, rot_z, rot_w]),
                position=np.array([pos_x, pos_y, pos_z])
            ))
    
    print(f"Loaded {len(samples)} CV pose samples")
    return samples


def visualize_in_rerun(
    imu_data: Tuple[int, List[IMUSample]],
    headpose_data: List[HeadPoseSample],
    cvpose_data: List[CVPoseSample],
    output_rrd: str = None
):
    """Visualize all sensor data in Rerun."""
    
    # Initialize Rerun
    rr.init("magic_leap_2_data", spawn=True)
    
    sample_rate_hz, imu_samples = imu_data
    
    print("\nVisualizing data in Rerun...")
    print(f"  IMU: {len(imu_samples)} samples @ {sample_rate_hz}Hz")
    print(f"  HeadPose: {len(headpose_data)} samples")
    print(f"  CVPose: {len(cvpose_data)} samples")
    
    # Log IMU data
    print("\nLogging IMU data...")
    for sample in imu_samples:
        if sample.has_accel:
            rr.set_time_nanos("device_time", sample.accel_timestamp_ns)
            rr.log("sensors/imu/accelerometer", rr.Arrows3D(
                origins=[[0, 0, 0]],
                vectors=[sample.accel.tolist()],
                colors=[[255, 0, 0]]
            ))
            rr.log("sensors/imu/accel_magnitude", rr.Scalar(float(np.linalg.norm(sample.accel))))
        
        if sample.has_gyro:
            rr.set_time_nanos("device_time", sample.gyro_timestamp_ns)
            rr.log("sensors/imu/gyroscope", rr.Arrows3D(
                origins=[[0, 0, 0]],
                vectors=[sample.gyro.tolist()],
                colors=[[0, 255, 0]]
            ))
            rr.log("sensors/imu/gyro_magnitude", rr.Scalar(float(np.linalg.norm(sample.gyro))))
    
    # Log head pose trajectory
    print("Logging head pose data...")
    head_positions = []
    head_timestamps = []
    
    for sample in headpose_data:
        rr.set_time_nanos("device_time", sample.timestamp_ns)
        
        # Log pose as transform
        rr.log("world/head_tracking", rr.Transform3D(
            translation=sample.position,
            rotation=rr.Quaternion(xyzw=sample.rotation),
            from_parent=False
        ))
        
        # Log confidence
        rr.log("sensors/head_tracking/confidence", rr.Scalar(sample.confidence))
        
        # Collect for trajectory
        head_positions.append(sample.position)
        head_timestamps.append(sample.timestamp_ns)
    
    # Log head trajectory as a line strip
    if head_positions:
        # Sample every Nth point for cleaner visualization
        step = max(1, len(head_positions) // 1000)
        rr.set_time_nanos("device_time", head_timestamps[-1])
        rr.log("world/head_trajectory", rr.LineStrips3D([head_positions[::step]], colors=[[0, 255, 255]]))
    
    # Log CV camera poses
    print("Logging CV camera pose data...")
    cv_positions = []
    cv_timestamps = []
    
    for sample in cvpose_data:
        if sample.result_code == 0:  # Only valid poses
            rr.set_time_nanos("device_time", sample.timestamp_ns)
            
            # Log pose as transform
            rr.log("world/cv_camera", rr.Transform3D(
                translation=sample.position,
                rotation=rr.Quaternion(xyzw=sample.rotation),
                from_parent=False
            ))
            
            # Collect for trajectory
            cv_positions.append(sample.position)
            cv_timestamps.append(sample.timestamp_ns)
    
    # Log CV camera trajectory
    if cv_positions:
        step = max(1, len(cv_positions) // 1000)
        rr.set_time_nanos("device_time", cv_timestamps[-1])
        rr.log("world/cv_camera_trajectory", rr.LineStrips3D([cv_positions[::step]], colors=[[255, 255, 0]]))
    
    # Save to .rrd file if requested
    if output_rrd:
        rr.save(output_rrd)
        print(f"\nSaved Rerun recording to: {output_rrd}")
    
    print("\n✓ Visualization complete! Check the Rerun viewer.")
    print("  Use the timeline at the bottom to scrub through time.")
    print("  Click on entities in the left panel to show/hide them.")


def main():
    parser = argparse.ArgumentParser(description='Visualize Magic Leap 2 sensor data in Rerun')
    parser.add_argument('--imu', type=str, required=True, help='Path to imu_raw_*.bin file')
    parser.add_argument('--headpose', type=str, required=True, help='Path to headpose_*.bin file')
    parser.add_argument('--cvpose', type=str, required=True, help='Path to cvpose_*.bin file')
    parser.add_argument('--output', type=str, help='Optional: Save to .rrd file')
    
    args = parser.parse_args()
    
    # Parse all data files
    print("Parsing binary files...")
    imu_data = parse_imu_file(Path(args.imu))
    headpose_data = parse_headpose_file(Path(args.headpose))
    cvpose_data = parse_cvpose_file(Path(args.cvpose))
    
    # Visualize
    visualize_in_rerun(imu_data, headpose_data, cvpose_data, args.output)


if __name__ == '__main__':
    main()