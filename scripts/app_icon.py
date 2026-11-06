#!/usr/bin/env python3
"""Default Nox icon and the desktop icon packages Aura attaches.

An application icon is assigned by nox.json "icon" or by icon.png beside
the project. Otherwise Aura uses the Nox mark shipped in assets/brand.
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
BRAND_FILES = {
    16: "icon-16.png",
    32: "icon-32.png",
    64: "icon-64.png",
    128: "icon-128.png",
    180: "icon-180.png",
    256: "icon-256.png",
    512: "icon-512.png",
    1024: "icon-1024.png",
}


def brand_dir(root: Path) -> Path:
    return root / "assets" / "brand"


def default_icon(root: Path) -> Path:
    return brand_dir(root) / "icon-1024.png"


def assigned_icon(project: Path) -> Path | None:
    manifest = project / "nox.json"
    if manifest.is_file():
        document = json.loads(manifest.read_text(encoding="utf-8"))
        raw = document.get("icon")
        if isinstance(raw, str) and raw.strip():
            candidate = Path(raw)
            if not candidate.is_absolute():
                candidate = project / candidate
            if candidate.is_file():
                return candidate
    direct = project / "icon.png"
    if direct.is_file():
        return direct
    return None


def resolve_icon(project: Path, root: Path) -> Path:
    chosen = assigned_icon(project)
    if chosen is not None:
        return chosen
    brand = default_icon(root)
    if brand.is_file():
        return brand
    legacy = root / "native" / "ios" / "AppIcon.png"
    return legacy


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    if data[:8] != PNG_SIGNATURE:
        raise ValueError("icon is not a png")
    width = height = 0
    depth = color = 0
    compressed = bytearray()
    offset = 8
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        tag = data[offset + 4 : offset + 8]
        chunk = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if tag == b"IHDR":
            width, height, depth, color, interlace = struct.unpack(">IIBBBBB", chunk)[:5]
            if depth != 8 or color not in {2, 6} or interlace != 0:
                raise ValueError("icon png must be 8-bit rgb or rgba")
        elif tag == b"IDAT":
            compressed.extend(chunk)
        elif tag == b"IEND":
            break
    if width <= 0 or height <= 0 or not compressed:
        raise ValueError("icon png is incomplete")
    channels = 4 if color == 6 else 3
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    rows = []
    cursor = 0
    previous = bytearray(stride)
    for _ in range(height):
        if cursor >= len(raw):
            raise ValueError("icon png rows are truncated")
        filter_kind = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor : cursor + stride])
        cursor += stride
        if len(row) != stride:
            raise ValueError("icon png row is short")
        for index in range(stride):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_kind == 1:
                row[index] = (row[index] + left) & 255
            elif filter_kind == 2:
                row[index] = (row[index] + up) & 255
            elif filter_kind == 3:
                row[index] = (row[index] + ((left + up) // 2)) & 255
            elif filter_kind == 4:
                estimate = left + up - up_left
                distance_left = abs(estimate - left)
                distance_up = abs(estimate - up)
                distance_up_left = abs(estimate - up_left)
                predictor = left
                if distance_up <= distance_left and distance_up <= distance_up_left:
                    predictor = up
                elif distance_up_left <= distance_left:
                    predictor = up_left
                row[index] = (row[index] + predictor) & 255
            elif filter_kind != 0:
                raise ValueError("icon png filter is unsupported")
        previous = row
        rows.append(row)
    rgba = bytearray()
    for row in rows:
        if channels == 4:
            rgba.extend(row)
            continue
        for index in range(0, len(row), 3):
            rgba.extend(row[index : index + 3])
            rgba.append(255)
    return width, height, bytes(rgba)


def encode_png(width: int, height: int, rgba: bytes) -> bytes:
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)
        rows.extend(rgba[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return PNG_SIGNATURE + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + _png_chunk(b"IEND", b"")


def scale_rgba(rgba: bytes, width: int, height: int, target: int) -> bytes:
    if width == target and height == target:
        return rgba
    output = bytearray(target * target * 4)
    if target < width and target < height:
        for y in range(target):
            top = y * height // target
            bottom = max(top + 1, (y + 1) * height // target)
            for x in range(target):
                left = x * width // target
                right = max(left + 1, (x + 1) * width // target)
                total = [0, 0, 0, 0]
                count = 0
                for source_y in range(top, bottom):
                    row = source_y * width * 4
                    for source_x in range(left, right):
                        pixel = row + source_x * 4
                        for channel in range(4):
                            total[channel] += rgba[pixel + channel]
                        count += 1
                dest = (y * target + x) * 4
                for channel in range(4):
                    output[dest + channel] = total[channel] // count
        return bytes(output)
    for y in range(target):
        source_y = min(height - 1, y * height // target)
        for x in range(target):
            source_x = min(width - 1, x * width // target)
            src = (source_y * width + source_x) * 4
            dest = (y * target + x) * 4
            output[dest : dest + 4] = rgba[src : src + 4]
    return bytes(output)


def square_png(path: Path, size: int) -> bytes:
    data = path.read_bytes()
    width, height, rgba = decode_png(data)
    if width == size and height == size:
        return data
    return encode_png(size, size, scale_rgba(rgba, width, height, size))


def write_ico(path: Path, source: Path) -> None:
    width, height, rgba = decode_png(source.read_bytes())
    sizes = [16, 32, 48, 64, 128, 256]
    images = [encode_png(size, size, scale_rgba(rgba, width, height, size)) for size in sizes]
    header = struct.pack("<HHH", 0, 1, len(images))
    entries = bytearray()
    cursor = 6 + 16 * len(images)
    payload = bytearray()
    for size, image in zip(sizes, images):
        dimension = 0 if size == 256 else size
        entries.extend(struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(image), cursor))
        payload.extend(image)
        cursor += len(image)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(entries) + bytes(payload))


def write_icns(path: Path, source: Path) -> None:
    width, height, rgba = decode_png(source.read_bytes())
    chunks = {
        b"icp4": 16,
        b"icp5": 32,
        b"icp6": 64,
        b"ic07": 128,
        b"ic08": 256,
        b"ic09": 512,
        b"ic10": 1024,
        b"ic11": 64,
        b"ic12": 128,
        b"ic13": 512,
        b"ic14": 1024,
    }
    body = bytearray()
    for tag, size in chunks.items():
        image = encode_png(size, size, scale_rgba(rgba, width, height, size))
        body.extend(tag)
        body.extend(struct.pack(">I", 8 + len(image)))
        body.extend(image)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"icns" + struct.pack(">I", 8 + len(body)) + bytes(body))


def xml_text(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def stamp_macos(bundle: Path, name: str, icon: Path, bundle_id: str) -> None:
    resources = bundle / "Contents" / "Resources"
    resources.mkdir(parents=True, exist_ok=True)
    write_icns(resources / "AppIcon.icns", icon)
    plist = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>{xml_text(name)}</string>
	<key>CFBundleIconFile</key>
	<string>AppIcon</string>
	<key>CFBundleIdentifier</key>
	<string>{xml_text(bundle_id)}</string>
	<key>CFBundleName</key>
	<string>{xml_text(name)}</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
"""
    (bundle / "Contents" / "Info.plist").write_text(plist, encoding="utf-8")
    if shutil.which("codesign"):
        subprocess.run(["codesign", "--force", "--sign", "-", str(bundle)], check=False)


