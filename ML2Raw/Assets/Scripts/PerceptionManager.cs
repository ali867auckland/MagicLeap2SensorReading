using UnityEngine;

public class PerceptionManager : MonoBehaviour
{
    [Header("Perception Startup")]
    [Tooltip("How long to wait for Perception services to become ready.")]
    [SerializeField] private uint startupWaitMs = 2000;

    [Tooltip("Keep Perception running across scene loads.")]
    [SerializeField] private bool persistAcrossScenes = true;

    private static PerceptionManager _instance;
    private bool _started;
    private bool _shutdownCalled;

    void Awake()
    {
        if (_instance != null && _instance != this)
        {
            Destroy(gameObject);
            return;
        }
        _instance = this;

        if (persistAcrossScenes)
            DontDestroyOnLoad(gameObject);

#if UNITY_ANDROID && !UNITY_EDITOR
        _started = MLDepthNative.MLPerceptionService_StartupAndWait(startupWaitMs);
        Debug.Log($"[PerceptionManager] StartupAndWait({startupWaitMs}) => {_started}");

        if (!_started)
            Debug.LogError("[PerceptionManager] Perception failed to start. Sensors may fail to connect.");
#else
        _started = true;
        Debug.Log($"[PerceptionManager] (Editor) Skipping Perception startup. waitMs={startupWaitMs}");
#endif
    }

    void OnApplicationQuit() => ShutdownIfNeeded();

    void OnDestroy()
    {
        if (_instance == this)
        {
            ShutdownIfNeeded();
            _instance = null;
        }
    }

    private void ShutdownIfNeeded()
    {
        if (_shutdownCalled) return;
        _shutdownCalled = true;

#if UNITY_ANDROID && !UNITY_EDITOR
        if (_started)
        {
            try
            {
                MLDepthNative.MLPerceptionService_Shutdown();
                Debug.Log("[PerceptionManager] Shutdown OK");
            }
            catch (System.Exception e)
            {
                Debug.LogWarning("[PerceptionManager] Shutdown exception: " + e);
            }
        }
#endif
        _started = false;
    }

    public static bool IsReady => _instance != null && _instance._started;
}
