import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw


SIZES = (16, 24, 32, 48, 64, 128, 256)
ROOT = Path(__file__).resolve().parent
ICON_COVERAGE = 0.8


def apply_pixel_hinting(image: Image.Image, size: int) -> None:
    if size != 16:
        return

    draw = ImageDraw.Draw(image)
    white = (255, 255, 255, 255)
    # Keep both branch endpoints distinct after the 16px resampling step.
    draw.line(((8, 8), (10, 11)), fill=white, width=1)
    draw.line(((9, 8), (11, 8)), fill=white, width=1)
    draw.rectangle((12, 7, 13, 8), fill=white)
    draw.rectangle((9, 12, 10, 13), fill=white)


def alpha_bbox(image: Image.Image) -> tuple[int, int, int, int]:
    alpha = image.getchannel("A")
    visible = alpha.point(lambda value: 255 if value > 8 else 0)
    bbox = visible.getbbox()
    if bbox is None:
        raise ValueError("source image has no visible pixels")
    return bbox


def prepare_icon(source: Path, size: int) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    subject = image.crop(alpha_bbox(image))
    target_span = round(size * ICON_COVERAGE)
    scale = min(target_span / subject.width, target_span / subject.height)
    target_size = (
        max(1, round(subject.width * scale)),
        max(1, round(subject.height * scale)),
    )
    subject = subject.resize(target_size, Image.Resampling.LANCZOS)

    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    position = ((size - subject.width) // 2, (size - subject.height) // 2)
    canvas.alpha_composite(subject, position)
    apply_pixel_hinting(canvas, size)
    return canvas


def write_ico(images: dict[int, Image.Image], destination: Path) -> None:
    png_payloads: list[tuple[int, bytes]] = []
    for size in SIZES:
        temporary = destination.with_suffix(f".{size}.png.tmp")
        images[size].save(temporary, format="PNG", optimize=True)
        png_payloads.append((size, temporary.read_bytes()))
        temporary.unlink()

    header_size = 6 + 16 * len(png_payloads)
    offset = header_size
    entries = []
    for size, payload in png_payloads:
        encoded_size = 0 if size == 256 else size
        entries.append(
            struct.pack(
                "<BBBBHHII",
                encoded_size,
                encoded_size,
                0,
                0,
                1,
                32,
                len(payload),
                offset,
            )
        )
        offset += len(payload)

    with destination.open("wb") as icon_file:
        icon_file.write(struct.pack("<HHH", 0, 1, len(png_payloads)))
        for entry in entries:
            icon_file.write(entry)
        for _, payload in png_payloads:
            icon_file.write(payload)


def validate_icon(image: Image.Image, size: int) -> None:
    if image.size != (size, size):
        raise ValueError(f"{size}px icon has wrong dimensions: {image.size}")
    if image.mode != "RGBA":
        raise ValueError(f"{size}px icon does not have an alpha channel")
    corners = ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1))
    if any(image.getpixel(point)[3] != 0 for point in corners):
        raise ValueError(f"{size}px icon does not have transparent corners")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Build QtGitStudio PNG and ICO resources from per-size transparent sources."
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        required=True,
        help="Directory containing QtGitStudio-<size>-source.png files.",
    )
    args = parser.parse_args()

    images = {
        size: prepare_icon(args.source_dir / f"QtGitStudio-{size}-source.png", size)
        for size in SIZES
    }
    for size, image in images.items():
        validate_icon(image, size)
        destination = ROOT / f"QtGitStudio-{size}.png"
        image.save(destination, format="PNG", optimize=True)
        print(f"wrote {destination.name}: {image.size[0]}x{image.size[1]} RGBA")
    write_ico(images, ROOT / "QtGitStudio.ico")
    print(f"wrote QtGitStudio.ico: {len(SIZES)} embedded sizes")


if __name__ == "__main__":
    main()
