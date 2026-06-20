#!/usr/bin/env python3
"""Pack NAM models, cabinet IRs, and presets into a QSPI data-partition image.

The resulting `data_image.bin` is flashed to the pedal's onboard QSPI flash with
tools/flash_data.sh (via dfu-util). The firmware reads it in place, memory-mapped.

The binary layout mirrors data_format.h exactly:

    NamDataHeader   magic(u32) version(u16) count(u16)            @ partition base
    NamDataEntry[]  type(u8) name[31] offset(u32) length(u32)
                    samplerate(u32) reserved(u32)                 (48 bytes each)
    ... blobs, each 4 KiB-aligned ...

Inputs from the source folder:
    *.namb          -> model entries (raw bytes)
    *.wav           -> IR entries (converted to mono float32 little-endian taps)
    presets.json    -> optional list of preset entries (see --help / README)

Usage:
    python3 build_data_image.py SRC_DIR [-o data_image.bin] [--max-taps N]
"""

import argparse
import json
import os
import struct
import sys

# --- constants mirrored from data_format.h ---------------------------------
MAGIC = 0x444D414E  # 'N','A','M','D'
VERSION = 1
SECTOR_SIZE = 4096
NAME_LEN = 31  # includes null terminator
PARTITION_SIZE = 0x00600000  # 6 MiB

ENTRY_MODEL = 0
ENTRY_IR = 1
ENTRY_PRESET = 2

HEADER_FMT = "<IHH"  # magic, version, count  -> 8 bytes
ENTRY_FMT = "<B31sIIII"  # type, name, offset, length, samplerate, reserved -> 48
PRESET_FMT = "<31s31sffB3x6f"  # model, ir, in_gain, out_vol, bypass, pad, 6×EQ -> 98 bytes

assert struct.calcsize(HEADER_FMT) == 8
assert struct.calcsize(ENTRY_FMT) == 48


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(1)


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def encode_name(name):
    """Truncate to fit a null-terminated NAME_LEN field and encode as ASCII."""
    raw = name.encode("ascii", "replace")[: NAME_LEN - 1]
    return raw + b"\x00" * (NAME_LEN - len(raw))


# --- minimal WAV reader (no numpy dependency) ------------------------------
def read_wav_mono_float(path):
    """Return (samples_as_list_of_float, sample_rate).

    Supports PCM 16/24/32-bit and IEEE float 32-bit, mono or multi-channel
    (channels are averaged to mono). Samples are normalized to roughly [-1, 1].
    """
    with open(path, "rb") as f:
        data = f.read()

    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        die(f"{path}: not a RIFF/WAVE file")

    fmt = None
    samples_bytes = None
    pos = 12
    while pos + 8 <= len(data):
        chunk_id = data[pos : pos + 4]
        (chunk_size,) = struct.unpack_from("<I", data, pos + 4)
        body = data[pos + 8 : pos + 8 + chunk_size]
        if chunk_id == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)  # tag,ch,rate,byterate,align,bits
        elif chunk_id == b"data":
            samples_bytes = body
        pos += 8 + chunk_size + (chunk_size & 1)  # chunks are word-aligned

    if fmt is None or samples_bytes is None:
        die(f"{path}: missing fmt or data chunk")

    audio_format, channels, rate, _byterate, _align, bits = fmt
    # audio_format: 1 = PCM int, 3 = IEEE float, 0xFFFE = extensible (assume PCM int)
    bytes_per_sample = bits // 8
    frame_count = len(samples_bytes) // (bytes_per_sample * channels)

    out = []
    for frame in range(frame_count):
        acc = 0.0
        base = frame * bytes_per_sample * channels
        for ch in range(channels):
            off = base + ch * bytes_per_sample
            chunk = samples_bytes[off : off + bytes_per_sample]
            if audio_format == 3 and bits == 32:
                (v,) = struct.unpack("<f", chunk)
            elif bits == 16:
                (i,) = struct.unpack("<h", chunk)
                v = i / 32768.0
            elif bits == 24:
                i = int.from_bytes(chunk, "little", signed=True)
                v = i / 8388608.0
            elif bits == 32:  # 32-bit PCM int
                (i,) = struct.unpack("<i", chunk)
                v = i / 2147483648.0
            elif bits == 8:  # 8-bit PCM is unsigned
                v = (chunk[0] - 128) / 128.0
            else:
                die(f"{path}: unsupported bit depth {bits}")
            acc += v
        out.append(acc / channels)
    return out, rate


