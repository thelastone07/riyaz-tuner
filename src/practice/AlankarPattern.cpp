// src/practice/AlankarPattern.cpp
#include "AlankarPattern.h"

namespace
{
    std::vector<AlankarStep> ascendingFor (AlankarPatternId id)
    {
        switch (id)
        {
            case AlankarPatternId::Alankar1:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar2:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar3:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 },
                    { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar4:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 },
                    { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                    { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };

            case AlankarPatternId::Alankar5:
                return {
                    { Swar::Sa, 0 }, { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 },
                    { Swar::Re, 0 }, { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 },
                    { Swar::Ga, 0 }, { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 },
                    { Swar::Ma, 0 }, { Swar::Pa, 0 }, { Swar::Dha, 0 }, { Swar::Ni, 0 }, { Swar::Sa, 1 }
                };
        }

        jassertfalse; // unreachable - every AlankarPatternId enumerator is handled above
        return {};
    }

    juce::String nameFor (AlankarPatternId id)
    {
        switch (id)
        {
            case AlankarPatternId::Alankar1: return "Alankar 1";
            case AlankarPatternId::Alankar2: return "Alankar 2";
            case AlankarPatternId::Alankar3: return "Alankar 3";
            case AlankarPatternId::Alankar4: return "Alankar 4";
            case AlankarPatternId::Alankar5: return "Alankar 5";
        }

        jassertfalse; // unreachable - every AlankarPatternId enumerator is handled above
        return "Alankar";
    }
}

AlankarPattern::AlankarPattern (AlankarPatternId idIn) : id (idIn)
{
    auto ascending = ascendingFor (id);
    steps = ascending;

    // Descending is the exact reverse of ascending, appended directly. The
    // peak note (the topmost step of ascending) is genuinely held/
    // re-articulated twice in a row as a result - once as the last
    // ascending note, once as the first descending note - matching how
    // these patterns are actually notated and sung, not de-duplicated.
    for (auto it = ascending.rbegin(); it != ascending.rend(); ++it)
        steps.push_back (*it);
}

juce::String AlankarPattern::name() const
{
    return nameFor (id);
}

const std::vector<AlankarStep>& AlankarPattern::fullSequence() const
{
    return steps;
}
