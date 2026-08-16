#!/usr/bin/env python3
"""Write a pre-formatted cartridge image for use with a QIC tape drive.

The layout is 32 sectors of 1024 bytes per segment, of which the last
three carry the ECC (Reed-Solomon parity). The header segment follows
the documented QIC-113 layout.

Usage:
    python docs/mkqictape.py <image> [--format NAME] [--label TEXT]
    python docs/mkqictape.py --list
"""

import argparse
import os
import re
import struct
import sys

# Segment geometry
SECTOR_SIZE     = 1024
SECTORS_PER_SEG = 32
ECC_SECTORS     = 3
DATA_SECTORS    = SECTORS_PER_SEG - ECC_SECTORS      # 29
SEGMENT_SIZE    = SECTOR_SIZE * SECTORS_PER_SEG      # 32768

# The floppy address grid the host lays over the tape. Not a property of
# the cartridge - every driver uses these - but the header states them.
SEGS_PER_CYL  = 4
SEGS_PER_HEAD = 600

# QIC-113 header segment format codes
FT_FMT_CODES = {
    "FT_FMT_NORMAL": 2,   # QIC-80 post rev. B, 205 or 307.5 ft
    "FT_FMT_1100FT": 3,
    "FT_FMT_VAR":    4,   # QIC-80 post rev. B, variable length
    "FT_FMT_425FT":  5,
    "FT_FMT_BIG":    6,   # variable length, and over 65535 segments a tape
}

# Every cartridge the drive offers, in the order the settings dialog lists
# them: name, tracks, segments a track, header format code.
CARTRIDGES = [
    ("Ditto 2 GB",                   72,  502, "FT_FMT_VAR"),
    ("Ditto 3 GB",                   72,  753, "FT_FMT_VAR"),
    ("Ditto 5 GB",                   72, 1256, "FT_FMT_BIG"),
    ("QIC-80, DC-2080 (205 ft)",     28,  100, "FT_FMT_NORMAL"),
    ("QIC-80, DC-2120 (307.5 ft)",   28,  150, "FT_FMT_NORMAL"),
    ("QIC-80, 425 ft",               28,  207, "FT_FMT_425FT"),
    ("QIC-80, 1100 ft",              28,  537, "FT_FMT_1100FT"),
    ("QIC-3010 (255 MB)",            40,  215, "FT_FMT_BIG"),
    ("QIC-3020 (500 MB)",            40,  422, "FT_FMT_BIG"),
    ("Travan TR-1 (400 MB)",         36,  366, "FT_FMT_VAR"),
    ("Travan TR-2 (800 MB)",         40,  540, "FT_FMT_BIG"),
    ("Travan TR-3 (1.6 GB)",         40, 1060, "FT_FMT_BIG"),
]

def cartridges():
    """The table, as name -> (tracks, spt, format code)."""
    out = {}
    for name, trk, spt, fmt in CARTRIDGES:
        if fmt not in FT_FMT_CODES:
            raise SystemExit("unknown format code %s for %s" % (fmt, name))
        out[name] = (trk, spt, FT_FMT_CODES[fmt])
    return out

def pick(cart, want):
    """Matches a cartridge by name, case and punctuation insensitively, so
    that --format tr3 finds "Travan TR-3 (1.6 GB)"."""
    def key(s):
        return re.sub(r"[^a-z0-9]", "", s.lower())

    k = key(want)
    exact = [n for n in cart if key(n) == k]
    if exact:
        return exact[0]

    hits = [n for n in cart if k and k in key(n)]
    if len(hits) == 1:
        return hits[0]
    if len(hits) > 1:
        raise SystemExit("%r matches several cartridges: %s"
                         % (want, ", ".join(sorted(hits))))

    raise SystemExit("no cartridge matches %r. Known: %s"
                     % (want, ", ".join(cart)))

# The byte a FORMAT TRACK lays down. Observed in every command the host
# issues: "4D 03 03 20 EF 6B". It is Colorado's recommended filler, which
# ftape's make-code table also lists.
FILL_BYTE = 0x6B

# ECC field: GF(256) with x^8+x^7+x^2+x+1, and the one multiplier the
# recurrence needs, r^105.
ECC_POLY   = 0x187
ECC_FACTOR = 0xC0

