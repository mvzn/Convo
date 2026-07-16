/*
    Convo — a convolution audio effect plugin
    Copyright (C) 2026 mvzn

    This program is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option) any
    later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License along
    with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <cmath>
#include <algorithm>

namespace convo
{
    /** Equal-power Mix law (the "Link/Mix" toggle in VOLUME): the mix percentage
        (0 = dry only, 100 = wet only) sweeps the constant-power circle
        dry^2 + wet^2 = 1 in the LINEAR gain domain — the correct crossfade law for
        two largely uncorrelated signals, keeping the combined loudness constant.
        While linked the processor derives both gains from the mix param and leaves
        the dry/wet params untouched, so unlinking restores the pre-link balance.
        Both legs live in [0, 1]: linked mode can never push either side above
        0 dB (a wet knob above unity has no on-circle partner — the seed clamps).
        Pure functions, shared with the headless tests. */
    constexpr float  kMixLinkFloorDb = -60.0f;              // the params' mute floor
    constexpr double kMixHalfPi      = 1.57079632679489662;

    inline float mixToDryGain (float mixPct) noexcept
    {
        const double t = std::clamp ((double) mixPct, 0.0, 100.0) * 0.01;
        return (float) std::cos (t * kMixHalfPi);
    }

    inline float mixToWetGain (float mixPct) noexcept
    {
        const double t = std::clamp ((double) mixPct, 0.0, 100.0) * 0.01;
        return (float) std::sin (t * kMixHalfPi);
    }

    /** Seed for engaging the link: the mix percentage whose wet leg matches the given
        Wet knob dB, so the wet you were hearing is preserved and only the dry snaps
        onto the circle. At/below the -60 dB floor -> 0%; at/above 0 dB -> 100%
        (no on-circle partner above unity, so the wet clamps to 0 dB when linked). */
    inline float wetDbToMixPct (float wetDb) noexcept
    {
        const double g = wetDb <= kMixLinkFloorDb ? 0.0 : std::pow (10.0, (double) wetDb * 0.05);
        return (float) (std::asin (std::clamp (g, 0.0, 1.0)) / kMixHalfPi * 100.0);
    }
}
