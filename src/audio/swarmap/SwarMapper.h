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

    // Cents are relative to the calibrated tonic (Sa). This class is
    // single-thread-owned (no internal synchronization) and allocates
    // nothing on update() - safe to call from a real-time thread.
    //
    // No-pitch/unvoiced contract: do NOT call update() for an unvoiced or
    // not-yet-available frame. Simply skip the call - the locked swar stays
    // put, which is what lets the hysteresis lock survive a brief pause in
    // singing without flickering. Do not call reset() on unvoiced frames;
    // reset() is for a genuinely fresh start (e.g. a new practice session
    // or re-calibration), not for silence. (See PitchPipeline, which
    // implements exactly this: it only calls update() when the pitch
    // engine reports a confident, non-nullopt frequency.)
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
float centsFromSaForSwar (Swar swar, int octaveOffset);
