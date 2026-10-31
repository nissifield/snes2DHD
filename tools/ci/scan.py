"""ROM/blob scanner: rejects ROM data, archives, oversize and stray binary files."""
import sys
from pathlib import Path
from typing import NamedTuple

ROM_EXTENSIONS = frozenset({".sfc", ".smc", ".fig", ".swc", ".bs", ".srm", ".state"})
ARCHIVE_EXTENSIONS = frozenset({".zip", ".7z", ".rar", ".gz", ".tar"})
IMAGE_EXTENSIONS = frozenset({".png", ".jpg", ".webp", ".svg"})
ARCHIVE_MAGICS = (b"PK\x03\x04", b"7z\xbc\xaf\x27\x1c", b"Rar!\x1a\x07", b"\x1f\x8b")
HEADER_OFFSETS = (0x7FDC, 0xFFDC, 0x7FDC + 0x200, 0xFFDC + 0x200)
MIN_HEADER_SIZE = 32 * 1024
SIZE_LIMIT = 1024 * 1024
BRANDING_SIZE_LIMIT = 4 * 1024 * 1024


class Finding(NamedTuple):
    path: str
    rule: str
    detail: str


def scan_file(path: Path) -> list[Finding]:
    findings = []
    name = str(path)
    suffix = path.suffix.lower()
    data = path.read_bytes()

    if suffix in ROM_EXTENSIONS:
        findings.append(Finding(name, "rom-extension", f"ROM save/state extension {suffix}"))

    if suffix in ARCHIVE_EXTENSIONS or data.startswith(ARCHIVE_MAGICS):
        findings.append(Finding(name, "archive", "archives are not reviewable"))

    if len(data) >= MIN_HEADER_SIZE:
        for off in HEADER_OFFSETS:
            if off + 4 <= len(data):
                pair = int.from_bytes(data[off:off + 2], "little") ^ \
                       int.from_bytes(data[off + 2:off + 4], "little")
                if pair == 0xFFFF:
                    findings.append(Finding(name, "snes-header",
                                            f"checksum/complement pair at {off:#06x}"))
                    break

    branding = "assets" in path.parts and "branding" in path.parts
    limit = BRANDING_SIZE_LIMIT if branding else SIZE_LIMIT
    if len(data) > limit:
        findings.append(Finding(name, "too-large", f"{len(data)} bytes > {limit}"))

    if "assets" not in path.parts and suffix not in IMAGE_EXTENSIONS:
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            findings.append(Finding(name, "binary-outside-assets",
                                    "non-UTF-8 file outside assets/"))
    return findings


def scan(paths: list[Path]) -> list[Finding]:
    findings = []
    for path in paths:
        findings.extend(scan_file(path))
    return findings


def main(argv=None) -> int:
    findings = scan([Path(a) for a in (argv if argv is not None else sys.argv[1:])])
    for f in findings:
        print(f"{f.path}: {f.rule}: {f.detail}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
