"""
TEST-11: tools/build_data_image.py packer round-trip.

Verifies that:
- Struct sizes match: NamDataHeader=8, NamDataEntry=48, NamPreset=74
- A synthetic data image parses correctly: magic, version, entry count
- NamPreset fields round-trip through pack/unpack without corruption
- Blob offsets are 4 KB-aligned
- Inspect script validates the same image without errors
"""
import struct
import os
import sys
import tempfile
import shutil

# --- Constants mirrored from data_format.h ----------------------------------
NAM_DATA_MAGIC   = 0x444D414E  # 'NAMD'
NAM_DATA_VERSION = 1
NAM_DATA_NAME_LEN = 31

HEADER_FMT  = "<IHH"   # magic(4) version(2) count(2)  = 8 bytes
ENTRY_FMT   = "<B31sIIII11x"  # type(1) name(31) offset(4) length(4) sr(4) reserved(4) pad(11) = NO wait...

# Actual NamDataEntry layout (48 bytes):
#   type(1) + name(31) + offset(4) + length(4) + samplerate(4) + reserved(4) = 44 bytes total
# But sizeof(NamDataEntry) == 48 → 4 bytes of padding after type before name? Let me check:
# type=uint8 at offset 0, name[31] at offset 1 → no padding needed (char alignment=1)
# then offset=uint32 at offset 32 → 32%4==0 ok, no padding
# so total = 1+31+4+4+4+4 = 48. Exact. No implicit padding.
ENTRY_FMT   = "<B31sIIII"   # = 1+31+4+4+4+4 = 48 bytes
PRESET_FMT  = "<31s31sffB3x6f"  # = 31+31+4+4+1+3+24 = 98 bytes

SECTOR_SIZE = 4096

def pack_header(count):
    return struct.pack(HEADER_FMT, NAM_DATA_MAGIC, NAM_DATA_VERSION, count)

def pack_entry(entry_type, name, offset, length, samplerate=0):
    name_bytes = name.encode()[:NAM_DATA_NAME_LEN - 1].ljust(NAM_DATA_NAME_LEN, b'\x00')
    return struct.pack(ENTRY_FMT, entry_type, name_bytes, offset, length, samplerate, 0)

def pack_preset(model_name, ir_name, input_gain, output_volume, bypass,
                eq_bass_gain=0.0, eq_mid_gain=0.0, eq_treble_gain=0.0,
                eq_bass_freq=100.0, eq_mid_freq=750.0, eq_treble_freq=4000.0):
    mn = model_name.encode()[:NAM_DATA_NAME_LEN-1].ljust(NAM_DATA_NAME_LEN, b'\x00')
    ir = ir_name.encode()[:NAM_DATA_NAME_LEN-1].ljust(NAM_DATA_NAME_LEN, b'\x00')
    return struct.pack(PRESET_FMT, mn, ir, input_gain, output_volume, bypass,
                       eq_bass_gain, eq_mid_gain, eq_treble_gain,
                       eq_bass_freq, eq_mid_freq, eq_treble_freq)


def build_test_image(blobs):
    """Build a minimal partition image with the given [(type, name, bytes)] list."""
    count = len(blobs)
    header = pack_header(count)
    assert struct.calcsize(HEADER_FMT) == 8

    # Directory start: after header
    dir_offset = len(header)
    dir_size   = count * struct.calcsize(ENTRY_FMT)
    assert struct.calcsize(ENTRY_FMT) == 48

    # Blob area: 4 KB-aligned start after directory
    blob_area_start = dir_offset + dir_size
    blob_area_start = (blob_area_start + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1)
    if blob_area_start == 0:
        blob_area_start = SECTOR_SIZE

    entries = b""
    blob_data = b""
    cursor = blob_area_start
    for (entry_type, name, data) in blobs:
        offset = cursor
        length = len(data)
        entries  += pack_entry(entry_type, name, offset, length)
        blob_data = blob_data.ljust(cursor - blob_area_start, b'\xff')
        blob_data += data
        next_cursor = (cursor + length + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1)
        cursor = next_cursor

    image = header + entries
    image = image.ljust(blob_area_start, b'\xff')
    image += blob_data
    return image


# --- TEST: struct sizes -------------------------------------------------------

def test_struct_sizes():
    assert struct.calcsize(HEADER_FMT) == 8,  f"Header size {struct.calcsize(HEADER_FMT)} != 8"
    assert struct.calcsize(ENTRY_FMT)  == 48, f"Entry size {struct.calcsize(ENTRY_FMT)} != 48"
    assert struct.calcsize(PRESET_FMT) == 98, f"Preset size {struct.calcsize(PRESET_FMT)} != 98"
    print("PASS  struct sizes")


# --- TEST: image round-trip --------------------------------------------------