def gather_blobs(src_dir, max_taps):
    """Return a list of (type, name, blob_bytes, samplerate)."""
    entries = []
    for fname in sorted(os.listdir(src_dir)):
        path = os.path.join(src_dir, fname)
        if not os.path.isfile(path):
            continue
        stem, ext = os.path.splitext(fname)
        ext = ext.lower()

        if ext == ".namb":
            with open(path, "rb") as f:
                blob = f.read()
            entries.append((ENTRY_MODEL, stem, blob, 0))
            print(f"  model  {stem:<24} {len(blob):>8} bytes")

        elif ext == ".wav":
            samples, rate = read_wav_mono_float(path)
            if max_taps and len(samples) > max_taps:
                print(
                    f"  ir     {stem:<24} trimming {len(samples)} -> {max_taps} taps"
                )
                samples = samples[:max_taps]
            blob = struct.pack("<%df" % len(samples), *samples)
            entries.append((ENTRY_IR, stem, blob, rate))
            print(f"  ir     {stem:<24} {len(samples):>5} taps @ {rate} Hz")

    return entries


def pack_preset(p):
    eq = p.get("eq", {})
    return struct.pack(
        PRESET_FMT,
        encode_name(p.get("model", "")),
        encode_name(p.get("ir", "")),
        float(p.get("input_gain", 1.0)),
        float(p.get("output_volume", 0.85)),
        1 if p.get("bypass", False) else 0,
        float(eq.get("bass_gain", 0.0)),
        float(eq.get("mid_gain", 0.0)),
        float(eq.get("treble_gain", 0.0)),
        float(eq.get("bass_freq", 100.0)),
        float(eq.get("mid_freq", 750.0)),
        float(eq.get("treble_freq", 4000.0)),
    )


def gather_presets(src_dir):
    """Return preset entries from an optional presets.json."""
    path = os.path.join(src_dir, "presets.json")
    if not os.path.isfile(path):
        return []
    with open(path) as f:
        presets = json.load(f)

    entries = []
    for p in presets:
        blob = pack_preset(p)
        name = p.get("name", "preset")
        entries.append((ENTRY_PRESET, name, blob, 0))
        print(f"  preset {name:<24} model={p.get('model','')} ir={p.get('ir','')}")
    return entries


def build_image(entries):
    """Lay out the directory + 4 KiB-aligned blobs and return the image bytes."""
    count = len(entries)
    dir_end = struct.calcsize(HEADER_FMT) + count * struct.calcsize(ENTRY_FMT)
    cursor = align_up(dir_end, SECTOR_SIZE)  # first blob starts on a sector boundary

    placed = []  # (type, name, offset, length, samplerate, blob)
    for (etype, name, blob, rate) in entries:
        offset = cursor
        placed.append((etype, name, offset, len(blob), rate, blob))
        cursor = align_up(offset + len(blob), SECTOR_SIZE)

    total = cursor
    if total > PARTITION_SIZE:
        die(
            f"image is {total} bytes, exceeds partition size {PARTITION_SIZE} "
            f"({PARTITION_SIZE // 1024} KiB)"
        )

    image = bytearray(total)
    struct.pack_into(HEADER_FMT, image, 0, MAGIC, VERSION, count)
    epos = struct.calcsize(HEADER_FMT)
    for (etype, name, offset, length, rate, blob) in placed:
        struct.pack_into(
            ENTRY_FMT, image, epos, etype, encode_name(name), offset, length, rate, 0
        )
        epos += struct.calcsize(ENTRY_FMT)
        image[offset : offset + length] = blob

    return bytes(image), total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src_dir", help="folder containing .namb / .wav / presets.json")
    ap.add_argument("-o", "--output", default="data_image.bin", help="output image path")
    ap.add_argument(
        "--max-taps",
        type=int,
        default=512,
        help="trim IRs to at most this many taps (0 = no limit; default 512)",
    )
    args = ap.parse_args()

    if not os.path.isdir(args.src_dir):
        die(f"{args.src_dir} is not a directory")

    print(f"scanning {args.src_dir} ...")
    entries = gather_blobs(args.src_dir, args.max_taps)
    entries += gather_presets(args.src_dir)
    if not entries:
        die("no .namb / .wav / presets found")

    image, total = build_image(entries)
    with open(args.output, "wb") as f:
        f.write(image)

    used_pct = 100.0 * total / PARTITION_SIZE
    print(
        f"\nwrote {args.output}: {len(entries)} entries, {total} bytes "
        f"({total // 1024} KiB, {used_pct:.1f}% of {PARTITION_SIZE // (1024*1024)} MiB partition)"
    )
    print("flash it with:  ./flash_data.sh " + args.output)


if __name__ == "__main__":
    main()
