# tests/Makefile — host-side unit tests for NamPlatform.
# Requires: clang++ (or g++), python3.
# Run:  cd tests && make

CXX     ?= clang++
CXXFLAGS = -std=c++17 -O0 -g \
           -Wall -Wextra -Wno-unused-parameter \
           -fsanitize=address,undefined \
           -DHOST_BUILD \
           -I. \
           -Ihost_shims \
           -I..

# host_shims must come before the project root so shim headers shadow SDK ones.

# Production sources compiled for the host (with hardware deps satisfied by shims).
QSPI_SRCS      = ../QspiStorage.cpp
EQ_SRCS        = ../Eq3.cpp
AUDIO_SRCS     = ../AudioEngine.cpp ../Eq3.cpp
IR_SRCS        = ../IRLoader.cpp
PRESET_SRCS    = ../PresetManager.cpp
MODEL_STUB_SRC = stub_impls/ModelManager_stub.cpp

BINARIES = test_data_format test_qspi_storage test_preset_manager test_ir_loader test_eq3 test_audio_engine test_quad_encoder test_meter_fill

.PHONY: all run clean

all: $(BINARIES)

# TEST-01: struct layout
test_data_format: test_data_format.cpp ../data_format.h
	$(CXX) $(CXXFLAGS) $< -o $@

# TEST-02 / TEST-03: QSPI storage directory parsing
test_qspi_storage: test_qspi_storage.cpp $(QSPI_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# TEST-04 to TEST-08: PresetManager logic
test_preset_manager: test_preset_manager.cpp \
                     $(PRESET_SRCS) \
                     $(QSPI_SRCS) \
                     $(AUDIO_SRCS) \
                     $(IR_SRCS) \
                     $(MODEL_STUB_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

# TEST-09 / TEST-10: FirConvolver guards and LoadIrFromQspi
test_ir_loader: test_ir_loader.cpp $(IR_SRCS) $(QSPI_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# EQ magnitude response
test_eq3: test_eq3.cpp $(EQ_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# AudioEngine EQ integration
test_audio_engine: test_audio_engine.cpp $(AUDIO_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# Pure quadrature decoder (no Daisy deps)
test_quad_encoder: test_quad_encoder.cpp ../quad_decode.h
	$(CXX) $(CXXFLAGS) $< -o $@

# Pure vmeter_fill geometry (no Daisy deps)
test_meter_fill: test_meter_fill.cpp ../display/meter_fill.h
	$(CXX) $(CXXFLAGS) $< -o $@

run: all
	@echo "=== TEST-01: data_format ==="
	./test_data_format
	@echo "=== TEST-02/03: qspi_storage ==="
	./test_qspi_storage
	@echo "=== TEST-04..08: preset_manager ==="
	./test_preset_manager
	@echo "=== TEST-09/10: ir_loader ==="
	./test_ir_loader
	@echo "=== test_eq3 ==="
	./test_eq3
	@echo "=== test_audio_engine ==="
	./test_audio_engine
	@echo "=== test_quad_encoder ==="
	./test_quad_encoder
	@echo "=== test_meter_fill ==="
	./test_meter_fill
	@echo "=== TEST-11: tools (Python) ==="
	python3 test_tools.py

clean:
	rm -f $(BINARIES)