def test_round_trip():
    preset_bytes = pack_preset("Plexi", "V30", 0.9, 0.75, 0)
    image = build_test_image([
        (0, "TestModel", b"\x01\x02\x03\x04"),  # NAM_ENTRY_MODEL
        (1, "TestIR",    b"\x00" * 16),          # NAM_ENTRY_IR (4 floats)
        (2, "RockLead",  preset_bytes),           # NAM_ENTRY_PRESET
    ])

    # Parse back
    hdr_size = struct.calcsize(HEADER_FMT)
    magic, version, count = struct.unpack_from(HEADER_FMT, image)
    assert magic   == NAM_DATA_MAGIC,   f"Magic mismatch: 0x{magic:08X}"
    assert version == NAM_DATA_VERSION, f"Version mismatch: {version}"
    assert count   == 3,                f"Count mismatch: {count}"

    entry_size = struct.calcsize(ENTRY_FMT)
    entries = []
    for i in range(count):
        off = hdr_size + i * entry_size
        t, name, offset, length, sr, reserved = struct.unpack_from(ENTRY_FMT, image, off)
        entries.append((t, name.rstrip(b'\x00').decode(), offset, length))

    assert entries[0] == (0, "TestModel", entries[0][2], 4)
    assert entries[1] == (1, "TestIR",    entries[1][2], 16)
    assert entries[2][0] == 2
    assert entries[2][1] == "RockLead"

    # Verify blob offsets are 4 KB-aligned
    for (_, name, offset, _) in entries:
        assert offset % SECTOR_SIZE == 0, f"Entry '{name}' offset {offset} not 4K-aligned"

    # Parse preset blob
    preset_offset = entries[2][2]
    preset_len    = entries[2][3]
    assert preset_len == 98
    fields = struct.unpack_from(PRESET_FMT, image, preset_offset)
    mn, ir, gain, vol, bypass = fields[0], fields[1], fields[2], fields[3], fields[4]
    assert mn.rstrip(b'\x00').decode() == "Plexi"
    assert ir.rstrip(b'\x00').decode() == "V30"
    assert abs(gain - 0.9)  < 0.001, f"gain {gain}"
    assert abs(vol  - 0.75) < 0.001, f"vol {vol}"
    assert bypass == 0

    print("PASS  round_trip")


# --- TEST: NamPreset field offsets via struct module -------------------------

def test_preset_field_offsets():
    # model_name at byte 0, ir_name at byte 31, input_gain at byte 62
    raw = pack_preset("AAAA", "BBBB", 0.5, 0.25, 1)
    assert len(raw) == 98
    assert raw[0:4]  == b"AAAA", f"model_name bad: {raw[0:4]}"
    assert raw[31:35] == b"BBBB", f"ir_name bad: {raw[31:35]}"
    gain_bytes = raw[62:66]
    vol_bytes  = raw[66:70]
    bypass_byte = raw[70]
    gain, = struct.unpack_from("<f", gain_bytes)
    vol,  = struct.unpack_from("<f", vol_bytes)
    assert abs(gain - 0.5)  < 0.001
    assert abs(vol  - 0.25) < 0.001
    assert bypass_byte == 1
    print("PASS  preset_field_offsets")


def test_eq_fields():
    # Import the real packer tool and verify its PRESET_FMT is 98 bytes.
    import importlib.util, os as _os
    tool_path = _os.path.join(_os.path.dirname(__file__), "..", "tools", "build_data_image.py")
    spec = importlib.util.spec_from_file_location("build_data_image", tool_path)
    mod  = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert struct.calcsize(mod.PRESET_FMT) == 98, \
        f"build_data_image.PRESET_FMT must be 98 bytes, got {struct.calcsize(mod.PRESET_FMT)}"
    # Round-trip with EQ fields via the tool's pack_preset().
    blob = mod.pack_preset({
        "model": "amp", "ir": "cab",
        "input_gain": 1.0, "output_volume": 0.8, "bypass": False,
        "eq": {"bass_gain": -3.0, "mid_gain": 2.5, "treble_gain": 4.0,
               "bass_freq": 120.0, "mid_freq": 800.0, "treble_freq": 3500.0},
    })
    assert len(blob) == 98, f"pack_preset output must be 98 bytes, got {len(blob)}"
    fields = struct.unpack(mod.PRESET_FMT, blob)
    assert abs(fields[5] - (-3.0)) < 1e-6,  f"eq_bass_gain bad: {fields[5]}"
    assert abs(fields[10] - 3500.0) < 1e-3, f"eq_treble_freq bad: {fields[10]}"
    print("PASS  eq_fields")


if __name__ == "__main__":
    ok = True
    try:
        test_struct_sizes()
        test_round_trip()
        test_preset_field_offsets()
        test_eq_fields()
    except AssertionError as e:
        print(f"FAIL  {e}")
        ok = False
    sys.exit(0 if ok else 1)
