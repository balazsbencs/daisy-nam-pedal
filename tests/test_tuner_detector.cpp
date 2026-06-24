// test_tuner_detector.cpp — host tests for the monophonic tuner detector.
#include "TunerDetector.h"
#include "test_harness.h"
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Feed `seconds` of a `hz` sine at 48 kHz through the detector's public capture
// API in small blocks (mirrors the audio callback). Goes through the decimator,
// does not bypass it.
static void FeedSine(TunerDetector& detector,
                     float          hz,
                     float          seconds,
                     float          amplitude   = 0.5f,
                     float          sample_rate = 48000.0f)
{
    const size_t total  = static_cast<size_t>(seconds * sample_rate);
    const size_t kBlock = 48;
    float        block[kBlock];
    size_t       done = 0;
    while (done < total)
    {
        size_t n = (total - done < kBlock) ? (total - done) : kBlock;
        for (size_t i = 0; i < n; ++i)
        {
            float t  = static_cast<float>(done + i) / sample_rate;
            block[i] = amplitude * std::sin(2.0f * static_cast<float>(M_PI) * hz * t);
        }
        detector.PushAudioBlock(block, n);
        done += n;
    }
}

// Feed two simultaneous sines (an interval) — phase one must not lock onto this.
static void FeedTwoSines(TunerDetector& detector,
                         float          hz_a,
                         float          hz_b,
                         float          seconds,
                         float          amplitude   = 0.4f,
                         float          sample_rate = 48000.0f)
{
    const size_t total  = static_cast<size_t>(seconds * sample_rate);
    const size_t kBlock = 48;
    float        block[kBlock];
    size_t       done = 0;
    while (done < total)
    {
        size_t n = (total - done < kBlock) ? (total - done) : kBlock;
        for (size_t i = 0; i < n; ++i)
        {
            float t  = static_cast<float>(done + i) / sample_rate;
            float w  = 2.0f * static_cast<float>(M_PI) * t;
            block[i] = amplitude * (std::sin(w * hz_a) + std::sin(w * hz_b));
        }
        detector.PushAudioBlock(block, n);
        done += n;
    }
}

// Feed `seconds` of zeros (silence).
static void FeedSilence(TunerDetector& detector, float seconds, float sample_rate = 48000.0f)
{
    const size_t total  = static_cast<size_t>(seconds * sample_rate);
    const size_t kBlock = 48;
    float        block[kBlock] = {};
    size_t       done = 0;
    while (done < total)
    {
        size_t n = (total - done < kBlock) ? (total - done) : kBlock;
        detector.PushAudioBlock(block, n);
        done += n;
    }
}

// Detect a single steady tone and assert it locks within `tol_hz`.
static void CheckString(float hz, float tol_hz, const char* label)
{
    TunerDetector det;
    det.Reset();
    FeedSine(det, hz, 0.4f);
    TunerPitch p;
    det.Analyze(p);
    CHECK(p.stable);
    if (!p.stable)
        fprintf(stderr, "  (%s %.2f Hz did not lock)\n", label, hz);
    CHECK(std::fabs(p.frequency_hz - hz) < tol_hz);
    if (std::fabs(p.frequency_hz - hz) >= tol_hz)
        fprintf(stderr, "  (%s expected %.2f got %.2f)\n", label, hz, p.frequency_hz);
}

int main()
{
    // --- Open guitar strings -------------------------------------------------
    // Low strings within ~2 Hz, all within ~1%.
    CheckString(82.41f, 2.0f, "E2");
    CheckString(110.00f, 2.0f, "A2");
    CheckString(146.83f, 2.0f, "D3");
    CheckString(196.00f, 196.00f * 0.01f, "G3");
    CheckString(246.94f, 246.94f * 0.01f, "B3");
    CheckString(329.63f, 329.63f * 0.01f, "E4");

    // --- Chromatic note mapping (no audio, pure helper) ----------------------
    {
        TunerPitch p;
        TunerNoteFromFrequency(440.0f, p);
        CHECK_STR(p.note, "A");
        CHECK_EQ(p.octave, 4);
        CHECK(std::fabs(p.cents) < 1.0f);
    }
    {
        TunerPitch p;
        TunerNoteFromFrequency(445.0f, p);
        CHECK_STR(p.note, "A");
        CHECK_EQ(p.octave, 4);
        CHECK(p.cents > 0.0f);
    }
    {
        TunerPitch p;
        TunerNoteFromFrequency(435.0f, p);
        CHECK_STR(p.note, "A");
        CHECK_EQ(p.octave, 4);
        CHECK(p.cents < 0.0f);
    }
    {
        TunerPitch p;
        TunerNoteFromFrequency(466.16f, p);
        CHECK_STR(p.note, "A#");
        CHECK_EQ(p.octave, 4);
        CHECK(std::fabs(p.cents) < 2.0f);
    }

    // --- Rejection paths -----------------------------------------------------
    {
        TunerDetector det;
        det.Reset();
        FeedSilence(det, 0.4f);
        TunerPitch p;
        det.Analyze(p);
        CHECK(!p.stable);
        CHECK(!p.signal_present);
    }
    {
        // Very low-level noise-ish tone below the silence floor.
        TunerDetector det;
        det.Reset();
        FeedSine(det, 110.0f, 0.4f, 0.0008f);
        TunerPitch p;
        det.Analyze(p);
        CHECK(!p.stable);
    }
    {
        // Two simultaneous tones (a fifth): phase one must not report a stable lock.
        TunerDetector det;
        det.Reset();
        FeedTwoSines(det, 110.0f, 164.81f, 0.4f);
        TunerPitch p;
        det.Analyze(p);
        CHECK(!p.stable || p.confidence < 0.5f);
    }

    return test_summary("test_tuner_detector");
}
