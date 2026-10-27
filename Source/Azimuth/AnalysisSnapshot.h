#pragma once

#include "Azimuth/SpectralTilt.h"

#include <atomic>
#include <cstdint>

namespace azimuter
{
/**
    Hands one analysis frame from the audio thread to the message thread.

    The calibrator needs the delay and the whole band-tilt vector from the
    *same* frame — a torn read that mixed two frames would quietly corrupt an
    observation. A plain atomic per field cannot promise that, so this is a
    small lock-free ring: the writer fills the next slot and then publishes a
    monotonic counter, and the reader checks afterwards that the writer has not
    lapped it mid-copy.

    Frames are published every hop (~23 ms) and read by a 20 Hz timer, so with
    four slots the writer needs ~90 ms to lap a reader that copies a few dozen
    bytes. A failed read is reported rather than papered over; the caller just
    waits for the next timer tick.

    `publish()` is wait-free and allocation-free — safe from `processBlock`.
*/
class AnalysisSnapshot
{
public:
    static constexpr int kMaxBands = SpectralTilt::kMaxBands;
    static constexpr int kSlots = 4;

    struct Frame
    {
        float delayUs{0.0f};       ///< fine (HF-band) inter-channel delay
        float coarseDelayUs{0.0f}; ///< unambiguous wide-band estimate
        float referenceLevelDb{-120.0f};
        float phaseCoherence{0.0f};
        float signalLevel{0.0f};
        float significance{0.0f}; ///< shared-signal evidence over noise; 1.0 = noise only
        int numBands{0};
        float tiltDb[kMaxBands]{};
    };

    /** Audio thread. */
    void publish(const Frame& frame) noexcept
    {
        const uint64_t c = counter.load(std::memory_order_relaxed);
        slots[c % kSlots] = frame;
        counter.store(c + 1, std::memory_order_release);
    }

    /** Message thread. Returns false if no frame has been published yet, or if
        the writer lapped the reader during the copy. */
    bool read(Frame& out) const noexcept
    {
        const uint64_t before = counter.load(std::memory_order_acquire);
        if (before == 0)
            return false;
        out = slots[(before - 1) % kSlots];
        const uint64_t after = counter.load(std::memory_order_acquire);
        return after - before < static_cast<uint64_t>(kSlots - 1);
    }

    void reset() noexcept { counter.store(0, std::memory_order_release); }

private:
    Frame slots[kSlots]{};
    std::atomic<uint64_t> counter{0};
};
} // namespace azimuter
