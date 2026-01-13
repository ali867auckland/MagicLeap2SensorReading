using System;
using System.IO;
using UnityEngine;

public class IMUNativeConsumer : MonoBehaviour
{
    [Header("IMU Config")]
    [Tooltip("Sample rate in Hz (100, 200, 500)")]
    [SerializeField] private int sampleRateHz = 200;

    [Header("Debug")]
    [SerializeField] private bool debugMode = true;

    private bool started = false;
    private uint frameIndex = 0;

    private string outPath;
    private FileStream fs;
    private BinaryWriter bw;

    // Buffer for high-frequency data
    private MLIMUNative.IMUData[] imuBuffer = new MLIMUNative.IMUData[512];

    void Start()
    {
        if (!PerceptionManager.IsReady)
        {
            Debug.LogError("[IMU] PerceptionManager not ready.");
            enabled = false;
            return;
        }

        outPath = Path.Combine(Application.persistentDataPath,
            $"imu_raw_{DateTime.Now:yyyyMMdd_HHmmss}.bin");

        fs = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read);
        bw = new BinaryWriter(fs);

        // File header: "IMURAW\0\0" + version + sampleRate
        bw.Write(new byte[] { (byte)'I', (byte)'M', (byte)'U', (byte)'R', (byte)'A', (byte)'W', 0, 0 });
        bw.Write(1); // version
        bw.Write(sampleRateHz);

        // Clean init
        try { MLIMUNative.MLIMUUnity_Shutdown(); } catch { }

        bool ok = MLIMUNative.MLIMUUnity_Init(sampleRateHz);
        Debug.Log($"[IMU] Init result: {ok} (rate={sampleRateHz}Hz)");

        if (!ok)
        {
            Debug.LogError("[IMU] Init failed.");
            CleanupFile();
            enabled = false;
            return;
        }

        started = true;
        Debug.Log("[IMU] Output file: " + outPath);
    }

    void Update()
    {
        if (!started || bw == null) return;

        // Get all buffered IMU data
        int count;
        bool got = MLIMUNative.MLIMUUnity_GetBuffered(imuBuffer, imuBuffer.Length, out count);

        if (!got || count <= 0) return;

        // Write all samples
        for (int i = 0; i < count; i++)
        {
            var data = imuBuffer[i];
            frameIndex++;

            // Write sample
            bw.Write(frameIndex);
            
            // Accelerometer
            bw.Write(data.accel_x);
            bw.Write(data.accel_y);
            bw.Write(data.accel_z);
            bw.Write(data.accel_timestamp_ns);
            bw.Write(data.has_accel);
            
            // Gyroscope
            bw.Write(data.gyro_x);
            bw.Write(data.gyro_y);
            bw.Write(data.gyro_z);
            bw.Write(data.gyro_timestamp_ns);
            bw.Write(data.has_gyro);
        }

        if (debugMode && (Time.frameCount % 60 == 0))
        {
            var accelCount = MLIMUNative.MLIMUUnity_GetAccelCount();
            var gyroCount = MLIMUNative.MLIMUUnity_GetGyroCount();
            
            // Get latest for display
            MLIMUNative.IMUData latest;
            if (MLIMUNative.MLIMUUnity_TryGetLatest(out latest))
            {
                Debug.Log($"[IMU] samples={frameIndex} accel=({latest.accel_x:F2},{latest.accel_y:F2},{latest.accel_z:F2}) " +
                          $"gyro=({latest.gyro_x:F3},{latest.gyro_y:F3},{latest.gyro_z:F3}) " +
                          $"counts: a={accelCount} g={gyroCount}");
            }
            else
            {
                Debug.Log($"[IMU] samples={frameIndex} counts: accel={accelCount} gyro={gyroCount}");
            }
        }

        // Periodic flush
        if ((frameIndex % 500) == 0)
        {
            bw.Flush();
            fs.Flush();
        }
    }

    void OnDisable() => Shutdown();
    void OnApplicationQuit() => Shutdown();

    private void Shutdown()
    {
        if (!started) return;
        started = false;

        var accelCount = MLIMUNative.MLIMUUnity_GetAccelCount();
        var gyroCount = MLIMUNative.MLIMUUnity_GetGyroCount();

        try { MLIMUNative.MLIMUUnity_Shutdown(); } catch { }

        CleanupFile();

        Debug.Log($"[IMU] Saved {frameIndex} samples (accel={accelCount}, gyro={gyroCount}) to: {outPath}");
    }

    private void CleanupFile()
    {
        try { bw?.Flush(); } catch { }
        try { fs?.Flush(); } catch { }
        try { bw?.Close(); } catch { }
        try { fs?.Close(); } catch { }
        bw = null;
        fs = null;
    }

    void OnDestroy() => Shutdown();
}