# QIC-113 header segment field offsets (ftape-header-segment.h).
FT_SIGNATURE, FT_FMT_CODE, FT_REV_LEVEL = 0, 4, 5
FT_HSEG_1, FT_HSEG_2, FT_FRST_SEG, FT_LAST_SEG = 6, 8, 10, 12
FT_FMT_DATE, FT_WR_DATE = 14, 18
FT_SPT, FT_TPC, FT_FHM, FT_FTM, FT_FSM = 24, 26, 27, 28, 29
FT_LABEL, FT_LABEL_DATE = 30, 74
FT_LABEL_SZ = FT_LABEL_DATE - FT_LABEL
FT_CMAP_START, FT_FMT_ERROR = 78, 128
FT_INIT_DATE, FT_FMT_CNT, FT_FSL_CNT = 138, 142, 144

# Format code 6 states its segment numbers in four bytes each, at their own
# offsets, because a cartridge that needs it has more than 65535 segments
# and the ordinary two-byte fields cannot hold them (ftape-read.c:445-455).
FT_6_HSEG_1, FT_6_HSEG_2, FT_6_FRST_SEG, FT_6_LAST_SEG = 234, 238, 242, 246

FT_HSEG_MAGIC = 0xAA55AA55


def ecc_table():
    """The r^105 multiply, as a 256-entry table."""
    tab = [0] * 256
    v = ECC_FACTOR
    bit = 1
    while bit < 0x100:
        tab[bit] = v & 0xFF
        v <<= 1
        if v & 0x100:
            v ^= ECC_POLY
        bit <<= 1
    for i in range(3, 0x100):
        low = i & (~i + 1)
        if i != low:
            tab[i] = tab[low] ^ tab[i ^ low]
    return tab


def ecc_parity_for_column(column, tab):
    """The three parity bytes for one byte column:

           p0 = p1 + r^105 * (m - p0)
           p1 = p2 + r^105 * (m - p0)
           p2 =               m - p0
    """
    p0 = p1 = p2 = 0
    for m in column:
        t1 = m ^ p0
        t2 = tab[t1]
        p0 = t2 ^ p1
        p1 = t2 ^ p2
        p2 = t1
    return p0, p1, p2


def build_segment(data, tab):
    """Wraps 29 sectors of data in its three parity sectors."""
    if len(data) != DATA_SECTORS * SECTOR_SIZE:
        raise ValueError("a segment carries exactly %d data bytes"
                         % (DATA_SECTORS * SECTOR_SIZE))

    par = [bytearray(SECTOR_SIZE) for _ in range(ECC_SECTORS)]
    for col in range(SECTOR_SIZE):
        column = data[col::SECTOR_SIZE]
        p0, p1, p2 = ecc_parity_for_column(column, tab)
        par[0][col], par[1][col], par[2][col] = p0, p1, p2

    return bytes(data) + b"".join(bytes(p) for p in par)


def timestamp(year, month, day, hour, minute, second):
    """FT_TIME_STAMP: the year in the top 7 bits, seconds-into-the-year
       below. Month and day are zero based, as the macro's arithmetic
       shows."""
    y = ((year - 1970) << 25) & 0xFE000000
    t = (second + 60 * (minute + 60 * (hour + 24 * (day + 31 * month))))
    return y | (t & 0x01FFFFFF)


