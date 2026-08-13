from __future__ import annotations

import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[2]
TRAILER_DIR = ROOT / "StoreAssets" / "Trailer"
FRAMES_DIR = TRAILER_DIR / "frames"
BACKGROUND = TRAILER_DIR / "signal-background.png"
ICON = ROOT / "StoreAssets" / "Icon" / "AppIcon-master.png"
READY = ROOT / "StoreAssets" / "Screenshots" / "en-US" / "02-ready-to-detect.png"
DETECTED = ROOT / "StoreAssets" / "Screenshots" / "en-US" / "01-codec-detected.png"

WIDTH = 1920
HEIGHT = 1080
FPS = 30
NAVY = (7, 17, 31)
WHITE = (245, 249, 255)
MUTED = (174, 198, 224)
CYAN = (56, 189, 248)


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    family = "segoeuib.ttf" if bold else "segoeui.ttf"
    path = Path("C:/Windows/Fonts") / family
    return ImageFont.truetype(str(path), size=size)


def fit_cover(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    scale = max(size[0] / image.width, size[1] / image.height)
    resized = image.resize(
        (round(image.width * scale), round(image.height * scale)),
        Image.Resampling.LANCZOS,
    )
    left = (resized.width - size[0]) // 2
    top = (resized.height - size[1]) // 2
    return resized.crop((left, top, left + size[0], top + size[1]))


def base() -> Image.Image:
    image = fit_cover(Image.open(BACKGROUND).convert("RGB"), (WIDTH, HEIGHT))
    image = ImageEnhance.Contrast(image).enhance(1.08)
    overlay = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    draw.rectangle((0, 0, WIDTH, HEIGHT), fill=(0, 4, 14, 25))
    return Image.alpha_composite(image.convert("RGBA"), overlay)


def add_icon(canvas: Image.Image, box: tuple[int, int, int, int]) -> None:
    icon = Image.open(ICON).convert("RGBA")
    icon.thumbnail((box[2] - box[0], box[3] - box[1]), Image.Resampling.LANCZOS)
    x = box[0] + (box[2] - box[0] - icon.width) // 2
    y = box[1] + (box[3] - box[1] - icon.height) // 2
    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    shadow_icon = Image.new("RGBA", icon.size, (0, 0, 0, 150))
    shadow_icon.putalpha(icon.getchannel("A"))
    shadow.paste(shadow_icon, (x + 12, y + 20), shadow_icon)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    canvas.alpha_composite(shadow)
    canvas.alpha_composite(icon, (x, y))


def add_screenshot(canvas: Image.Image, screenshot_path: Path, left: int, top: int, width: int) -> None:
    screenshot = Image.open(screenshot_path).convert("RGB")
    height = round(width * screenshot.height / screenshot.width)
    screenshot = screenshot.resize((width, height), Image.Resampling.LANCZOS)

    rounded = Image.new("RGBA", screenshot.size, (0, 0, 0, 0))
    mask = Image.new("L", screenshot.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, screenshot.width - 1, screenshot.height - 1),
        radius=22,
        fill=255,
    )
    rounded.paste(screenshot, (0, 0), mask)

    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    shadow_box = Image.new("RGBA", screenshot.size, (0, 0, 0, 180))
    shadow_box.putalpha(mask)
    shadow.paste(shadow_box, (left + 18, top + 26), shadow_box)
    shadow = shadow.filter(ImageFilter.GaussianBlur(24))
    canvas.alpha_composite(shadow)
    canvas.alpha_composite(rounded, (left, top))

    border = ImageDraw.Draw(canvas)
    border.rounded_rectangle(
        (left - 1, top - 1, left + width, top + height),
        radius=22,
        outline=(79, 142, 211, 180),
        width=2,
    )


def text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], value: str, size: int, *, bold: bool = False, fill=WHITE) -> None:
    draw.text(xy, value, font=font(size, bold), fill=fill)


def title_frame() -> Image.Image:
    canvas = base()
    draw = ImageDraw.Draw(canvas)
    add_icon(canvas, (190, 270, 570, 650))
    text(draw, (660, 315), "Bluetooth Audio", 74, bold=True)
    text(draw, (660, 405), "Codec Viewer", 74, bold=True)
    text(draw, (664, 525), "See the codec Windows actually negotiated.", 34, fill=MUTED)
    draw.rounded_rectangle((664, 604, 1070, 666), radius=31, fill=(21, 112, 239, 235))
    text(draw, (718, 615), "LOCAL  •  FOCUSED  •  PRECISE", 23, bold=True)
    return canvas


def ready_frame() -> Image.Image:
    canvas = base()
    draw = ImageDraw.Draw(canvas)
    add_screenshot(canvas, READY, 780, 192, 1000)
    text(draw, (130, 244), "One focused action", 58, bold=True)
    text(draw, (134, 335), "Connect your Bluetooth audio device,", 31, fill=MUTED)
    text(draw, (134, 382), "then select Detect codec.", 31, fill=MUTED)
    draw.line((134, 480, 570, 480), fill=CYAN, width=5)
    text(draw, (134, 525), "The interface stays at", 30, fill=WHITE)
    text(draw, (134, 570), "standard user permission.", 30, fill=WHITE)
    return canvas


