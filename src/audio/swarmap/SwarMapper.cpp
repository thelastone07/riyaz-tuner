#include "SwarMapper.h"
#include <cmath>

namespace
{
    int floorDiv (int a, int b)
    {
        int q = a / b;
        int r = a % b;
        if (r != 0 && ((r < 0) != (b < 0)))
            --q;
        return q;
    }

    int nearestCenterCents (float cents)
    {
        return (int) std::lround (cents / 100.0f) * 100;
    }
}

SwarMapper::SwarMapper (float hysteresisMarginCents)
    : hysteresisMargin (hysteresisMarginCents)
{
}

SwarLabel SwarMapper::labelForLockedCenter (float centsFromSa) const
{
    const int semitoneTotal = lockedCenterCents / 100;
    const int octaveIndex = floorDiv (semitoneTotal, 12);
    const int semitoneIndex = semitoneTotal - octaveIndex * 12;

    OctaveRegister reg = OctaveRegister::Other;
    if (octaveIndex == -1) reg = OctaveRegister::Mandra;
    else if (octaveIndex == 0) reg = OctaveRegister::Madhya;
    else if (octaveIndex == 1) reg = OctaveRegister::Taar;

    return SwarLabel {
        (Swar) semitoneIndex,
        reg,
        octaveIndex,
        centsFromSa - (float) lockedCenterCents
    };
}

SwarLabel SwarMapper::update (float centsFromSa)
{
    if (! hasLockedCenter)
    {
        lockedCenterCents = nearestCenterCents (centsFromSa);
        hasLockedCenter = true;
    }

    return labelForLockedCenter (centsFromSa);
}

void SwarMapper::reset()
{
    hasLockedCenter = false;
    lockedCenterCents = 0;
}

juce::String swarToString (Swar swar)
{
    switch (swar)
    {
        case Swar::Sa:       return "S";
        case Swar::ReKomal:  return "r";
        case Swar::Re:       return "R";
        case Swar::GaKomal:  return "g";
        case Swar::Ga:       return "G";
        case Swar::Ma:       return "m";
        case Swar::MaTivra:  return "M'";
        case Swar::Pa:       return "P";
        case Swar::DhaKomal: return "d";
        case Swar::Dha:      return "D";
        case Swar::NiKomal:  return "n";
        case Swar::Ni:       return "N";
    }
    return "?";
}

juce::String registerToString (OctaveRegister reg)
{
    switch (reg)
    {
        case OctaveRegister::Mandra: return "mandra";
        case OctaveRegister::Madhya: return "madhya";
        case OctaveRegister::Taar:   return "taar";
        case OctaveRegister::Other:  return "other";
    }
    return "?";
}
