from __future__ import annotations

import argparse
import csv
from pathlib import Path

import av
import numpy as np
from PIL import Image, ImageDraw


def main() -> int:
    parser = argparse.ArgumentParser(description="Sample an ArxVR headset recording and find black stereo frames")
    parser.add_argument("video", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, object]] = []
    thumbnails: list[tuple[float, Image.Image]] = []
    next_sample = 0.0
    with av.open(str(args.video)) as container:
        stream = container.streams.video[0]
        duration = float(stream.duration * stream.time_base) if stream.duration else 0.0
        print(f"video={stream.width}x{stream.height} duration={duration:.2f}s rate={stream.average_rate}")
        for frame in container.decode(stream):
            timestamp = float(frame.time or 0.0)
            if timestamp + 1e-6 < next_sample:
                continue
            rgb = frame.to_ndarray(format="rgb24")
            half = rgb.shape[1] // 2
            for eye, data in (("left", rgb[:, :half]), ("right", rgb[:, half:])):
                sampled = data[::8, ::8].astype(np.float32)
                luminance = sampled[..., 0] * 0.2126 + sampled[..., 1] * 0.7152 + sampled[..., 2] * 0.0722
                rows.append({
                    "time_seconds": round(timestamp, 3),
                    "eye": eye,
                    "mean_luminance": round(float(luminance.mean()), 3),
                    "visible_ratio": round(float((luminance > 8.0).mean()), 4),
                })
            if len(thumbnails) < 16 and timestamp >= len(thumbnails) * max(duration / 15.0, 1.0):
                image = Image.fromarray(rgb).resize((640, 320), Image.Resampling.LANCZOS)
                thumbnails.append((timestamp, image))
            next_sample += args.interval

    metrics_path = args.output / "video-frame-metrics.csv"
    with metrics_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    dark_times = sorted({float(row["time_seconds"]) for row in rows if float(row["visible_ratio"]) < 0.02})
    (args.output / "video-black-frame-times.txt").write_text(
        "\n".join(f"{time:.3f}" for time in dark_times), encoding="utf-8"
    )

    if thumbnails:
        columns = 2
        tile_width, tile_height = 640, 350
        rows_count = (len(thumbnails) + columns - 1) // columns
        sheet = Image.new("RGB", (tile_width * columns, tile_height * rows_count), "black")
        draw = ImageDraw.Draw(sheet)
        for index, (timestamp, image) in enumerate(thumbnails):
            x = (index % columns) * tile_width
            y = (index // columns) * tile_height
            sheet.paste(image, (x, y + 25))
            draw.text((x + 8, y + 6), f"t={timestamp:.1f}s", fill="white")
        sheet.save(args.output / "video-contact-sheet.jpg", quality=90)

    print(f"samples={len(rows) // 2} dark_times={len(dark_times)}")
    if dark_times:
        print(f"first_dark={dark_times[0]:.3f}s last_dark={dark_times[-1]:.3f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
