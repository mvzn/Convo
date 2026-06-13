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
