#pragma once
#include <juce_core/juce_core.h>

enum class Swar { Sa, ReKomal, Re, GaKomal, Ga, Ma, MaTivra, Pa, DhaKomal, Dha, NiKomal, Ni };
enum class OctaveRegister { Mandra, Madhya, Taar, Other };

struct SwarLabel
{
    Swar swar;
    OctaveRegister octaveRegister;
    int octaveIndex;
    float centsFromCenter;
};

class SwarMapper
{
public:
    explicit SwarMapper (float hysteresisMarginCents = 15.0f);

    SwarLabel update (float centsFromSa);
    void reset();

private:
    float hysteresisMargin;
    bool hasLockedCenter = false;
    int lockedCenterCents = 0;

    SwarLabel labelForLockedCenter (float centsFromSa) const;
};

juce::String swarToString (Swar swar);
juce::String registerToString (OctaveRegister reg);