def header_segment(spt, tracks, fmt_code, label, tab):
    """The segment the host reads to learn the cartridge geometry."""
    total = spt * tracks
    hdr = bytearray(DATA_SECTORS * SECTOR_SIZE)

    stamp = timestamp(2026, 7, 9, 12, 0, 0)

    struct.pack_into("<I", hdr, FT_SIGNATURE, FT_HSEG_MAGIC)
    hdr[FT_FMT_CODE]  = fmt_code
    hdr[FT_REV_LEVEL] = 0

    # Segment numbering. The wide fields are the ones a format code 6
    # reader looks at; the narrow ones are filled too when they can hold
    # the value, and left alone when they cannot, since a truncated last
    # segment is worse than an absent one.
    if fmt_code == FT_FMT_CODES["FT_FMT_BIG"]:
        struct.pack_into("<I", hdr, FT_6_HSEG_1, 0)
        struct.pack_into("<I", hdr, FT_6_HSEG_2, 1)
        struct.pack_into("<I", hdr, FT_6_FRST_SEG, 2)
        struct.pack_into("<I", hdr, FT_6_LAST_SEG, total - 1)
    elif (total - 1) > 0xFFFF:
        raise SystemExit("%d segments will not fit the narrow header fields; "
                         "this cartridge needs FT_FMT_BIG."
                         % total)

    if (total - 1) <= 0xFFFF:
        struct.pack_into("<H", hdr, FT_HSEG_1, 0)
        struct.pack_into("<H", hdr, FT_HSEG_2, 1)
        struct.pack_into("<H", hdr, FT_FRST_SEG, 2)
        struct.pack_into("<H", hdr, FT_LAST_SEG, total - 1)

    struct.pack_into("<I", hdr, FT_FMT_DATE, stamp)
    struct.pack_into("<I", hdr, FT_WR_DATE, stamp)
    struct.pack_into("<I", hdr, FT_INIT_DATE, stamp)

    struct.pack_into("<H", hdr, FT_SPT, spt)
    hdr[FT_TPC] = tracks

    # The floppy address the last segment falls at, which is how the host
    # checks its own mapping against the tape's.
    hdr[FT_FHM] = ((total - 1) // SEGS_PER_HEAD) & 0xFF
    hdr[FT_FTM] = ((SEGS_PER_HEAD // SEGS_PER_CYL) - 1) & 0xFF
    hdr[FT_FSM] = (SEGS_PER_CYL * SECTORS_PER_SEG) & 0xFF

    text = label.encode("ascii", "replace")[:FT_LABEL_SZ]
    hdr[FT_LABEL:FT_LABEL + len(text)] = text
    hdr[FT_LABEL + len(text):FT_LABEL_DATE] = b" " * (FT_LABEL_SZ - len(text))
    struct.pack_into("<I", hdr, FT_LABEL_DATE, stamp)

    struct.pack_into("<H", hdr, FT_CMAP_START, 0)
    hdr[FT_FMT_ERROR] = 0
    struct.pack_into("<H", hdr, FT_FMT_CNT, 1)
    struct.pack_into("<H", hdr, FT_FSL_CNT, 0)

    # Everything from FT_FSL on is the failed sector log. All zero is an
    # empty list: a cartridge with no bad sectors.
    return build_segment(hdr, tab)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", help="image file to write")
    ap.add_argument("--format", "--capacity", dest="format", default="2",
                    help="which cartridge to make, matching the drive's "
                         "setting. Any unambiguous part of the name will "
                         "do - tr3, dc-2120, 3020 - as will the bare 2, 3 "
                         "or 5 the option used to take. --list shows them.")
    ap.add_argument("--list", action="store_true",
                    help="list the cartridges the drive offers and exit")
    ap.add_argument("--label", default="Blank cartridge",
                    help="tape label written into the header segment")
    args = ap.parse_args()

    cart = cartridges()

    if args.list:
        print("%-30s %6s %9s %14s" % ("cartridge", "tracks", "segments",
                                      "image bytes"))
        for name, (trk, spt, _fmt) in cart.items():
            print("%-30s %6d %9d %14d"
                  % (name, trk, spt, trk * spt * SEGMENT_SIZE))
        return 0

    if args.image is None:
        ap.error("an image file is required unless --list is given")

    name = pick(cart, args.format)
    tracks, spt, fmt_code = cart[name]
    total = spt * tracks
    tab   = ecc_table()

    print("%s: %d tracks of %d segments = %d segments, %d bytes"
          % (name, tracks, spt, total, total * SEGMENT_SIZE))

    header = header_segment(spt, tracks, fmt_code, args.label, tab)
    blank  = build_segment(bytes([FILL_BYTE]) * (DATA_SECTORS * SECTOR_SIZE),
                           tab)

    # Every data segment is the same fill, so its parity is too: build one
    # and repeat it rather than running the recurrence 36000 times.
    chunk = blank * 64

    with open(args.image, "wb") as f:
        f.write(header)          # segment 0 - the header
        f.write(header)          # segment 1 - its duplicate
        left = total - 2
        while left > 0:
            n = min(left, 64)
            f.write(chunk if n == 64 else blank * n)
            left -= n

    print("wrote %s" % args.image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
