# Headless Streaming with chiaki-cli

`chiaki-cli stream` enables zero-GUI headless streaming of PlayStation 4 and PlayStation 5 Remote Play sessions with framed video pipe output and real-time controller input over stdin.

## Usage

```bash
chiaki-cli stream \
  --host <PLAYSTATION_IP> \
  --regist-key <REGISTRATION_KEY> \
  --morning <MORNING_KEY> \
  --video-format yuv420p \
  --resolution 720p \
  --fps 60 \
  --input-format json
```

### Options

* `--video-format`: Choose `yuv420p` (raw YUV420p) or `png` (encoded PNG frames). Frames are streamed directly to `stdout`.
* `--codec`: Choose `h264` or `h265` video codec.
* `--input-format`: Choose `json` or `lines` to inject controller inputs dynamically over `stdin`.
* `--ps5`: Enable PlayStation 5 protocol mode.

See `scripts/example_opencv_stream.py` for a complete Python example.
