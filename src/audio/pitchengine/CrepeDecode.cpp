#include "CrepeDecode.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kCentsPerBin = 20.0f;
    constexpr float kCentsOffset = 1997.3794084376191f;

    float binToCents (int bin)
    {
        return kCentsPerBin * (float) bin + kCentsOffset;
    }
}

CrepeDecodeResult decodeCrepeOutput (const float* probabilities, int numBins)
{
    if (numBins <= 0)
        return { 0.0f, 0.0f }; // no bins to read — safe default (silence/no confidence)

    int argmaxBin = 0;
    float maxVal = probabilities[0];
    for (int i = 1; i < numBins; ++i)
    {
        if (probabilities[i] > maxVal)
        {
            maxVal = probabilities[i];
            argmaxBin = i;
        }
    }

    const int start = std::max (0, argmaxBin - 4);
    const int end = std::min (numBins, argmaxBin + 5); // exclusive

    float weightedCentsSum = 0.0f;
    float probSum = 0.0f;
    for (int i = start; i < end; ++i)
    {
        const float p = std::max (0.0f, probabilities[i]); // ReLU
        weightedCentsSum += binToCents (i) * p;
        probSum += p;
    }

    const float cents = probSum > 0.0f ? weightedCentsSum / probSum : binToCents (argmaxBin);
    const float frequencyHz = 10.0f * std::pow (2.0f, cents / 1200.0f);

    return { frequencyHz, probabilities[argmaxBin] };
}
