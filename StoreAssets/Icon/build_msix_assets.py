"""Generate the MSIX visual assets from the checked-in master app icon."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "StoreAssets" / "Icon" / "AppIcon-transparent.png"
OUTPUT = ROOT / "BluetoothAudioCodec.WinUI" / "Assets"
RESAMPLE = Image.Resampling.LANCZOS


def square(icon: Image.Image, size: int) -> Image.Image:
    return icon.resize((size, size), RESAMPLE)


def centered(icon: Image.Image, width: int, height: int, icon_size: int) -> Image.Image:
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    resized = square(icon, icon_size)
    canvas.alpha_composite(resized, ((width - icon_size) // 2, (height - icon_size) // 2))
    return canvas


def save(image: Image.Image, name: str) -> None:
    image.save(OUTPUT / name, format="PNG", optimize=True)


def normalize_master(source: Image.Image) -> Image.Image:
    """Remove faint generation artifacts and apply Win11-style safe margins."""
    icon = source.convert("RGBA")
    alpha = icon.getchannel("A").point(lambda value: 0 if value < 8 else value)
    icon.putalpha(alpha)
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError("The source icon is completely transparent.")

    cropped = icon.crop(bounds)
    maximum = round(min(source.size) * 0.84)
    scale = min(maximum / cropped.width, maximum / cropped.height)
    fitted = cropped.resize(
        (round(cropped.width * scale), round(cropped.height * scale)),
        RESAMPLE,
    )
    canvas = Image.new("RGBA", source.size, (0, 0, 0, 0))
    canvas.alpha_composite(
        fitted,
        ((canvas.width - fitted.width) // 2, (canvas.height - fitted.height) // 2),
    )
    return canvas


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with Image.open(SOURCE) as source:
        icon = normalize_master(source)

        # The executable and MSI use these assets independently of the MSIX
        # visual-element resources.
        app_icon = square(icon, 256)
        save(app_icon, "AppIcon.png")
        app_icon.save(
            OUTPUT / "AppIcon.ico",
            format="ICO",
            sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
                   (48, 48), (64, 64), (128, 128), (256, 256)],
        )

        # Unqualified fallback assets referenced directly by Package.appxmanifest.
        save(square(icon, 44), "Square44x44Logo.png")
        save(square(icon, 150), "Square150x150Logo.png")
        save(square(icon, 50), "StoreLogo.png")
        save(centered(icon, 310, 150, 132), "Wide310x150Logo.png")
        save(centered(icon, 620, 300, 220), "SplashScreen.png")

        # Scale-qualified resources prevent Windows from upscaling a small fallback.
        scales = (100, 125, 150, 200, 400)
        for scale in scales:
            factor = scale / 100
            save(square(icon, round(44 * factor)), f"Square44x44Logo.scale-{scale}.png")
            save(square(icon, round(150 * factor)), f"Square150x150Logo.scale-{scale}.png")
            save(square(icon, round(50 * factor)), f"StoreLogo.scale-{scale}.png")
            save(
                centered(icon, round(310 * factor), round(150 * factor), round(132 * factor)),
                f"Wide310x150Logo.scale-{scale}.png",
            )
            save(
                centered(icon, round(620 * factor), round(300 * factor), round(220 * factor)),
                f"SplashScreen.scale-{scale}.png",
            )

        # Target-size resources are used by the desktop shell, taskbar and Start menu.
        for size in (16, 20, 24, 30, 32, 36, 40, 44, 48, 60, 64, 72, 80, 96, 256):
            target = square(icon, size)
            save(target, f"Square44x44Logo.targetsize-{size}.png")
            save(target, f"Square44x44Logo.targetsize-{size}_altform-unplated.png")


if __name__ == "__main__":
    main()
