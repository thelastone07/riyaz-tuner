#include "TaalPattern.h"
#include <juce_core/juce_core.h>

TaalPattern::TaalPattern (TaalType typeIn) : type (typeIn)
{
}

int TaalPattern::beatCount() const
{
    switch (type)
    {
        case TaalType::PlainClick: return 1;
        case TaalType::Teentaal:   return 16;
        case TaalType::Jhaptaal:   return 10;
        case TaalType::Ektaal:     return 12;
    }

    jassertfalse; // unreachable - every TaalType enumerator is handled above
    return 1;
}

BeatType TaalPattern::classify (int beatIndex) const
{
    jassert (beatIndex >= 0 && beatIndex < beatCount());

    switch (type)
    {
        case TaalType::PlainClick:
            return BeatType::Plain;

        case TaalType::Teentaal:
            if (beatIndex == 0) return BeatType::Sam;
            if (beatIndex == 8) return BeatType::Khali;
            if (beatIndex == 4 || beatIndex == 12) return BeatType::Clap;
            return BeatType::Plain;

        case TaalType::Jhaptaal:
            if (beatIndex == 0) return BeatType::Sam;
            if (beatIndex == 2) return BeatType::Khali;
            if (beatIndex == 5 || beatIndex == 7) return BeatType::Clap;
            return BeatType::Plain;

        case TaalType::Ektaal:
            if (beatIndex == 0) return BeatType::Sam;
            if (beatIndex == 2 || beatIndex == 6) return BeatType::Khali;
            if (beatIndex == 4 || beatIndex == 8 || beatIndex == 10) return BeatType::Clap;
            return BeatType::Plain;
    }

    jassertfalse; // unreachable - every TaalType enumerator is handled above
    return BeatType::Plain;
}
