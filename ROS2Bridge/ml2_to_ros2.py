#!/usr/bin/env python3
"""
Convert Magic Leap 2 binary data to ROS 2 bag format
This allows you to use standard ROS 2 tools and the Rerun ROS 2 bridge
"""

import struct
import numpy as np
from pathlib import Path
import argparse

# ROS 2 imports
try:
    import rclpy
    from rclpy.serialization import serialize_message
    from rosbag2_py import SequentialWriter, StorageOptions, ConverterOptions, TopicMetadata
    from sensor_msgs.msg import Imu
    from geometry_msgs.msg import PoseStamped, Pose, Point, Quaternion
    from std_msgs.msg import Header, Float32
    from builtin_interfaces.msg import Time
    ROS2_AVAILABLE = True
except ImportError:
    ROS2_AVAILABLE = False
    print("WARNING: ROS 2 not available. Install with:")
    print("  sudo apt install ros-humble-desktop python3-rosbag2-py")
    print("  source /opt/ros/humble/setup.bash")


def ns_to_ros_time(timestamp_ns: int) -> Time:
    """Convert nanosecond timestamp to ROS Time."""
    t = Time()
    t.sec = int(timestamp_ns // 1_000_000_000)
    t.nanosec = int(timestamp_ns % 1_000_000_000)
    return t


def create_ros2_bag(output_path: str, imu_file: Path, headpose_file: Path, cvpose_file: Path):
    """Convert ML2 binary files to ROS 2 bag."""
    
    if not ROS2_AVAILABLE:
        print("ERROR: ROS 2 is not available")
        return False
    
    # Initialize ROS 2
    rclpy.init()
    
    # Setup bag writer
    storage_options = StorageOptions(uri=output_path, storage_id='sqlite3')
    converter_options = ConverterOptions('', '')
    writer = SequentialWriter()
    writer.open(storage_options, converter_options)
    
    # Create topics
    topics = {
        '/ml2/imu/data': ('sensor_msgs/msg/Imu', Imu),
        '/ml2/head_tracking/pose': ('geometry_msgs/msg/PoseStamped', PoseStamped),
        '/ml2/head_tracking/confidence': ('std_msgs/msg/Float32', Float32),
        '/ml2/cv_camera/pose': ('geometry_msgs/msg/PoseStamped', PoseStamped),
    }
    
    for topic_name, (topic_type, _) in topics.items():
        topic_metadata = TopicMetadata(
            name=topic_name,
            type=topic_type,
            serialization_format='cdr'
        )
        writer.create_topic(topic_metadata)
    
    print(f"Creating ROS 2 bag: {output_path}")
    print(f"Topics: {list(topics.keys())}")
    
    # Convert IMU data
    print("\nConverting IMU data...")
    imu_count = convert_imu_to_bag(writer, imu_file, '/ml2/imu/data')
    
    # Convert head tracking
    print("Converting head tracking data...")
    head_count = convert_headpose_to_bag(writer, headpose_file, 
                                         '/ml2/head_tracking/pose',
                                         '/ml2/head_tracking/confidence')
    
    # Convert CV camera pose
    print("Converting CV camera pose data...")
    cv_count = convert_cvpose_to_bag(writer, cvpose_file, '/ml2/cv_camera/pose')
    
    del writer
    rclpy.shutdown()
    
    print(f"\n✓ ROS 2 bag created: {output_path}")
    print(f"  IMU messages: {imu_count}")
    print(f"  Head pose messages: {head_count}")
    print(f"  CV pose messages: {cv_count}")
    print(f"\nPlay with: ros2 bag play {output_path}")
    
    return True


def convert_imu_to_bag(writer: SequentialWriter, filepath: Path, topic: str) -> int:
    """Convert IMU binary to ROS 2 messages."""
    count = 0
    
    with open(filepath, 'rb') as f:
        # Skip header
        f.seek(16)
        
        sample_size = 54
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            frame_idx = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            
            # Accelerometer
            accel_x, accel_y, accel_z = struct.unpack('fff', data[offset:offset+12])
            offset += 12
            accel_ts = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            has_accel = struct.unpack('?', data[offset:offset+1])[0]
            offset += 1
            
            # Gyroscope
            gyro_x, gyro_y, gyro_z = struct.unpack('fff', data[offset:offset+12])
            offset += 12
            gyro_ts = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            has_gyro = struct.unpack('?', data[offset:offset+1])[0]
            
            if not (has_accel and has_gyro):
                continue
            
            # Create IMU message
            msg = Imu()
            msg.header = Header()
            msg.header.stamp = ns_to_ros_time(accel_ts)
            msg.header.frame_id = 'ml2_imu'
            
            msg.linear_acceleration.x = float(accel_x)
            msg.linear_acceleration.y = float(accel_y)
            msg.linear_acceleration.z = float(accel_z)
            
            msg.angular_velocity.x = float(gyro_x)
            msg.angular_velocity.y = float(gyro_y)
            msg.angular_velocity.z = float(gyro_z)
            
            # Write to bag
            writer.write(topic, serialize_message(msg), accel_ts)
            count += 1
            
            if count % 1000 == 0:
                print(f"  IMU: {count} messages...")
    
    return count


def convert_headpose_to_bag(writer: SequentialWriter, filepath: Path, 
                           pose_topic: str, confidence_topic: str) -> int:
    """Convert head pose binary to ROS 2 messages."""
    count = 0
    
    with open(filepath, 'rb') as f:
        # Skip header
        f.seek(12)
        
        sample_size = 77
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            frame_idx = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            unity_time = struct.unpack('f', data[offset:offset+4])[0]
            offset += 4
            timestamp_ns = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            result_code = struct.unpack('i', data[offset:offset+4])[0]
            offset += 4
            
            # Rotation (quaternion: x, y, z, w)
            rot_x, rot_y, rot_z, rot_w = struct.unpack('ffff', data[offset:offset+16])
            offset += 16
            
            # Position
            pos_x, pos_y, pos_z = struct.unpack('fff', data[offset:offset+12])
            offset += 12
            
            # Status info
            status = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            confidence = struct.unpack('f', data[offset:offset+4])[0]
            
            # Create pose message
            pose_msg = PoseStamped()
            pose_msg.header = Header()
            pose_msg.header.stamp = ns_to_ros_time(timestamp_ns)
            pose_msg.header.frame_id = 'world'
            
            pose_msg.pose.position = Point(x=float(pos_x), y=float(pos_y), z=float(pos_z))
            pose_msg.pose.orientation = Quaternion(x=float(rot_x), y=float(rot_y), 
                                                   z=float(rot_z), w=float(rot_w))
            
            # Create confidence message
            conf_msg = Float32()
            conf_msg.data = float(confidence)
            
            # Write to bag
            writer.write(pose_topic, serialize_message(pose_msg), timestamp_ns)
            writer.write(confidence_topic, serialize_message(conf_msg), timestamp_ns)
            count += 1
            
            if count % 100 == 0:
                print(f"  Head pose: {count} messages...")
    
    return count


def convert_cvpose_to_bag(writer: SequentialWriter, filepath: Path, topic: str) -> int:
    """Convert CV camera pose binary to ROS 2 messages."""
    count = 0
    valid_count = 0
    
    with open(filepath, 'rb') as f:
        # Skip header
        f.seek(12)
        
        sample_size = 56
        
        while True:
            data = f.read(sample_size)
            if len(data) < sample_size:
                break
            
            offset = 0
            record_idx = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            unity_time = struct.unpack('f', data[offset:offset+4])[0]
            offset += 4
            rgb_frame_idx = struct.unpack('I', data[offset:offset+4])[0]
            offset += 4
            timestamp_ns = struct.unpack('q', data[offset:offset+8])[0]
            offset += 8
            result_code = struct.unpack('i', data[offset:offset+4])[0]
            offset += 4
            
            # Rotation (quaternion: x, y, z, w)
            rot_x, rot_y, rot_z, rot_w = struct.unpack('ffff', data[offset:offset+16])
            offset += 16
            
            # Position
            pos_x, pos_y, pos_z = struct.unpack('fff', data[offset:offset+12])
            
            # Only write valid poses
            if result_code != 0:
                continue
            
            valid_count += 1
            
            # Create pose message
            msg = PoseStamped()
            msg.header = Header()
            msg.header.stamp = ns_to_ros_time(timestamp_ns)
            msg.header.frame_id = 'world'
            
            msg.pose.position = Point(x=float(pos_x), y=float(pos_y), z=float(pos_z))
            msg.pose.orientation = Quaternion(x=float(rot_x), y=float(rot_y), 
                                             z=float(rot_z), w=float(rot_w))
            
            # Write to bag
            writer.write(topic, serialize_message(msg), timestamp_ns)
            count += 1
            
            if count % 100 == 0:
                print(f"  CV pose: {count} valid messages...")
    
    print(f"  CV pose: {valid_count} valid out of {count} total")
    return count


def main():
    parser = argparse.ArgumentParser(description='Convert Magic Leap 2 binary data to ROS 2 bag')
    parser.add_argument('--imu', type=str, required=True, help='Path to imu_raw_*.bin')
    parser.add_argument('--headpose', type=str, required=True, help='Path to headpose_*.bin')
    parser.add_argument('--cvpose', type=str, required=True, help='Path to cvpose_*.bin')
    parser.add_argument('--output', type=str, required=True, help='Output bag name (e.g., ml2_session)')
    
    args = parser.parse_args()
    
    create_ros2_bag(args.output, Path(args.imu), Path(args.headpose), Path(args.cvpose))


if __name__ == '__main__':
    main()