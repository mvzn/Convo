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