def permission_frame() -> Image.Image:
    canvas = base()
    draw = ImageDraw.Draw(canvas)
    add_icon(canvas, (720, 190, 1200, 670))
    text(draw, (445, 720), "Administrator approval appears only when detection starts.", 38, bold=True)
    text(draw, (555, 790), "A short-lived helper reads one local Bluetooth A2DP trace.", 29, fill=MUTED)
    text(draw, (590, 838), "No driver. No service. No settings changed.", 29, fill=MUTED)
    return canvas


def detected_frame() -> Image.Image:
    canvas = base()
    draw = ImageDraw.Draw(canvas)
    add_screenshot(canvas, DETECTED, 120, 166, 1180)
    text(draw, (1375, 250), "See the result", 54, bold=True)
    text(draw, (1378, 340), "Codec name", 28, fill=CYAN)
    text(draw, (1378, 386), "Output device", 28, fill=WHITE)
    text(draw, (1378, 432), "Codec IDs", 28, fill=WHITE)
    text(draw, (1378, 478), "Vendor IDs", 28, fill=WHITE)
    text(draw, (1378, 524), "Observation time", 28, fill=WHITE)
    draw.line((1378, 605, 1690, 605), fill=CYAN, width=4)
    text(draw, (1378, 650), "SBC • AAC • aptX", 26, fill=MUTED)
    text(draw, (1378, 692), "LDAC • LHDC • more", 26, fill=MUTED)
    return canvas


def privacy_frame() -> Image.Image:
    canvas = base()
    draw = ImageDraw.Draw(canvas)
    text(draw, (220, 210), "Designed for local diagnostics", 62, bold=True)
    items = [
        "No audio recording",
        "No telemetry or advertising",
        "No Bluetooth settings changed",
        "The helper exits after detection",
    ]
    y = 350
    for label in items:
        draw.ellipse((230, y - 4, 278, y + 44), fill=(21, 112, 239, 235))
        draw.line((243, y + 20, 251, y + 29), fill=WHITE, width=5)
        draw.line((251, y + 29, 266, y + 12), fill=WHITE, width=5)
        text(draw, (310, y), label, 35)
        y += 105
    add_icon(canvas, (1335, 280, 1705, 650))
    return canvas


def end_frame() -> Image.Image:
    canvas = base()
    draw = ImageDraw.Draw(canvas)
    add_icon(canvas, (720, 175, 1200, 655))
    text(draw, (478, 700), "Bluetooth Audio Codec Viewer", 62, bold=True)
    text(draw, (648, 800), "Available for Windows 10 and 11", 31, fill=MUTED)
    return canvas


def main() -> None:
    TRAILER_DIR.mkdir(parents=True, exist_ok=True)
    FRAMES_DIR.mkdir(parents=True, exist_ok=True)

    frames = [
        ("01-title.png", title_frame()),
        ("02-ready.png", ready_frame()),
        ("03-permission.png", permission_frame()),
        ("04-detected.png", detected_frame()),
        ("05-privacy.png", privacy_frame()),
        ("06-end.png", end_frame()),
    ]
    for name, frame in frames:
        frame.convert("RGB").save(FRAMES_DIR / name, quality=95)

    hero = detected_frame().convert("RGB")
    hero.save(TRAILER_DIR / "BluetoothAudioCodecViewer-SuperHero-1920x1080.png")
    title_frame().convert("RGB").save(
        TRAILER_DIR / "BluetoothAudioCodecViewer-TrailerThumbnail-1920x1080.png"
    )

    concat = TRAILER_DIR / "frames.txt"
    concat.write_text(
        "file 'frames/01-title.png'\nduration 5\n"
        "file 'frames/02-ready.png'\nduration 6\n"
        "file 'frames/03-permission.png'\nduration 6\n"
        "file 'frames/04-detected.png'\nduration 7\n"
        "file 'frames/05-privacy.png'\nduration 6\n"
        "file 'frames/06-end.png'\nduration 5\n"
        "file 'frames/06-end.png'\n",
        encoding="utf-8",
    )

    output = TRAILER_DIR / "BluetoothAudioCodecViewer-Trailer-en-US.mp4"
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-f",
            "concat",
            "-safe",
            "0",
            "-i",
            str(concat),
            "-f",
            "lavfi",
            "-i",
            "anullsrc=channel_layout=stereo:sample_rate=48000",
            "-t",
            "35",
            "-vf",
            f"fps={FPS},format=yuv420p",
            "-c:v",
            "libx264",
            "-profile:v",
            "high",
            "-level",
            "4.1",
            "-crf",
            "18",
            "-preset",
            "medium",
            "-c:a",
            "aac",
            "-b:a",
            "128k",
            "-movflags",
            "+faststart",
            str(output),
        ],
        cwd=TRAILER_DIR,
        check=True,
    )

    concat.unlink(missing_ok=True)
    for frame_path in FRAMES_DIR.glob("*.png"):
        frame_path.unlink()
    FRAMES_DIR.rmdir()


if __name__ == "__main__":
    main()
