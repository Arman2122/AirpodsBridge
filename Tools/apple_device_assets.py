#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""
Fetch the popup's device artwork from Apple's own asset catalogue and convert it.

Apple publishes the renders and animations its proximity-pairing card shows as a
MobileAsset, `com.apple.MobileAsset.SharingDeviceAssets`: one zip per product and colour,
listed in a signed plist on mesu.apple.com. Each zip is a `.devicebundle` with transparent
PNG renders of the buds and the case, and QuickTime movies of the card's animations, HEVC
with a separate alpha layer.

This tool reads that catalogue, downloads the bundles for the products the application
knows, and writes what the popup uses into `Source/Resource/Device/`:

  <Product>/CL_<n>/still.png   the first frame of the card's loop, transparent
  <Product>/CL_<n>/loop.webp   the loop itself, as an animated WebP with alpha
  catalog.json                 what is there, by product and colour, which Apple build, and
                               where in each picture the buds and the case sit ("anchors")
  Device.qrc                   the resource list Qt compiles in

`CL` is Apple's colour code for the variant. It is the same number the device broadcasts as
its colour byte, so the application can pick the user's actual colour.

The movies are the hard part. Their alpha travels as a second HEVC layer, which FFmpeg does
not decode. The base and alpha layers are standard single-layer streams though, so the
tool splits the NAL units by layer, gives each stream an ordinary single-layer VPS, decodes
each with FFmpeg, and puts the frames back together. The colour layer is premultiplied by
the alpha, and is un-premultiplied before it is written.

Usage:
  python Tools/apple_device_assets.py list [--all]
  python Tools/apple_device_assets.py fetch [--product P ...] [--cl N ...]
                                            [--animate default|all|none] [--skip-existing]
  python Tools/apple_device_assets.py check

`fetch` is idempotent: the same Apple build yields byte-identical files, so a refresh that
changes nothing leaves the working tree clean. `check` says whether Apple has published
anything newer than what is committed, without downloading any bundle.

Needs Python 3.10+, Pillow, numpy, and ffmpeg/ffprobe on PATH. Downloads are cached under
`Build/AppleDeviceAssets/`.

Note on integrity: the catalogue carries a `_Measurement` per entry (base64, said to be
SHA-1), but it does not match a plain SHA-1 of the zip as served. Downloads are checked
against `_DownloadSize` and the zip's own CRCs instead.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import plistlib
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile
from collections import Counter
from pathlib import Path

CATALOG_URL = (
    "https://mesu.apple.com/assets/com_apple_MobileAsset_SharingDeviceAssets/"
    "com_apple_MobileAsset_SharingDeviceAssets.xml"
)
ASSET_TYPE = "com.apple.MobileAsset.SharingDeviceAssets"

ROOT = Path(__file__).resolve().parent.parent
OUTPUT_DIR = ROOT / "Source" / "Resource" / "Device"
DEFAULT_CACHE = ROOT / "Build" / "AppleDeviceAssets"

# Conversion parameters. Changing any of these changes every output file, so they are
# constants rather than options: the committed artwork must be reproducible.
LOOP_WIDTH = 480      # the popup's slot is 241 x 106 logical pixels; the frames are sized
LOOP_HEIGHT = 212     # to fit twice that, so they are crisp up to 200 % display scaling
LOOP_FPS = 60         # Apple's own rate; every source frame is kept
LOOP_QUALITY = 80     # WebP quality, lossy
WEBP_METHOD = 4       # encoder effort, 0..6
CROP_PADDING = 12     # pixels kept around the content, at source scale
ALPHA_THRESHOLD = 8   # alpha below this counts as empty when cropping
ANCHOR_GAP = 0.03     # a clear column this wide, as a fraction of the width, separates two objects

# A VPS for an ordinary single-layer stream, taken from a one-frame libx265 encode
# (`ffmpeg -f lavfi -i color=black:s=64x64 -frames:v 1 -c:v libx265 -f hevc`). FFmpeg
# reads the VPS for the id and the layer count only; the SPS carries everything else, so
# one VPS serves every stream the tool splits. Apple's own VPS describes two layers, which
# is what FFmpeg refuses.
SINGLE_LAYER_VPS = bytes.fromhex("40010c01ffff01600000030090000003000003001e959809")

# The card's movies, in order of preference. `loop-charged` is the battery card at rest;
# older bundles have `loop`; `connect-loop` is the pairing card.
LOOP_CANDIDATES = ("ProxCard_loop-charged.mov", "ProxCard_loop.mov", "ProxCard_connect-loop.mov")
# For a bundle with no decodable movie, the still comes from a render instead.
STILL_CANDIDATES = (
    "ProxCard_case-open@3x.png",
    "ProxCard_case-closed-charged@3x.png",
    "ProxCard_case-closed@3x.png",
)


