from __future__ import annotations

import argparse
from pathlib import Path

import av
from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract selected SBS frames from an ArxVR recording")
    parser.add_argument("video", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("times", nargs="+", type=float)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    targets = sorted(args.times)
    target_index = 0

    with av.open(str(args.video)) as container:
        stream = container.streams.video[0]
        for frame in container.decode(stream):
            if target_index >= len(targets):
                break
            timestamp = float(frame.time or 0.0)
            if timestamp < targets[target_index]:
                continue
            image = Image.fromarray(frame.to_ndarray(format="rgb24"))
            path = args.output / f"video-{targets[target_index]:06.2f}s.jpg"
            image.save(path, quality=92)
            print(f"{timestamp:.3f}s -> {path}")
            target_index += 1
    return 0 if target_index == len(targets) else 1


if __name__ == "__main__":
    raise SystemExit(main())
