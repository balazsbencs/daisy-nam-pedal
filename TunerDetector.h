// TunerDetector.h — fixed-buffer monophonic pitch detector for tuner mode.
// Captures dry input in the audio callback (decimated ring), runs YIN pitch
// detection from the main loop. No heap, no locks.
#pragma once

#include <cstddef>
#include <cstdint>

struct TunerPitch
{
    bool  signal_present = false;
    bool  stable         = false;
    float frequency_hz   = 0.0f;
    float cents          = 0.0f;
    float confidence     = 0.0f;
    char  note[4]        = {'-', '-', '\0', '\0'};
    int   octave         = 0;
};

// Capture/analysis constants.
constexpr float    kInputSampleRate    = 48000.0f;
constexpr uint32_t kDecimation         = 4;
constexpr float    kDetectorSampleRate = kInputSampleRate / kDecimation; // 12 kHz
constexpr size_t   kRingSize           = 4096;                           // power of two
constexpr size_t   kWindowSize         = 2048;

// Map a frequency to the nearest chromatic note name, octave and cents offset.
// Pure and unit-testable without feeding audio.
void TunerNoteFromFrequency(float hz, TunerPitch& out);

class TunerDetector
{
  public:
    void Reset();

    // Called from the audio callback with raw 48 kHz mono input. Low-passes,
    // decimates by 4, and stores into the ring buffer. Lock-free.
    void PushAudioBlock(const float* mono_input, size_t frames);

    // Called from the main loop (rate-limited). Runs YIN over the latest window
    // and fills `out`. Returns true when a stable pitch was found.
    bool Analyze(TunerPitch& out);

  private:
    static constexpr size_t kRingMask = kRingSize - 1;
    static constexpr size_t kMinTau   = 8;   // 1500 Hz at 12 kHz
    static constexpr size_t kMaxTau   = 200; // 60 Hz at 12 kHz

    float    ring_[kRingSize]  = {};
    uint32_t write_idx_        = 0; // total decimated samples written
    float    lp_state_         = 0.0f;
    uint32_t decim_counter_    = 0;

    // Analysis scratch kept off the stack for embedded safety.
    float window_[kWindowSize] = {};
    float diff_[kMaxTau + 1]   = {};
    float cmndf_[kMaxTau + 1]  = {};
};