@dataclasses.dataclass(frozen=True)
class Product:
    product_type: str        # Apple's ProductType, without the "-v2" suffix
    name: str                # what the application calls it, for messages
    default_cl: int          # the colour shown when the device's colour byte has no artwork
    every_colour: bool = True  # fetch every colour Apple publishes, or the default only
    animate: bool = True     # give the default colour the loop, or stills only


# Every model the application can decode, plus the Beats it names but does not decode yet.
# Mirrors `Core::AirPods::AppleProductType` in Source/Domain/Base.h; the test suite checks
# that every model has artwork, so the two lists cannot drift apart unnoticed.
PRODUCTS = (
    Product("AirPods1,1", "AirPods 1", 0),
    Product("AirPods1,2", "AirPods 2", 0),
    Product("AirPods1,3", "AirPods 3", 0),
    Product("AirPods1,4", "AirPods 4", 0),
    Product("AirPods1,5", "AirPods 4 (ANC)", 0),
    Product("AirPods3,7", "AirPods 5", 0),
    Product("AirPods3,8", "AirPods 5 (Wireless Case)", 0),
    Product("AirPodsPro1,1", "AirPods Pro", 0),
    Product("AirPodsPro1,2", "AirPods Pro 2, including USB-C", 0),
    Product("AirPodsPro1,3", "AirPods Pro 3", 0),
    Product("AirPodsMax1,1", "AirPods Max", 15),          # Dark Grey
    Product("AirPodsMax1,2", "AirPods Max (USB-C) & Max 2", 18),  # Black
    Product("PowerbeatsPro2,1", "Powerbeats Pro 2", 1),   # Black + all 4 colours + 60 fps loop
    Product("BeatsFitPro1,1", "Beats Fit Pro & Powerbeats Fit", 1),  # Black
    Product("PowerBeats3,1", "Powerbeats 3", 1, every_colour=False, animate=False),
    Product("Powerbeats4,1", "Powerbeats", 1, every_colour=False, animate=False),
    Product("PowerbeatsPro1,1", "Powerbeats Pro", 1, every_colour=False, animate=False),
    Product("BeatsX1,1", "BeatsX", 1, every_colour=False, animate=False),
    Product("BeatsX2,1", "Beats Flex", 1, every_colour=False, animate=False),
    Product("BeatsSolo3,1", "Beats Solo3", 1, every_colour=False, animate=False),
    Product("BeatsSoloPro1,1", "Beats Solo Pro", 1, every_colour=False, animate=False),
    Product("BeatsSolo4,1", "Beats Solo 4", 0, every_colour=False, animate=False),
    Product("BeatsSoloBuds1,1", "Beats Solo Buds", 1, every_colour=False, animate=False),
    Product("BeatsStudio3,2", "Beats Studio3", 1, every_colour=False, animate=False),
    Product("BeatsStudioPro1,1", "Beats Studio Pro", 1, every_colour=False, animate=False),
    Product("BeatsStudioBuds1,1", "Beats Studio Buds", 1, every_colour=False, animate=False),
    Product("BeatsStudioBuds1,2", "Beats Studio Buds +", 0, every_colour=False, animate=False),
    Product("Beats3601,1", "Beats 360", 1, every_colour=False, animate=False),
)


@dataclasses.dataclass
class Entry:
    product_type: str
    cl: int
    variant: str
    compatibility: int
    url: str
    download_size: int
    mastered: str
    build: str

    @property
    def zip_name(self) -> str:
        return self.url.rsplit("/", 1)[-1]


# ------------------------------------------------------------------ catalogue


def log(message: str) -> None:
    print(message, flush=True)


def fetch_catalog(cache: Path) -> dict:
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / "catalog.xml"
    log(f"catalogue: {CATALOG_URL}")
    with urllib.request.urlopen(CATALOG_URL, timeout=60) as response:
        path.write_bytes(response.read())
    with open(path, "rb") as file:
        return plistlib.load(file)


def entries_from(catalog: dict) -> list[Entry]:
    entries = []
    for asset in catalog.get("Assets", []):
        product_type = asset.get("ProductType")
        if not product_type or "CL" not in asset:
            continue
        compatibility = int(asset.get("_CompatibilityVersion", 1))
        if product_type.endswith("-v2"):
            product_type = product_type[: -len("-v2")]
        entries.append(
            Entry(
                product_type=product_type,
                cl=int(asset["CL"]),
                variant=str(asset.get("VariantName", "")),
                compatibility=compatibility,
                url=asset["__BaseURL"] + asset["__RelativePath"],
                download_size=int(asset["_DownloadSize"]),
                mastered=str(asset.get("_MasteredVersion", "")),
                build=str(asset.get("Build", "")),
            )
        )
    return entries


