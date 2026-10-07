![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng Headless](https://streetpea.github.io/chiaki-ng/)

> **Note**: This repository is a standalone fork of [streetpea/chiaki-ng](https://github.com/streetpea/chiaki-ng) licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). It adds a high-performance headless streaming mode (`stdout` video output) and real-time controller command injection (`stdin`) to `chiaki-cli`.

---

## Headless Streaming (`chiaki-cli stream`)

This fork extends `chiaki-cli` with a zero-GUI `stream` command designed for headless automation and external processing pipelines.

* **Video Output**: Stream decoded frames directly to `stdout` in framed binary packets (`--video-format yuv420p` or `--video-format png`).
* **Controller Input**: Send real-time button, trigger, and analog stick events into `stdin` formatted as JSON (`--input-format json`).
* **Low Latency**: Bypasses GUI rendering (Qt/SDL) for minimal processing overhead.
* **Codecs**: Supports `--codec h265` (HEVC) and `--codec h264`.

A complete Python example demonstrating frame ingestion and controller interaction is provided in [`scripts/example_opencv_stream.py`](scripts/example_opencv_stream.py).

---

## About chiaki-ng

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://streetpea.github.io/chiaki-ng/).

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.
