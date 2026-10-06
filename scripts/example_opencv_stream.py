#!/usr/bin/env python3
"""
Chiaki-ng Headless Stream Example
Demonstrates how to consume video frames from `chiaki-cli stream` via stdout
and send controller input via stdin using pure Python and OpenCV.
"""

import os
import sys
import json
import time
import argparse
import subprocess
import cv2
import numpy as np


class ChiakiStreamConsumer:
    def __init__(self, host: str, regist_key: str, morning: str,
                 video_format: str = "png", resolution: str = "720p", fps: int = 60,
                 chiaki_bin: str = "chiaki-cli"):
        self.host = host
        self.regist_key = regist_key
        self.morning = morning
        self.video_format = video_format
        self.resolution = resolution
        self.fps = fps
        self.chiaki_bin = chiaki_bin
        self.proc = None

    def start(self):
        # Resolve chiaki-cli binary
        bin_path = self.chiaki_bin
        if not os.path.exists(bin_path):
            candidates = [
                os.path.join(os.path.dirname(__file__), "..", "build-cli", "cli", "chiaki-cli.exe"),
                os.path.join(os.path.dirname(__file__), "chiaki-cli.exe"),
            ]
            for c in candidates:
                if os.path.exists(c):
                    bin_path = c
                    break

        cmd = [
            bin_path, "stream",
            "--host", self.host,
            "--regist-key", self.regist_key,
            "--morning", self.morning,
            "--video-format", self.video_format,
            "--resolution", self.resolution,
            "--fps", str(self.fps),
            "--input-format", "json"
        ]

        print(f"[*] Launching: {' '.join(cmd)}")
        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stdin=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0
        )

    def _read_exact(self, n: int) -> bytes:
        data = bytearray()
        while len(data) < n:
            chunk = self.proc.stdout.read(n - len(data))
            if not chunk:
                return bytes(data)
            data.extend(chunk)
        return bytes(data)

    def read_frame(self):
        """Reads framed packet: FRAME <w> <h> <pts> <idx> <size>\n<payload>\n"""
        header_line = self.proc.stdout.readline()
        if not header_line:
            return None

        line_str = header_line.decode("utf-8", errors="ignore").strip()
        if not line_str.startswith("FRAME "):
            return None

        parts = line_str.split()
        if len(parts) < 6:
            return None

        width = int(parts[1])
        height = int(parts[2])
        pts = int(parts[3])
        frame_idx = int(parts[4])
        payload_size = int(parts[5])

        payload = self._read_exact(payload_size)
        self.proc.stdout.read(1)  # trailing newline

        if self.video_format == "png":
            arr = np.frombuffer(payload, dtype=np.uint8)
            img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
            return img, width, height, pts, frame_idx
        elif self.video_format == "yuv420p":
            expected_size = width * height * 3 // 2
            yuv = np.frombuffer(payload[:expected_size], dtype=np.uint8).reshape((height * 3 // 2, width))
            img = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_I420)
            return img, width, height, pts, frame_idx
        return None

    def send_input(self, buttons: list = None, left_x: int = 0, left_y: int = 0,
                   right_x: int = 0, right_y: int = 0, l2: int = 0, r2: int = 0):
        if not self.proc or not self.proc.stdin:
            return
        payload = {
            "buttons": buttons or [],
            "left_x": left_x,
            "left_y": left_y,
            "right_x": right_x,
            "right_y": right_y,
            "l2": l2,
            "r2": r2
        }
        cmd = (json.dumps(payload) + "\n").encode("utf-8")
        try:
            self.proc.stdin.write(cmd)
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass

    def run(self):
        self.start()
        print("[*] Stream started. Press 'q' in preview window to exit.")
        try:
            while self.proc.poll() is None:
                frame_data = self.read_frame()
                if frame_data is None:
                    continue

                img, w, h, pts, idx = frame_data
                cv2.putText(img, f"Frame: {idx} | PTS: {pts} | {w}x{h}",
                            (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                cv2.imshow("Chiaki Headless Stream", img)

                key = cv2.waitKey(1) & 0xFF
                if key == ord('q'):
                    break
        finally:
            if self.proc:
                self.proc.terminate()
            cv2.destroyAllWindows()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Chiaki-ng Headless Stream Client Example")
    parser.add_argument("--host", required=True, help="PS4/PS5 IP Address")
    parser.add_argument("--regist-key", required=True, help="Chiaki registration key")
    parser.add_argument("--morning", required=True, help="Chiaki morning handshake token")
    parser.add_argument("--video-format", default="png", choices=["png", "yuv420p"], help="Frame format")
    parser.add_argument("--resolution", default="720p", choices=["360p", "540p", "720p", "1080p"], help="Stream resolution")
    parser.add_argument("--fps", type=int, default=60, choices=[30, 60], help="Target FPS")
    args = parser.parse_args()

    consumer = ChiakiStreamConsumer(
        host=args.host,
        regist_key=args.regist_key,
        morning=args.morning,
        video_format=args.video_format,
        resolution=args.resolution,
        fps=args.fps
    )
    consumer.run()