def usable_entries(entries: list[Entry]) -> dict[tuple[str, int], Entry]:
    """The newest alpha-capable bundle per product and colour.

    Only compatibility version 2 carries the HEVC-with-alpha movies and the current renders;
    version 1 is H.264 on a matte and is ignored.
    """
    chosen: dict[tuple[str, int], Entry] = {}
    for entry in entries:
        if entry.compatibility != 2:
            continue
        key = (entry.product_type, entry.cl)
        current = chosen.get(key)
        if current is None or (entry.mastered, entry.build) >= (current.mastered, current.build):
            chosen[key] = entry
    return chosen


def known_products() -> dict[str, Product]:
    return {product.product_type: product for product in PRODUCTS}


# ------------------------------------------------------------------ download


def download(entry: Entry, cache: Path) -> Path:
    zips = cache / "zips"
    zips.mkdir(parents=True, exist_ok=True)
    path = zips / entry.zip_name
    if path.exists() and path.stat().st_size == entry.download_size:
        return path

    log(f"  downloading {entry.download_size / 1e6:.1f} MB")
    partial = path.with_suffix(".part")
    with urllib.request.urlopen(entry.url, timeout=120) as response, open(partial, "wb") as file:
        shutil.copyfileobj(response, file, length=1 << 20)
    if partial.stat().st_size != entry.download_size:
        partial.unlink()
        raise RuntimeError(
            f"{entry.zip_name}: got {partial.stat().st_size} bytes, "
            f"catalogue says {entry.download_size}"
        )
    with zipfile.ZipFile(partial) as archive:
        bad = archive.testzip()
        if bad is not None:
            partial.unlink()
            raise RuntimeError(f"{entry.zip_name}: corrupt member {bad}")
    partial.replace(path)
    return path


def extract_bundle(zip_path: Path, entry: Entry, cache: Path) -> Path:
    """The `.devicebundle` directory, extracted once."""
    target = cache / "bundles" / f"{safe_name(entry.product_type)}-CL_{entry.cl}"
    if target.exists() and any(target.iterdir()):
        return target
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    with zipfile.ZipFile(zip_path) as archive:
        for info in archive.infolist():
            parts = Path(info.filename).parts
            # AssetData/<name>.devicebundle/<file>
            if len(parts) < 3 or parts[0] != "AssetData" or info.is_dir():
                continue
            with archive.open(info) as source, open(target / Path(*parts[2:]).name, "wb") as dest:
                shutil.copyfileobj(source, dest)
    return target


def safe_name(product_type: str) -> str:
    return product_type.replace(",", "_")


# ------------------------------------------------------------------ HEVC layer split


def _boxes(buf: bytes, start: int, end: int):
    pos = start
    while pos + 8 <= end:
        size, kind = struct.unpack(">I4s", buf[pos : pos + 8])
        header = 8
        if size == 1:
            size = struct.unpack(">Q", buf[pos + 8 : pos + 16])[0]
            header = 16
        elif size == 0:
            size = end - pos
        if size < header:
            return
        yield kind, pos + header, pos + size
        pos += size


def _find(buf: bytes, start: int, end: int, path: list[bytes]):
    if not path:
        yield start, end
        return
    for kind, s, e in _boxes(buf, start, end):
        if kind != path[0]:
            continue
        if kind == b"stsd":
            s += 8  # version, flags, entry count
        elif kind in (b"hvc1", b"hev1"):
            s += 78  # visual sample entry fields before the child boxes
        yield from _find(buf, s, e, path[1:])


def _first(buf: bytes, path: list[bytes]) -> tuple[int, int] | None:
    for span in _find(buf, 0, len(buf), path):
        return span
    return None


def _nal_type(nal: bytes) -> int:
    return (nal[0] >> 1) & 0x3F


def _nal_layer(nal: bytes) -> int:
    return ((nal[0] & 1) << 5) | (nal[1] >> 3)


def _as_layer0(nal: bytes) -> bytes:
    header = bytearray(nal[:2])
    header[0] &= 0xFE
    header[1] &= 0x07
    return bytes(header) + nal[2:]


@dataclasses.dataclass
class SplitMovie:
    base: bytes    # Annex-B stream, the colour layer
    alpha: bytes   # Annex-B stream, the alpha layer, or empty
    fps: float


