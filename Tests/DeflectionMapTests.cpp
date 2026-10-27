#include "Azimuth/DeflectionMap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using azimuter::DeflectionMap;

namespace
{
constexpr float kTol = 1.0e-5f;
}

TEST_CASE("Deflection is zero at lock", "[deflection]")
{
    REQUIRE_THAT(DeflectionMap::fromMicroseconds(0.0f), Catch::Matchers::WithinAbs(0.0f, kTol));
}

TEST_CASE("Pitch floor sits exactly at half deflection", "[deflection]")
{
    // Half deflection is where the Sonifier's pitch bottoms out and the pulse
    // begins. Both cues hinge on this point, so it is pinned to the constant.
    REQUIRE_THAT(DeflectionMap::fromMicroseconds(DeflectionMap::pitchFloorMicroseconds),
                 Catch::Matchers::WithinAbs(0.5f, kTol));
}

TEST_CASE("Fine region keeps the resolution of the old 50 us full scale", "[deflection]")
{
    // Below the pitch floor the map must stay identical to the previous
    // linear delayUs/50 mapping — widening full scale is allowed to extend
    // the coarse range, never to dilute fine alignment.
    for (float us : {1.0f, 2.5f, 5.0f, 10.0f, 17.5f, 24.0f})
        REQUIRE_THAT(DeflectionMap::fromMicroseconds(us), Catch::Matchers::WithinAbs(us / 50.0f, kTol));
}

TEST_CASE("Full scale is reached at the documented delay", "[deflection]")
{
    REQUIRE_THAT(DeflectionMap::fromMicroseconds(DeflectionMap::fullScaleMicroseconds),
                 Catch::Matchers::WithinAbs(1.0f, kTol));
}

TEST_CASE("Deflection clamps beyond full scale", "[deflection]")
{
    REQUIRE_THAT(DeflectionMap::fromMicroseconds(400.0f), Catch::Matchers::WithinAbs(1.0f, kTol));
    REQUIRE_THAT(DeflectionMap::fromMicroseconds(-400.0f), Catch::Matchers::WithinAbs(-1.0f, kTol));
}

TEST_CASE("Measured drift range stays inside full scale", "[deflection]")
{
    // A single C-90 side was measured drifting 55-75 us. The old 50 us full
    // scale saturated inside that range, making 50 us and 90 us sound alike.
    REQUIRE(DeflectionMap::fromMicroseconds(55.0f) < DeflectionMap::fromMicroseconds(75.0f));
    REQUIRE(DeflectionMap::fromMicroseconds(75.0f) < 1.0f);
}

TEST_CASE("Deflection is odd-symmetric in the sign of the delay", "[deflection]")
{
    for (float us : {0.5f, 12.0f, 25.0f, 60.0f, 99.0f})
        REQUIRE_THAT(DeflectionMap::fromMicroseconds(-us),
                     Catch::Matchers::WithinAbs(-DeflectionMap::fromMicroseconds(us), kTol));
}

TEST_CASE("Deflection increases monotonically with delay", "[deflection]")
{
    float previous = -1.0f;
    for (int i = 0; i <= 200; ++i)
    {
        const float d = DeflectionMap::fromMicroseconds(static_cast<float>(i) * 0.5f);
        REQUIRE(d >= previous);
        previous = d;
    }
}