def stamp_linux(directory: Path, name: str, icon: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    png = directory / f"{name}.png"
    png.write_bytes(square_png(icon, 512))
    binary = directory / name
    desktop = directory / f"{name}.desktop"
    desktop.write_text(
        "\n".join(
            [
                "[Desktop Entry]",
                "Type=Application",
                f"Name={name}",
                f"Exec={binary}",
                f"Icon={png}",
                "Terminal=false",
                "Categories=Utility;",
                "",
            ]
        ),
        encoding="utf-8",
    )
    desktop.chmod(0o755)


def stamp_windows(directory: Path, name: str, icon: Path) -> None:
    write_ico(directory / f"{name}.ico", icon)


def install_brand(source: Path, root: Path) -> None:
    destination = brand_dir(root)
    destination.mkdir(parents=True, exist_ok=True)
    master = source / "icon-512.png"
    width, height, rgba = decode_png(master.read_bytes())
    for size, name in BRAND_FILES.items():
        official = source / name
        if official.is_file() and size not in {128, 1024}:
            copied_width, copied_height, copied = decode_png(official.read_bytes())
            image = copied if copied_width == size and copied_height == size else scale_rgba(copied, copied_width, copied_height, size)
        else:
            image = scale_rgba(rgba, width, height, size)
        (destination / name).write_bytes(encode_png(size, size, image))


def run_self_test() -> int:
    root = Path(__file__).resolve().parent.parent
    brand = default_icon(root)
    if not brand.is_file():
        print("icon self-test: default Nox icon is missing", file=sys.stderr)
        return 1
    width, height, rgba = decode_png(brand.read_bytes())
    if (width, height) != (1024, 1024):
        print("icon self-test: default icon is not 1024", file=sys.stderr)
        return 1
    center = (512 * width + 512) * 4
    if rgba[center + 2] <= rgba[center]:
        print("icon self-test: default icon is not the Nox mark", file=sys.stderr)
        return 1
    sample = encode_png(2, 2, bytes([10, 20, 200, 255]) * 4)
    scaled_width, scaled_height, scaled = decode_png(encode_png(4, 4, scale_rgba(decode_png(sample)[2], 2, 2, 4)))
    if (scaled_width, scaled_height) != (4, 4) or scaled[2] < 150:
        print("icon self-test: scale did not keep the blue channel", file=sys.stderr)
        return 1
    import tempfile

    with tempfile.TemporaryDirectory() as raw:
        directory = Path(raw)
        project = directory / "app"
        project.mkdir()
        (project / "nox.json").write_text('{"name":"demo","icon":"mark.png"}\n', encoding="utf-8")
        (project / "mark.png").write_bytes(sample)
        if assigned_icon(project) != project / "mark.png":
            print("icon self-test: nox.json icon was ignored", file=sys.stderr)
            return 1
        bare = directory / "bare"
        bare.mkdir()
        (bare / "icon.png").write_bytes(sample)
        if resolve_icon(bare, root) != bare / "icon.png":
            print("icon self-test: icon.png was ignored", file=sys.stderr)
            return 1
        empty = directory / "empty"
        empty.mkdir()
        if resolve_icon(empty, root) != brand:
            print("icon self-test: missing icon did not use the Nox mark", file=sys.stderr)
            return 1
        write_ico(directory / "mark.ico", brand)
        write_icns(directory / "mark.icns", brand)
        ico = (directory / "mark.ico").read_bytes()
        icns = (directory / "mark.icns").read_bytes()
        if ico[:4] != b"\x00\x00\x01\x00" or icns[:4] != b"icns":
            print("icon self-test: desktop icon containers are wrong", file=sys.stderr)
            return 1
        stamp_linux(directory / "linux", "Notes", brand)
        desktop = (directory / "linux" / "Notes.desktop").read_text(encoding="utf-8")
        if "Name=Notes" not in desktop or not (directory / "linux" / "Notes.png").is_file():
            print("icon self-test: linux desktop entry is incomplete", file=sys.stderr)
            return 1
        bundle = directory / "Notes.app"
        (bundle / "Contents" / "MacOS").mkdir(parents=True)
        stamp_macos(bundle, "Notes", brand, "com.noxlang.aura.notes")
        info = (bundle / "Contents" / "Info.plist").read_text(encoding="utf-8")
        if "AppIcon" not in info or not (bundle / "Contents" / "Resources" / "AppIcon.icns").is_file():
            print("icon self-test: macos bundle has no icon", file=sys.stderr)
            return 1
    print("icon self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--install-brand", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    if args.install_brand:
        install_brand(args.install_brand, args.root)
        return 0
    parser.print_help(sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