def split_layers(movie: Path) -> SplitMovie:
    data = movie.read_bytes()
    trak = [b"moov", b"trak", b"mdia", b"minf", b"stbl"]

    # Frame rate, from the media header and the sample durations.
    mdhd = _first(data, [b"moov", b"trak", b"mdia", b"mdhd"])
    stts = _first(data, [*trak, b"stts"])
    if mdhd is None or stts is None:
        raise RuntimeError(f"{movie.name}: not a QuickTime movie I understand")
    version = data[mdhd[0]]
    timescale = struct.unpack(">I", data[mdhd[0] + (20 if version == 1 else 12) :][:4])[0]
    count = struct.unpack(">I", data[stts[0] + 4 : stts[0] + 8])[0]
    deltas = Counter()
    for i in range(count):
        n, delta = struct.unpack(">II", data[stts[0] + 8 + 8 * i : stts[0] + 16 + 8 * i])
        deltas[delta] += n
    fps = timescale / deltas.most_common(1)[0][0]

    # Parameter sets, per layer. Apple's VPS is dropped; the synthetic one goes first.
    streams = {0: bytearray(b"\x00\x00\x00\x01" + SINGLE_LAYER_VPS), 1: bytearray(b"\x00\x00\x00\x01" + SINGLE_LAYER_VPS)}
    seen_parameter_sets: set[bytes] = set()

    def add(nal: bytes) -> None:
        kind, layer = _nal_type(nal), _nal_layer(nal)
        if layer > 1:
            return
        if kind == 32 or kind in (35, 39, 40):  # VPS, AUD, SEI
            return
        if kind in (33, 34):  # SPS, PPS
            if nal in seen_parameter_sets:
                return
            seen_parameter_sets.add(nal)
        elif kind >= 32:
            return
        streams[layer] += b"\x00\x00\x00\x01" + _as_layer0(nal)

    hvcc = _first(data, [*trak, b"stsd", b"hvc1", b"hvcC"]) or _first(
        data, [*trak, b"stsd", b"hev1", b"hvcC"]
    )
    if hvcc is None:
        raise RuntimeError(f"{movie.name}: no HEVC configuration; not an hvc1/hev1 track")
    hv = data[hvcc[0] : hvcc[1]]
    length_size = (hv[21] & 3) + 1
    arrays = hv[22]
    pos = 23
    for _ in range(arrays):
        n = struct.unpack(">H", hv[pos + 1 : pos + 3])[0]
        pos += 3
        for _ in range(n):
            size = struct.unpack(">H", hv[pos : pos + 2])[0]
            pos += 2
            add(hv[pos : pos + size])
            pos += size

    # Samples: sizes, chunk layout, chunk offsets.
    stsz = _first(data, [*trak, b"stsz"])
    stsc = _first(data, [*trak, b"stsc"])
    stco = _first(data, [*trak, b"stco"])
    co64 = _first(data, [*trak, b"co64"])
    if stsz is None or stsc is None or (stco is None and co64 is None):
        raise RuntimeError(f"{movie.name}: sample tables missing")

    fixed, count = struct.unpack(">II", data[stsz[0] + 4 : stsz[0] + 12])
    sizes = [fixed] * count if fixed else list(struct.unpack(f">{count}I", data[stsz[0] + 12 : stsz[0] + 12 + 4 * count]))

    n = struct.unpack(">I", data[stsc[0] + 4 : stsc[0] + 8])[0]
    chunk_table = [struct.unpack(">III", data[stsc[0] + 8 + 12 * i : stsc[0] + 20 + 12 * i]) for i in range(n)]

    if stco is not None:
        n = struct.unpack(">I", data[stco[0] + 4 : stco[0] + 8])[0]
        offsets = list(struct.unpack(f">{n}I", data[stco[0] + 8 : stco[0] + 8 + 4 * n]))
    else:
        n = struct.unpack(">I", data[co64[0] + 4 : co64[0] + 8])[0]
        offsets = list(struct.unpack(f">{n}Q", data[co64[0] + 8 : co64[0] + 8 + 8 * n]))

    sample = 0
    for index, chunk_offset in enumerate(offsets):
        per_chunk = 0
        for first_chunk, samples_per_chunk, _ in chunk_table:
            if first_chunk <= index + 1:
                per_chunk = samples_per_chunk
        pos = chunk_offset
        for _ in range(per_chunk):
            if sample >= len(sizes):
                break
            end = pos + sizes[sample]
            while pos + length_size <= end:
                size = int.from_bytes(data[pos : pos + length_size], "big")
                pos += length_size
                add(data[pos : pos + size])
                pos += size
            pos = end
            sample += 1

    has_alpha = any(_nal_type(nal) < 32 for nal in _annexb(bytes(streams[1])))
    return SplitMovie(bytes(streams[0]), bytes(streams[1]) if has_alpha else b"", fps)


def _annexb(stream: bytes):
    pos = 0
    while True:
        start = stream.find(b"\x00\x00\x00\x01", pos)
        if start < 0:
            return
        nxt = stream.find(b"\x00\x00\x00\x01", start + 4)
        yield stream[start + 4 : nxt if nxt >= 0 else len(stream)]
        if nxt < 0:
            return
        pos = nxt


# ------------------------------------------------------------------ decode and compose


def require_ffmpeg() -> None:
    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            sys.exit(f"{tool} is not on PATH; install FFmpeg (any recent build) and retry")


def _stream_size(path: Path) -> tuple[int, int]:
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "csv=p=0", str(path)],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    width, height = (int(v) for v in out.split(",")[:2])
    return width, height


