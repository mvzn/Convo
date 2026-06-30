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

namespace convo
{
    /** In-place mid/side encode of one stereo frame block: mid = (L+R)/2 into the
        first pointer, side = (L-R)/2 into the second. Used by the audio-thread Bass Mono
        stage (encode, high-pass the side, decode) and exercised directly by the headless
        tests. Pure function. */
    inline void msEncode (float* L, float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]);
            L[i] = m;
            R[i] = s;
        }
    }

    /** Exact inverse of msEncode: L = M + S, R = M - S (the 0.5 factors live in the
        encode, so decode is gain-of-one and the round-trip is sample-exact). */
    inline void msDecode (float* M, float* S, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = M[i] + S[i];
            const float r = M[i] - S[i];
            M[i] = l;
            S[i] = r;
        }
    }
}
