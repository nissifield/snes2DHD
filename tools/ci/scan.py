"""ROM/blob scanner: rejects ROM data, archives, oversize and stray binary files.

Works on repo-relative paths: scan_file(root, rel) reads root/rel and judges
`rel`, so checkout directory names can't fool the assets/branding rules.
"""
import argparse
import re
import sys
import zipfile
from pathlib import Path
from typing import NamedTuple

ROM_EXTENSIONS = frozenset({".sfc", ".smc", ".fig", ".swc", ".bs", ".srm", ".state"})
ARCHIVE_EXTENSIONS = frozenset({".zip", ".7z", ".rar", ".gz", ".tar"})
IMAGE_EXTENSIONS = frozenset({".png", ".svg"})
UNSUPPORTED_IMAGE_EXTENSIONS = frozenset({".jpg", ".jpeg", ".webp"})
ARCHIVE_MAGICS = (b"PK\x03\x04", b"7z\xbc\xaf\x27\x1c", b"Rar!\x1a\x07", b"\x1f\x8b",
                  b"\xfd7zXZ\x00", b"BZh", b"\x28\xb5\x2f\xfd")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
# Standard PNG chunks only; private chunks can hide data too.
PNG_CHUNKS = frozenset({b"IHDR", b"PLTE", b"IDAT", b"IEND", b"tRNS", b"gAMA",
                        b"cHRM", b"sRGB", b"iCCP", b"sBIT", b"pHYs", b"bKGD",
                        b"tIME", b"tEXt", b"zTXt", b"iTXt"})
LFS_POINTER = b"version https://git-lfs"
HEADER_OFFSETS = (0x7FDC, 0xFFDC, 0x7FDC + 0x200, 0xFFDC + 0x200)
MAP_MODES = frozenset({0x20, 0x21, 0x22, 0x23, 0x25, 0x30, 0x31, 0x32, 0x35})
MIN_HEADER_SIZE = 32 * 1024
SIZE_LIMIT = 1024 * 1024
BRANDING_SIZE_LIMIT = 4 * 1024 * 1024
TEXT_SIZE_LIMIT = 256 * 1024
BLOB_RUN = re.compile(rb"[A-Za-z0-9+/=]{4096,}")
HEX_BYTE = re.compile(rb"0x[0-9A-Fa-f]{2}")
HEX_BYTE_LIMIT = 2048


class Finding(NamedTuple):
    path: str
    rule: str
    detail: str


def _has_snes_header(data: bytes) -> int | None:
    for off in HEADER_OFFSETS:
        if off + 4 <= len(data):
            pair = int.from_bytes(data[off:off + 2], "little") ^ \
                   int.from_bytes(data[off + 2:off + 4], "little")
            if pair == 0xFFFF and data[off - 7] in MAP_MODES \
                    and 0x08 <= data[off - 5] <= 0x0D:
                return off
    return None


def _png_chunks(rel: str, data: bytes) -> list[Finding]:
    """Walk chunks by length field from offset 8; the first IEND must end
    the file exactly and every chunk type must be a standard one."""
    off = len(PNG_MAGIC)
    while off + 8 <= len(data):
        length = int.from_bytes(data[off:off + 4], "big")
        ctype = data[off + 4:off + 8]
        end = off + 12 + length
        if ctype not in PNG_CHUNKS:
            return [Finding(rel, "png-chunk", f"unknown chunk type {ctype!r}")]
        if end > len(data):
            return [Finding(rel, "png-chunk", f"{ctype.decode()} runs past EOF")]
        if ctype == b"IEND":
            if end != len(data):
                return [Finding(rel, "trailing-data", "bytes after IEND chunk")]
            return []
        off = end
    return [Finding(rel, "png-chunk", "no IEND chunk")]


def scan_file(root: Path, rel: str) -> list[Finding]:
    findings = []
    path = Path(root) / rel
    data = path.read_bytes()
    suffix = Path(rel).suffix.lower()
    parts = Path(rel).parts
    in_assets = parts[0] == "assets"
    branding = parts[:2] == ("assets", "branding")

    if suffix in ROM_EXTENSIONS:
        findings.append(Finding(rel, "rom-extension", f"ROM save/state extension {suffix}"))

    if suffix in ARCHIVE_EXTENSIONS or data.startswith(ARCHIVE_MAGICS) \
            or data[257:262] == b"ustar" or zipfile.is_zipfile(path):
        findings.append(Finding(rel, "archive", "archives are not reviewable"))

    if len(data) >= MIN_HEADER_SIZE:
        off = _has_snes_header(data)
        if off is not None:
            findings.append(Finding(rel, "snes-header",
                                    f"checksum/complement pair at {off:#06x}"))

    try:
        data.decode("utf-8")
        is_text = True
    except UnicodeDecodeError:
        is_text = False

    if data.startswith(LFS_POINTER):
        findings.append(Finding(rel, "lfs-pointer", "git-lfs pointer file"))

    if suffix in UNSUPPORTED_IMAGE_EXTENSIONS:
        # shortcut: no JPEG/WebP parser yet, so these are rejected outright
        findings.append(Finding(rel, "unsupported-image",
                                f"no parser for {suffix} yet"))
        limit = SIZE_LIMIT
    elif is_text:
        # text checks apply to every UTF-8 file, SVG included
        if BLOB_RUN.search(re.sub(rb"\s+", b"", data)):
            findings.append(Finding(rel, "encoded-blob",
                                    "run of >=4096 base64/hex characters"))
        if len(HEX_BYTE.findall(data)) > HEX_BYTE_LIMIT:
            findings.append(Finding(rel, "encoded-blob",
                                    f"more than {HEX_BYTE_LIMIT} 0xNN byte tokens"))
        if suffix == ".png":
            findings.append(Finding(rel, "magic-mismatch",
                                    "content does not match .png magic"))
        limit = TEXT_SIZE_LIMIT
    elif suffix == ".png" and data.startswith(PNG_MAGIC):
        findings.extend(_png_chunks(rel, data))
        limit = BRANDING_SIZE_LIMIT if branding else SIZE_LIMIT
    elif suffix in IMAGE_EXTENSIONS:
        findings.append(Finding(rel, "magic-mismatch",
                                f"content does not match {suffix} magic"))
        limit = BRANDING_SIZE_LIMIT if branding else SIZE_LIMIT
    else:
        if in_assets:
            findings.append(Finding(rel, "unknown-binary",
                                    "binary in assets/ is not a known image format"))
        else:
            findings.append(Finding(rel, "binary-outside-assets",
                                    "non-UTF-8 file outside assets/"))
        limit = BRANDING_SIZE_LIMIT if branding else SIZE_LIMIT

    if len(data) > limit:
        findings.append(Finding(rel, "too-large", f"{len(data)} bytes > {limit}"))
    return findings


def scan(root: Path, rels: list[str]) -> list[Finding]:
    findings = []
    for rel in rels:
        findings.extend(scan_file(root, rel))
    return findings


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="directory FILEs are relative to")
    parser.add_argument("files", nargs="+", metavar="FILE")
    args = parser.parse_args(argv)
    findings = scan(Path(args.root), args.files)
    for f in findings:
        print(f"{f.path}: {f.rule}: {f.detail}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
