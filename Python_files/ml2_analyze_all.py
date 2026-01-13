import sys
import numpy as np
import matplotlib.pyplot as plt

from ml2_autodetect import detect_bin_type
from ml2_read_cvpose import read_cvpose
from ml2_read_headpose import read_headpose

def print_stats(name, ts, result):
    if len(ts) < 2:
        print(f"{name}: only {len(ts)} records")
        return
    dts_ms = np.diff(ts) / 1e6
    fps = 1000.0 / np.median(dts_ms)
    print(f"{name}: records={len(ts)}  duration={(ts[-1]-ts[0])/1e9:.2f}s  fps~{fps:.2f}")
    print(f"  dt_ms: mean={np.mean(dts_ms):.3f}  p50={np.percentile(dts_ms,50):.3f}  p95={np.percentile(dts_ms,95):.3f}  max={np.max(dts_ms):.3f}")
    print(f"  resultCode nonzero: {int(np.sum(result != 0))}")

def plot_dt(name, ts):
    if len(ts) < 2:
        return
    dts_ms = np.diff(ts) / 1e6
    plt.figure()
    plt.plot(dts_ms)
    plt.title(f"{name} frame dt (ms)")
    plt.ylabel("ms")
    plt.xlabel("frame")
    plt.show()

def plot_pos(name, ts, pos):
    if len(ts) < 2:
        return
    t = (ts - ts[0]) / 1e9
    plt.figure()
    plt.plot(t, pos[:,0], label="x")
    plt.plot(t, pos[:,1], label="y")
    plt.plot(t, pos[:,2], label="z")
    plt.title(f"{name} position")
    plt.xlabel("t (s)")
    plt.ylabel("m")
    plt.legend()
    plt.show()

def plot_conf(name, ts, conf):
    if len(ts) < 2:
        return
    t = (ts - ts[0]) / 1e9
    plt.figure()
    plt.plot(t, conf)
    plt.title(f"{name} confidence")
    plt.xlabel("t (s)")
    plt.ylabel("confidence")
    plt.show()

def main(path):
    t = detect_bin_type(path)
    print("Detected:", t)

    if t == "cvpose":
        ts, pos, quat, result = read_cvpose(path)
        print_stats("CVPOSE", ts, result)
        plot_dt("CVPOSE", ts)
        plot_pos("CVPOSE", ts, pos)

    elif t == "headpose":
        ts, pos, quat, conf, result, status, err, has_evt, mapmask = read_headpose(path)
        print_stats("HEADPOSE", ts, result)
        plot_dt("HEADPOSE", ts)
        plot_pos("HEADPOSE", ts, pos)
        plot_conf("HEADPOSE", ts, conf)

    else:
        print("This analyzer currently plots CVPOSE + HEADPOSE.")
        print("RGB/DEPTH/WORLDCAM detection works, but plotting for them isn’t added yet in your current setup.")

if __name__ == "__main__":
    main(sys.argv[1])
