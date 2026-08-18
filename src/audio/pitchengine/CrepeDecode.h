#pragma once

struct CrepeDecodeResult
{
    float frequencyHz;
    float confidence;
};

// Decodes one frame's 360 sigmoid-activated pitch-bin probabilities into a
// (frequency, confidence) pair using CREPE's weighted-local-average method.
CrepeDecodeResult decodeCrepeOutput (const float* probabilities, int numBins);
