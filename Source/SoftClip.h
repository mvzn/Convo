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

namespace convo
{
    /** Final-output safety clipper: bit-transparent below the knee, tanh-saturated
        above it, asymptotic ceiling at 1.0 (never reaches it, never folds over).
        C1-continuous at the knee (tanh'(0) == 1), so engaging it adds nothing —
        no colouration, no level change — until the signal actually runs hot.
        Pure function, shared with the headless tests. */
    constexpr float kClipKnee = 0.75f;   // ~-2.5 dBFS

    inline float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kClipKnee)
            return x;

        constexpr float range = 1.0f - kClipKnee;
        const float y = kClipKnee + range * std::tanh ((a - kClipKnee) / range);
        return x > 0.0f ? y : -y;
    }
}