def _decoder(path: Path, pix_fmt: str) -> subprocess.Popen:
    return subprocess.Popen(
        ["ffmpeg", "-v", "error", "-nostdin", "-f", "hevc", "-i", str(path), "-f", "rawvideo", "-pix_fmt", pix_fmt, "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=1 << 22,
    )


def _frames(process: subprocess.Popen, frame_bytes: int):
    while True:
        chunk = process.stdout.read(frame_bytes)
        if len(chunk) < frame_bytes:
            break
        yield chunk
    process.stdout.close()
    process.wait()
    if process.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {process.stderr.read().decode(errors='replace').strip()[:400]}")


@dataclasses.dataclass
class Converted:
    frames: list          # PIL images, RGBA, already cropped and scaled
    source: str           # which file in the bundle
    source_fps: float
    alpha_mode: str       # "premultiplied" or "straight", as the colour layer was stored


def _detect_alpha_mode(rgb, alpha, bg) -> str:
    """Detect how the colour layer was stored against its alpha channel.

    Apple's bundles use three conventions:
      - `premultiplied`: colour scaled by alpha over a black matte (classic AirPods loops)
      - `premultiplied-white`: colour blended over a white matte (`C = C_true * a + 255 * (1 - a)`,
        used by newer AirPods 5 / 2026 SharingDeviceAssets bundles)
      - `straight`: un-premultiplied colour (Beats loops)
    """
    luminance = rgb.mean(axis=2)
    edge = (alpha > 40) & (alpha < 120)
    inside = alpha == 255
    if bg.min() > 230.0:
        if not edge.any() or not inside.any():
            return "premultiplied-white"
        unmatted = 255.0 - (255.0 - luminance[edge]) * 255.0 / alpha[edge]
        if (unmatted < -16.0).mean() < 0.05 and luminance[edge].mean() >= luminance[inside].mean():
            return "premultiplied-white"
        return "straight"

    if not edge.any() or not inside.any():
        return "premultiplied"
    unpremultiplied_edge = (luminance[edge] * 255.0 / alpha[edge]).mean()
    return "premultiplied" if unpremultiplied_edge < 1.5 * max(1.0, luminance[inside].mean()) else "straight"


def convert_movie(movie: Path, work: Path) -> Converted:
    import numpy as np
    from PIL import Image

    split = split_layers(movie)
    if not split.alpha:
        raise RuntimeError(f"{movie.name}: no alpha layer")
    work.mkdir(parents=True, exist_ok=True)
    base_path, alpha_path = work / "base.hevc", work / "alpha.hevc"
    base_path.write_bytes(split.base)
    alpha_path.write_bytes(split.alpha)

    width, height = _stream_size(base_path)
    if _stream_size(alpha_path) != (width, height):
        raise RuntimeError(f"{movie.name}: alpha layer is a different size from the colour layer")

    # Pass one: the content's bounding box across the whole loop, from the alpha alone.
    box = None
    total = 0
    for chunk in _frames(_decoder(alpha_path, "gray"), width * height):
        total += 1
        mask = np.frombuffer(chunk, np.uint8).reshape(height, width) > ALPHA_THRESHOLD
        rows, cols = np.flatnonzero(mask.any(axis=1)), np.flatnonzero(mask.any(axis=0))
        if rows.size == 0:
            continue
        frame_box = (cols[0], rows[0], cols[-1] + 1, rows[-1] + 1)
        box = frame_box if box is None else (
            min(box[0], frame_box[0]), min(box[1], frame_box[1]), max(box[2], frame_box[2]), max(box[3], frame_box[3]),
        )
    if box is None:
        raise RuntimeError(f"{movie.name}: alpha layer is empty")
    box = (max(0, box[0] - CROP_PADDING), max(0, box[1] - CROP_PADDING), min(width, box[2] + CROP_PADDING), min(height, box[3] + CROP_PADDING))

    # Pass two: colour and alpha in lockstep; crop, un-premultiply where the layer was
    # premultiplied, scale, and keep every n-th frame.
    step = max(1, round(split.fps / LOOP_FPS))
    crop_w, crop_h = box[2] - box[0], box[3] - box[1]
    scale = min(LOOP_WIDTH / crop_w, LOOP_HEIGHT / crop_h)
    target = (max(1, round(crop_w * scale)), max(1, round(crop_h * scale)))
    frames = []
    base_frames = _frames(_decoder(base_path, "rgb24"), width * height * 3)
    alpha_frames = _frames(_decoder(alpha_path, "gray"), width * height)
    index = 0
    alpha_mode = None
    bg = None
    misaligned = 0
    checked = 0
    for rgb_bytes, alpha_bytes in zip(base_frames, alpha_frames):
        if index % step == 0:
            rgb = np.frombuffer(rgb_bytes, np.uint8).reshape(height, width, 3)[box[1]:box[3], box[0]:box[2]].astype(np.float32)
            alpha = np.frombuffer(alpha_bytes, np.uint8).reshape(height, width)[box[1]:box[3], box[0]:box[2]]
            if alpha_mode is None:
                empty = alpha == 0
                bg = np.median(rgb[empty], axis=0) if empty.any() else np.zeros(3, dtype=np.float32)
                alpha_mode = _detect_alpha_mode(rgb, alpha, bg)
            if alpha_mode != "straight" and index % (step * 15) == 0:
                # The two layers are decoded separately. In premultiplied streams, colour well
                # outside the alpha matte means the frames drifted apart, which would show as
                # ghosting on every loop. Straight streams may carry an unmasked backdrop where
                # alpha == 0.
                checked += 1
                stray = np.count_nonzero((np.abs(rgb - bg).max(axis=2) > 24) & (alpha == 0))
                if stray > 0.02 * max(1, np.count_nonzero(alpha)):
                    misaligned += 1
            if alpha_mode == "premultiplied":
                factor = np.where(alpha > 0, 255.0 / np.maximum(alpha, 1), 0.0)[..., None]
                rgb = np.clip(rgb * factor, 0, 255)
            elif alpha_mode == "premultiplied-white":
                factor = np.where(alpha > 0, 255.0 / np.maximum(alpha, 1), 0.0)[..., None]
                rgb = np.where(alpha[..., None] > 0, np.clip(255.0 - (255.0 - rgb) * factor, 0, 255), 0.0)
            rgba = np.dstack([rgb, alpha]).astype(np.uint8)
            image = Image.fromarray(rgba, "RGBA").resize(target, Image.LANCZOS)
            frames.append(image)
        index += 1
    if index != total:
        raise RuntimeError(f"{movie.name}: colour layer has {index} frames, alpha layer {total}")
    if misaligned:
        raise RuntimeError(f"{movie.name}: colour and alpha layers disagree on {misaligned} of {checked} checked frames")
    return Converted(frames, movie.name, split.fps, alpha_mode)


def convert_variant(bundle: Path, out_dir: Path, animate: bool, work: Path) -> dict:
    """Write still.png and, if asked, loop.webp; return the catalogue record."""
    from PIL import Image

    out_dir.mkdir(parents=True, exist_ok=True)
    movie = next((bundle / name for name in LOOP_CANDIDATES if (bundle / name).exists()), None)
    record: dict = {}
    if movie is not None:
        converted = convert_movie(movie, work)
        still = converted.frames[0]
        record["from"] = converted.source
        record["alpha"] = converted.alpha_mode
        log(f"  {converted.source}: {len(converted.frames)} frames kept, colour {converted.alpha_mode}")
        if animate:
            loop_path = out_dir / "loop.webp"
            # WebP stores whole milliseconds per frame. Rounding each frame's end time rather
            # than its length makes the delays alternate 16 and 17 at 60 fps, so the loop
            # keeps Apple's exact length instead of drifting by a frame in every twenty.
            durations = [
                round((i + 1) * 1000 / LOOP_FPS) - round(i * 1000 / LOOP_FPS)
                for i in range(len(converted.frames))
            ]
            still.save(
                loop_path, format="WEBP", save_all=True, append_images=converted.frames[1:],
                duration=durations, loop=0, quality=LOOP_QUALITY, method=WEBP_METHOD,
                lossless=False, exact=False,
            )
            record["loop"] = "loop.webp"
            record["fps"] = LOOP_FPS
            record["frames"] = len(converted.frames)
        else:
            (out_dir / "loop.webp").unlink(missing_ok=True)
    else:
        render = next((bundle / name for name in STILL_CANDIDATES if (bundle / name).exists()), None)
        if render is None:
            raise RuntimeError(f"{bundle.name}: neither a movie nor a render to make a still from")
        still = Image.open(render).convert("RGBA")
        scale = min(1.0, LOOP_WIDTH / still.width, LOOP_HEIGHT / still.height)
        if scale < 1.0:
            still = still.resize((max(1, round(still.width * scale)), max(1, round(still.height * scale))), Image.LANCZOS)
        record["from"] = render.name
        (out_dir / "loop.webp").unlink(missing_ok=True)

    still_path = out_dir / "still.png"
    still.save(still_path, format="PNG", optimize=True)
    record["still"] = "still.png"
    record["size"] = [still.width, still.height]
    return record


def anchors_of(still_path: Path) -> list[float]:
    """Where the things in a still are, left to right, as fractions of its width.

    A pair of buds and its case are two objects with clear air between them; a headphone
    is one. The picture is split at the widest run of empty columns inside the content,
    if that run is wide enough to be a gap rather than the space between two buds, and
    each object's anchor is the centre of its extent. The popup puts the battery rings
    under these, so they follow Apple's composition rather than a fixed position.
    """
    from PIL import Image
    import numpy as np

    with Image.open(still_path) as image:
        alpha = np.array(image.convert("RGBA"))[:, :, 3] > ALPHA_THRESHOLD
    width = alpha.shape[1]
    filled = alpha.any(axis=0)
    columns = np.flatnonzero(filled)
    if len(columns) == 0:
        return [0.5]
    first, last = int(columns[0]), int(columns[-1])

    widest = (0, first, first)  # (length, start, end) of the widest empty run inside
    start = None
    for x in range(first, last + 2):
        empty = x <= last and not filled[x]
        if empty and start is None:
            start = x
        elif not empty and start is not None:
            if x - start > widest[0]:
                widest = (x - start, start, x - 1)
            start = None

    def centre(a: int, b: int) -> float:
        return round((a + b) / 2 / width, 3)

    if widest[0] >= width * ANCHOR_GAP:
        left = np.flatnonzero(filled[: widest[1]])
        right = np.flatnonzero(filled[widest[2] + 1 :]) + widest[2] + 1
        return [centre(int(left[0]), int(left[-1])), centre(int(right[0]), int(right[-1]))]
    return [centre(first, last)]


def annotate_anchors(manifest: dict) -> None:
    """Fill in every variant's anchors from its still on disk; deterministic, so cheap to redo."""
    for product in manifest["products"].values():
        for variant in product["variants"].values():
            variant["anchors"] = anchors_of(OUTPUT_DIR / variant["still"])


# ------------------------------------------------------------------ manifest


def load_manifest() -> dict:
    path = OUTPUT_DIR / "catalog.json"
    if not path.exists():
        return {}
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def write_manifest(manifest: dict) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    (OUTPUT_DIR / "catalog.json").write_text(text, encoding="utf-8", newline="\n")

    files = ["catalog.json"]
    for product in manifest["products"].values():
        for variant in product["variants"].values():
            for key in ("still", "loop"):
                if key in variant:
                    files.append(variant[key])
    lines = ["<RCC>", '    <qresource prefix="/Resource/Device">']
    lines += [f"        <file>{name}</file>" for name in files]
    lines += ["    </qresource>", "</RCC>", ""]
    (OUTPUT_DIR / "Device.qrc").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def prune(manifest: dict) -> None:
    """Remove artwork directories the manifest no longer names. Other files are left alone."""
    wanted = set()
    for product_type, product in manifest["products"].items():
        for cl in product["variants"]:
            wanted.add((safe_name(product_type), f"CL_{cl}"))
    for product_dir in OUTPUT_DIR.iterdir():
        if not product_dir.is_dir():
            continue
        for variant_dir in product_dir.iterdir():
            if variant_dir.is_dir() and variant_dir.name.startswith("CL_") and (product_dir.name, variant_dir.name) not in wanted:
                shutil.rmtree(variant_dir)
                log(f"removed {variant_dir.relative_to(OUTPUT_DIR)}")
        if not any(product_dir.iterdir()):
            product_dir.rmdir()


# ------------------------------------------------------------------ commands


def cmd_list(args) -> int:
    entries = entries_from(fetch_catalog(args.cache))
    known = known_products()
    rows = [e for e in entries if args.all or (e.product_type in known and e.compatibility == 2)]
    rows.sort(key=lambda e: (e.product_type, e.cl, e.compatibility))
    print(f"{'ProductType':22} {'CL':>3} {'Variant':26} {'v':>2} {'MB':>6}  mastered")
    for e in rows:
        print(f"{e.product_type:22} {e.cl:>3} {e.variant:26} {e.compatibility:>2} {e.download_size / 1e6:6.1f}  {e.mastered}")
    print(f"{len(rows)} entries")
    return 0


def cmd_check(args) -> int:
    manifest = load_manifest()
    if not manifest:
        print("nothing committed yet: run fetch")
        return 1
    catalog = fetch_catalog(args.cache)
    usable = usable_entries(entries_from(catalog))
    builds = {e.build for e in usable.values()}
    changes = []
    if manifest.get("build") not in builds:
        changes.append(f"Apple build is now {', '.join(sorted(builds))}; committed artwork is from {manifest.get('build')}")
    for product_type, product in manifest["products"].items():
        for cl, variant in product["variants"].items():
            entry = usable.get((product_type, int(cl)))
            if entry is None:
                changes.append(f"{product_type} CL {cl}: no longer published")
            elif entry.mastered != variant.get("mastered"):
                changes.append(f"{product_type} CL {cl}: mastered {entry.mastered}, committed {variant.get('mastered')}")
        policy = known_products().get(product_type)
        if policy is not None and policy.every_colour:
            published = sorted(cl for (pt, cl) in usable if pt == product_type)
            missing = [cl for cl in published if str(cl) not in product["variants"]]
            if missing:
                changes.append(f"{product_type}: colours not fetched: {missing}")
    if not changes:
        print(f"up to date with Apple build {manifest.get('build')}")
        return 0
    print("\n".join(changes))
    return 1


def cmd_fetch(args) -> int:
    require_ffmpeg()
    known = known_products()
    wanted_products = args.product or [p.product_type for p in PRODUCTS]
    for product_type in wanted_products:
        if product_type not in known:
            sys.exit(f"unknown product {product_type}; add it to PRODUCTS and to Core::AirPods::AppleProductType")

    catalog = fetch_catalog(args.cache)
    usable = usable_entries(entries_from(catalog))
    manifest = load_manifest() or {"generator": "Tools/apple_device_assets.py", "asset": ASSET_TYPE, "build": "", "products": {}}
    manifest["generator"] = "Tools/apple_device_assets.py"
    manifest["asset"] = ASSET_TYPE
    manifest["loopWidth"] = LOOP_WIDTH

    builds = set()
    for product_type in wanted_products:
        product = known[product_type]
        published = sorted(cl for (pt, cl) in usable if pt == product_type)
        if not published:
            sys.exit(f"{product_type}: Apple publishes no alpha-capable bundle for it")
        default_cl = product.default_cl if product.default_cl in published else published[0]
        if default_cl != product.default_cl:
            log(f"{product_type}: default colour {product.default_cl} is not published, using {default_cl}")
        if args.cl:
            wanted_cls = [cl for cl in args.cl if cl in published]
        else:
            wanted_cls = list(published) if product.every_colour else []
        if default_cl not in wanted_cls:
            wanted_cls.append(default_cl)

        previous = manifest["products"].get(product_type, {})
        variants = dict(previous.get("variants", {}))
        for cl in sorted(wanted_cls):
            entry = usable[(product_type, cl)]
            builds.add(entry.build)
            animate = args.animate == "all" or (
                args.animate == "default" and cl == default_cl and product.animate
            )
            out_dir = OUTPUT_DIR / safe_name(product_type) / f"CL_{cl}"
            existing = variants.get(str(cl), {})
            if (
                args.skip_existing and existing.get("mastered") == entry.mastered
                and (out_dir / "still.png").exists() and (animate == (out_dir / "loop.webp").exists())
            ):
                log(f"{product_type} CL {cl} ({entry.variant}): up to date")
                continue
            log(f"{product_type} CL {cl} ({entry.variant}){' + animation' if animate else ''}")
            bundle = extract_bundle(download(entry, args.cache), entry, args.cache)
            work = args.cache / "work" / f"{safe_name(product_type)}-CL_{cl}"
            converted = convert_variant(bundle, out_dir, animate, work)
            shutil.rmtree(work, ignore_errors=True)
            prefix = f"{safe_name(product_type)}/CL_{cl}/"
            variant = {"name": entry.variant, "mastered": entry.mastered, "from": converted["from"]}
            if "alpha" in converted:
                variant["alpha"] = converted["alpha"]
            variant["still"] = prefix + converted["still"]
            variant["size"] = converted["size"]
            if "loop" in converted:
                variant["loop"] = prefix + converted["loop"]
                variant["fps"] = converted["fps"]
                variant["frames"] = converted["frames"]
            variants[str(cl)] = variant
        manifest["products"][product_type] = {
            "name": product.name,
            "default": default_cl,
            "mastered": max((v.get("mastered", "") for v in variants.values()), default=""),
            "variants": {k: variants[k] for k in sorted(variants, key=int)},
        }

    # Stable order: the application's own product order, then colour.
    ordered = {p.product_type: manifest["products"][p.product_type] for p in PRODUCTS if p.product_type in manifest["products"]}
    manifest["products"] = ordered
    if builds:
        manifest["build"] = sorted(builds)[-1]
    annotate_anchors(manifest)
    write_manifest(manifest)
    if not args.keep_stale:
        prune(manifest)
    log(f"wrote {OUTPUT_DIR.relative_to(ROOT)}: {sum(len(p['variants']) for p in ordered.values())} variants across {len(ordered)} products")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE, help="where downloads are kept (default: Build/AppleDeviceAssets)")
    commands = parser.add_subparsers(dest="command", required=True)

    listing = commands.add_parser("list", help="show what Apple publishes")
    listing.add_argument("--all", action="store_true", help="every entry, not just the products the application knows")
    listing.set_defaults(run=cmd_list)

    check = commands.add_parser("check", help="compare the committed artwork with Apple's catalogue")
    check.set_defaults(run=cmd_check)

    fetch = commands.add_parser("fetch", help="download, convert and write the artwork")
    fetch.add_argument("--product", action="append", help="ProductType to fetch (repeatable; default: all known)")
    fetch.add_argument("--cl", action="append", type=int, help="colour code to fetch (repeatable; default: every published colour)")
    fetch.add_argument("--animate", choices=("default", "all", "none"), default="default", help="which colours get the loop animation (default: the default colour only)")
    fetch.add_argument("--skip-existing", action="store_true", help="leave variants alone whose Apple version has not changed")
    fetch.add_argument("--keep-stale", action="store_true", help="do not delete artwork directories the manifest no longer names")
    fetch.set_defaults(run=cmd_fetch)

    args = parser.parse_args(argv)
    try:
        return args.run(args)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
