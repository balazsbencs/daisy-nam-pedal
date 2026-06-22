TARGET = NamPlatform

LIBDAISY_DIR  = third_party/libDaisy
DAISYSP_DIR   = third_party/DaisySP
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# --- project sources --------------------------------------------------------
CPP_SOURCES = \
  main.cpp \
  NamEmbeddedStubs.cpp \
  QspiStorage.cpp \
  AudioEngine.cpp \
  Eq3.cpp \
  RealFft128.cpp \
  PartitionedConvolver.cpp \
  QuadEncoder.cpp \
  IRLoader.cpp \
  ModelManager.cpp \
  PresetManager.cpp \
  Controls.cpp \
  Ui.cpp \
  display/st7789_driver.cpp \
  display/display_renderer.cpp \
  nam-binary-loader/namb/get_dsp_namb.cpp \
  NeuralAmpModelerCore/NAM/activations.cpp \
  NeuralAmpModelerCore/NAM/conv1d.cpp \
  NeuralAmpModelerCore/NAM/convnet.cpp \
  NeuralAmpModelerCore/NAM/dsp.cpp \
  NeuralAmpModelerCore/NAM/linear.cpp \
  NeuralAmpModelerCore/NAM/lstm.cpp \
  NeuralAmpModelerCore/NAM/ring_buffer.cpp \
  NeuralAmpModelerCore/NAM/util.cpp \
  NeuralAmpModelerCore/NAM/wavenet/model.cpp \
  NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp

# display stack from the user's other project (copy or symlink ./display/)
# Uncomment when you drop the files in:
# CPP_SOURCES += display/st7789_driver.cpp display/display_manager.cpp \
#                display/display_renderer.cpp

C_INCLUDES = \
  -I. \
  -Idisplay \
  -INeuralAmpModelerCore \
  -INeuralAmpModelerCore/Dependencies/eigen \
  -INeuralAmpModelerCore/Dependencies/nlohmann \
  -Inam-binary-loader

# --- build config -----------------------------------------------------------
# BOOT_QSPI is mandatory: the app is ~600 KB of code, which exceeds the
# STM32H750's 128 KB internal flash. Models/IRs live in the same QSPI chip
# at 0x90200000 (see data_format.h). DO NOT change this to BOOT_SRAM.
APP_TYPE = BOOT_QSPI

CPP_STANDARD = -std=gnu++17
OPT = -O3
LDFLAGS = -u _printf_float

# No SD card, no FatFS.
# USE_FATFS = 1   <-- intentionally omitted

C_DEFS = \
  -DNAM_SAMPLE_FLOAT \
  -DNAM_USE_INLINE_GEMM \
  -DNAM_ENABLE_A2_FAST \
  -D__ARM_ARCH_7EM__ \
  -DUSE_ARM_DSP \
  -DARM_DSP_CONFIG_TABLES \
  -DARM_FFT_ALLOW_TABLES \
  -DARM_TABLE_TWIDDLECOEF_F32_64 \
  -DARM_TABLE_BITREVIDX_FXT_64 \
  -DARM_TABLE_TWIDDLECOEF_RFFT_F32_128

C_SOURCES += \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/CommonTables/arm_common_tables.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_bitreversal2.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_init_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c

# Pull in the libDaisy master build rules (also picks up DAISYSP_DIR).
include $(SYSTEM_FILES_DIR)/Makefile

CPPFLAGS += -fexceptions -ffast-math -funroll-loops -ftree-vectorize \
            -fmove-loop-invariants

# ---------------------------------------------------------------------------
# Data image — pack default models, IRs, and presets into a QSPI flash image.
# Flash it with:  ./tools/flash_data.sh data_image.bin
# ---------------------------------------------------------------------------
.PHONY: data-image
data-image:
	python3 tools/build_data_image.py data/ -o data_image.bin
