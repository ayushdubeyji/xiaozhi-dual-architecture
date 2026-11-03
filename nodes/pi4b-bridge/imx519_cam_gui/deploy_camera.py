#!/usr/bin/env python3
"""
IMX519 Camera GUI - Resource-Managed Build & Deploy Script for Pi 4B (2GB)
Builds the camera GUI using low-priority cgroups and -j2 to avoid OOM or UI freezing.
"""
import os
import sys
import subprocess
import shutil

CAM_DIR = "/home/ayush/imx519_cam_gui"
BUILD_DIR = os.path.join(CAM_DIR, "build")

def main():
    print("=== [IMX519 Camera Build & Deploy (Pi 4B 2GB)] ===")
    if not os.path.exists(CAM_DIR):
        print(f"Error: {CAM_DIR} not found.")
        sys.exit(1)

    os.chdir(CAM_DIR)
    os.makedirs(BUILD_DIR, exist_ok=True)

    # 1. Configure CMake if not already generated
    if not os.path.exists(os.path.join(BUILD_DIR, "build.ninja")) and not os.path.exists(os.path.join(BUILD_DIR, "Makefile")):
        print("Configuring CMake...")
        res = subprocess.run(["cmake", "-B", "build", "-S", "."], check=False)
        if res.returncode != 0:
            print("CMake configuration failed.")
            sys.exit(res.returncode)

    # 2. Build via lowprio-build or systemd-run with -j2
    print("Compiling IMX519 camera binary with resource limits (-j2, MemoryMax=1200M)...")
    if shutil.which("lowprio-build"):
        cmd = ["lowprio-build", "cmake", "--build", "build", "-j2"]
    elif shutil.which("systemd-run"):
        cmd = [
            "systemd-run", "--scope", "--quiet",
            "-p", "MemoryMax=1200M",
            "-p", "CPUQuota=300%",
            "-p", "CPUWeight=20",
            "nice", "-n", "15",
            "ionice", "-c3",
            "cmake", "--build", "build", "-j2"
        ]
    else:
        cmd = ["nice", "-n", "15", "cmake", "--build", "build", "-j2"]

    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"Build failed with exit code {res.returncode}")
        sys.exit(res.returncode)

    print("=== Build Successful! Binary ready at /home/ayush/imx519_cam_gui/build/imx519_cam_gui ===")

if __name__ == "__main__":
    main()
