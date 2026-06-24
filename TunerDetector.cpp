// TunerDetector.cpp — see TunerDetector.h.
#include "TunerDetector.h"

#include <cmath>

namespace
{
// One-pole low-pass coefficient for ~3 kHz cutoff at 48 kHz (anti-alias before
// decimating to 12 kHz). alpha = 1 - exp(-2*pi*fc/fs).
constexpr float kLpAlpha = 0.325f;

// Reject anything quieter than this RMS as "no signal".
constexpr float kSilenceRms = 0.003f;

// YIN absolute threshold: a tau whose CMNDF dips below this is a pitch candidate.
constexpr float kYinThreshold = 0.12f;

// Plausible fundamental range (guitar + headroom).
constexpr float kMinFreqHz = 60.0f;
constexpr float kMaxFreqHz = 1500.0f;
} // namespace

void TunerDetector::Reset()
{
    for (size_t i = 0; i < kRingSize; ++i)
        ring_[i] = 0.0f;
    write_idx_     = 0;
    lp_state_      = 0.0f;
    decim_counter_ = 0;
}

void TunerDetector::PushAudioBlock(const float* mono_input, size_t frames)
{
    for (size_t i = 0; i < frames; ++i)
    {
        lp_state_ += kLpAlpha * (mono_input[i] - lp_state_);
        if (++decim_counter_ >= kDecimation)
        {
            decim_counter_       = 0;
            ring_[write_idx_ & kRingMask] = lp_state_;
            ++write_idx_;
        }
    }
}

bool TunerDetector::Analyze(TunerPitch& out)
{
    out = TunerPitch{};

    // Not enough samples captured yet.
    if (write_idx_ < kWindowSize)
        return false;

    // Copy the latest kWindowSize decimated samples in order, oldest first.
    const uint32_t start = write_idx_ - static_cast<uint32_t>(kWindowSize);
    double         sum_sq = 0.0;
    for (size_t i = 0; i < kWindowSize; ++i)
    {
        float s    = ring_[(start + i) & kRingMask];
        window_[i] = s;
        sum_sq += static_cast<double>(s) * s;
    }

    float rms = std::sqrt(static_cast<float>(sum_sq / kWindowSize));
    out.signal_present = rms >= kSilenceRms;
    if (!out.signal_present)
        return false;

    // YIN difference function over a fixed integration length so every tau sums
    // the same number of terms.
    const size_t N = kWindowSize - kMaxTau;
    diff_[0]       = 0.0f;
    for (size_t tau = 1; tau <= kMaxTau; ++tau)
    {
        float acc = 0.0f;
        for (size_t j = 0; j < N; ++j)
        {
            float d = window_[j] - window_[j + tau];
            acc += d * d;
        }
        diff_[tau] = acc;
    }

    // Cumulative mean normalized difference.
    cmndf_[0]        = 1.0f;
    float running_sum = 0.0f;
    for (size_t tau = 1; tau <= kMaxTau; ++tau)
    {
        running_sum += diff_[tau];
        cmndf_[tau] = (running_sum > 0.0f)
                          ? diff_[tau] * static_cast<float>(tau) / running_sum
                          : 1.0f;
    }

    // Absolute threshold: first tau dipping below threshold, descend to its local min.
    size_t tau = 0;
    for (size_t t = kMinTau; t <= kMaxTau; ++t)
    {
        if (cmndf_[t] < kYinThreshold)
        {
            while (t + 1 <= kMaxTau && cmndf_[t + 1] < cmndf_[t])
                ++t;
            tau = t;
            break;
        }
    }
    if (tau == 0)
        return false; // no confident pitch

    // Parabolic interpolation around the minimum for sub-sample accuracy.
    float refined = static_cast<float>(tau);
    if (tau > kMinTau && tau < kMaxTau)
    {
        float a     = cmndf_[tau - 1];
        float b     = cmndf_[tau];
        float c     = cmndf_[tau + 1];
        float denom = 2.0f * (2.0f * b - a - c);
        if (std::fabs(denom) > 1e-9f)
            refined = static_cast<float>(tau) + (c - a) / denom;
    }

    float freq = kDetectorSampleRate / refined;
    if (freq < kMinFreqHz || freq > kMaxFreqHz)
        return false;

    out.frequency_hz = freq;
    out.confidence   = 1.0f - cmndf_[tau];
    out.stable       = true;
    TunerNoteFromFrequency(freq, out);
    return true;
}

void TunerNoteFromFrequency(float hz, TunerPitch& out)
{
    static const char* const kNames[12]
        = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    if (hz <= 0.0f)
    {
        out.note[0] = '-';
        out.note[1] = '-';
        out.note[2] = '\0';
        out.note[3] = '\0';
        out.octave  = 0;
        out.cents   = 0.0f;
        return;
    }

    float note_f      = 69.0f + 12.0f * std::log2(hz / 440.0f);
    int   note_number = static_cast<int>(std::lround(note_f));
    int   idx         = ((note_number % 12) + 12) % 12;

    const char* nm = kNames[idx];
    out.note[0]    = nm[0];
    out.note[1]    = nm[1]; // '#' for sharps, '\0' otherwise
    out.note[2]    = '\0';
    out.note[3]    = '\0';
    out.octave     = note_number / 12 - 1;

    float note_freq = 440.0f * std::exp2((note_number - 69) / 12.0f);
    out.cents       = 1200.0f * std::log2(hz / note_freq);
}
