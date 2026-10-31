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
IMAGE_EXTENSIONS = frozenset({".png", ".jpg", ".jpeg", ".webp", ".svg"})
ARCHIVE_MAGICS = (b"PK\x03\x04", b"7z\xbc\xaf\x27\x1c", b"Rar!\x1a\x07", b"\x1f\x8b",
                  b"\xfd7zXZ\x00", b"BZh", b"\x28\xb5\x2f\xfd")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
JPEG_MAGIC = b"\xff\xd8\xff"
HEADER_OFFSETS = (0x7FDC, 0xFFDC, 0x7FDC + 0x200, 0xFFDC + 0x200)
MAP_MODES = frozenset({0x20, 0x21, 0x22, 0x23, 0x25, 0x30, 0x31, 0x32, 0x35})
MIN_HEADER_SIZE = 32 * 1024
SIZE_LIMIT = 1024 * 1024
BRANDING_SIZE_LIMIT = 4 * 1024 * 1024
TEXT_SIZE_LIMIT = 256 * 1024
BLOB_RUN = re.compile(rb"[A-Za-z0-9+/=]{4096,}")


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


def _image_magic_ok(suffix: str, data: bytes, is_text: bool) -> bool:
    if suffix == ".png":
        return data.startswith(PNG_MAGIC)
    if suffix in (".jpg", ".jpeg"):
        # shortcut: JPEG trailing data not checked, add EOI parsing if JPEGs appear
        return data.startswith(JPEG_MAGIC)
    if suffix == ".webp":
        return data[:4] == b"RIFF" and data[8:12] == b"WEBP"
    return is_text  # .svg


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

    if suffix in IMAGE_EXTENSIONS:
        if not _image_magic_ok(suffix, data, is_text):
            findings.append(Finding(rel, "magic-mismatch",
                                    f"content does not match {suffix} magic"))
        if suffix == ".png" and data.startswith(PNG_MAGIC):
            iend = data.rfind(b"IEND")
            if iend != -1 and len(data) > iend + 8:
                findings.append(Finding(rel, "trailing-data", "bytes after IEND chunk"))
        limit = BRANDING_SIZE_LIMIT if branding else SIZE_LIMIT
    elif is_text:
        if BLOB_RUN.search(data):
            findings.append(Finding(rel, "encoded-blob",
                                    "run of >=4096 base64/hex characters"))
        limit = TEXT_SIZE_LIMIT
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
