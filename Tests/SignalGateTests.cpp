#include "Azimuth/SignalGate.h"

#include <catch2/catch_test_macros.hpp>

using azimuter::SignalGate;

namespace
{
constexpr float kStep = 0.01f; // 10 ms blocks

bool feed(SignalGate& g, float significance, float seconds)
{
    bool state = g.present();
    for (float t = 0.0f; t < seconds; t += kStep)
        state = g.update(significance, kStep);
    return state;
}
} // namespace

TEST_CASE("Gate starts closed", "[gate]")
{
    SignalGate g;
    REQUIRE_FALSE(g.present());
}

TEST_CASE("Gate opens once significance holds above the on threshold", "[gate]")
{
    SignalGate g;
    REQUIRE_FALSE(feed(g, 2.0f, SignalGate::kOnHoldSeconds * 0.5f));
    REQUIRE(feed(g, 2.0f, SignalGate::kOnHoldSeconds));
}

TEST_CASE("Noise never opens the gate", "[gate]")
{
    SignalGate g;
    REQUIRE_FALSE(feed(g, 1.0f, 60.0f));
}

TEST_CASE("Brief spikes do not open the gate", "[gate]")
{
    SignalGate g;
    for (int i = 0; i < 50; ++i)
    {
        feed(g, 2.0f, SignalGate::kOnHoldSeconds * 0.5f);
        feed(g, 1.0f, 0.5f);
    }
    REQUIRE_FALSE(g.present());
}

TEST_CASE("Gate closes only after significance stays low", "[gate]")
{
    SignalGate g;
    feed(g, 2.0f, 1.0f);
    REQUIRE(g.present());
    REQUIRE(feed(g, 1.0f, SignalGate::kOffHoldSeconds * 0.5f)); // still open
    REQUIRE_FALSE(feed(g, 1.0f, SignalGate::kOffHoldSeconds));  // now closed
}

TEST_CASE("Brief dips in the middle of music do not close the gate", "[gate]")
{
    SignalGate g;
    feed(g, 2.0f, 1.0f);
    for (int i = 0; i < 50; ++i)
    {
        feed(g, 1.0f, SignalGate::kOffHoldSeconds * 0.6f);
        feed(g, 2.0f, 0.3f);
    }
    REQUIRE(g.present());
}

TEST_CASE("Hysteresis: values between the thresholds keep the current state", "[gate]")
{
    const float between = 0.5f * (SignalGate::kOnThreshold + SignalGate::kOffThreshold);
    SignalGate closed;
    REQUIRE_FALSE(feed(closed, between, 10.0f));
    SignalGate open;
    feed(open, 2.0f, 1.0f);
    REQUIRE(feed(open, between, 10.0f));
}
