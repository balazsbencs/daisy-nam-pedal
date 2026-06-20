// data_format.h — on-board QSPI data partition layout for the NAM pedal platform.
//
// This is the canonical, single-source-of-truth definition of the storage format
// shared by the firmware (reads it, memory-mapped) and the PC packer tool
// (tools/build_data_image.py, which mirrors these constants in Python).
//
// Storage model (no SD card):
//   The pedal boots execute-in-place from the 8 MB QSPI flash (APP_TYPE=BOOT_QSPI).
//   Models / IRs / presets live in a "data partition" near the top of that same
//   QSPI chip. The firmware never WRITES this region while running (you can't erase
//   QSPI while fetching code from it); writes happen via the bootloader's DFU using
//   tools/flash_data.sh. The firmware only READS, by casting the memory-mapped
//   pointer QSPIHandle::GetData(NAM_DATA_PARTITION_OFFSET + entry.offset).
//
// QSPI 8 MB map under BOOT_QSPI:
//   0x90000000 - 0x9003FFFF  bootloader            (256 KB, reserved)
//   0x90040000 - 0x901FFFFF  application code      (~600 KB used; reserved to 2 MB)
//   0x90200000 - 0x907FFFFF  DATA PARTITION        (~6 MB: models + IRs + presets)

#ifndef NAM_DATA_FORMAT_H
#define NAM_DATA_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// QSPI memory-mapped base (STM32H750 / Daisy Seed).
#define NAM_QSPI_BASE_ADDR 0x90000000u

// Offset of the data partition from the start of QSPI (also its DFU target).
#define NAM_DATA_PARTITION_OFFSET 0x00200000u // -> absolute 0x90200000
#define NAM_DATA_PARTITION_SIZE 0x00600000u   // 6 MB

// Absolute memory-mapped address of the partition (for direct pointer reads).
#define NAM_DATA_PARTITION_ADDR \
    (NAM_QSPI_BASE_ADDR + NAM_DATA_PARTITION_OFFSET)

// 'N','A','M','D' stored in that byte order -> little-endian uint32.
#define NAM_DATA_MAGIC 0x444D414Eu

#define NAM_DATA_VERSION 1u

// QSPI erase granularity. Blobs are aligned to this so a future in-place writer
// (approach B) can erase/replace one entry without touching its neighbours.
#define NAM_DATA_SECTOR_SIZE 4096u

// Entry name capacity (including the null terminator).
#define NAM_DATA_NAME_LEN 31u

// Entry types.
typedef enum
{
    NAM_ENTRY_MODEL  = 0, // raw .namb blob, fed to nam::get_dsp_namb()
    NAM_ENTRY_IR     = 1, // raw float32 LE mono taps; tap count = length / 4
    NAM_ENTRY_PRESET = 2, // NamPreset record (see below)
} NamEntryType;

// Partition header — lives at the very start of the partition (sector 0).
typedef struct
{
    uint32_t magic;   // NAM_DATA_MAGIC
    uint16_t version; // NAM_DATA_VERSION
    uint16_t count;   // number of directory entries that follow
} NamDataHeader;

// Directory entry — 48 bytes. The directory array immediately follows the header.
// `offset` is measured from the partition base (NAM_DATA_PARTITION_ADDR), so a blob
// is read at NAM_DATA_PARTITION_ADDR + entry.offset for `length` bytes.
typedef struct
{
    uint8_t  type;                    // NamEntryType
    char     name[NAM_DATA_NAME_LEN]; // null-terminated display name
    uint32_t offset;                  // bytes from partition base to blob
    uint32_t length;                  // blob length in bytes
    uint32_t samplerate;              // IR/model sample rate, 0 if n/a
    uint32_t reserved;                // 0; reserved for future flags
} NamDataEntry;

// Optional preset record stored as a NAM_ENTRY_PRESET blob. References models/IRs
// by name so it stays valid regardless of directory ordering. Levels in [0,1].
//
// Packed to eliminate implicit compiler padding between the char[] fields and the
// first float. Python packer uses "<31s31sffB3x" which also produces 74 bytes.
// static_assert below enforces the match at compile time.
typedef struct __attribute__((packed))
{
    char    model_name[NAM_DATA_NAME_LEN]; //  0..30
    char    ir_name[NAM_DATA_NAME_LEN];    // 31..61  (empty string = IR bypassed)
    float   input_gain;                    // 62..65
    float   output_volume;                 // 66..69
    uint8_t bypass;                        // 70      (0 = active, 1 = passthrough)
    uint8_t pad[3];                        // 71..73  explicit padding
    // --- EQ block (appended; older blobs lack this — see PresetManager) ---
    float   eq_bass_gain;                  // 74..77   dB  [-12,12]
    float   eq_mid_gain;                   // 78..81   dB
    float   eq_treble_gain;                // 82..85   dB
    float   eq_bass_freq;                  // 86..89   Hz  (0 = use default)
    float   eq_mid_freq;                   // 90..93   Hz
    float   eq_treble_freq;                // 94..97   Hz
} NamPreset;

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus
static_assert(sizeof(NamDataHeader) == 8,  "NamDataHeader size mismatch");
static_assert(sizeof(NamDataEntry)  == 48, "NamDataEntry size mismatch");
static_assert(sizeof(NamPreset)     == 98, "NamPreset size mismatch — check packing vs Python PRESET_FMT");
#endif

#endif // NAM_DATA_FORMAT_H
