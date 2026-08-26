#pragma once

// Which taal (or plain click) the metronome is keeping. Four fixed patterns
// forever - not worth a generic vibhag-pattern parser (YAGNI).
enum class TaalType { PlainClick, Teentaal, Jhaptaal, Ektaal };

// How a single beat in the cycle is accented. Sam = beat 1, the start of the
// cycle. Clap = a vibhag (section) boundary that gets a clap accent. Khali =
// a vibhag boundary that is traditionally silent in real taal practice, but
// is rendered as a distinct, quieter/duller sound here (an explicit,
// user-confirmed decision for solo practice usability - see the spec).
// Plain = every other beat.
enum class BeatType { Sam, Clap, Khali, Plain };

// Stateless besides which taal it was constructed for - classify() answers
// "what kind of beat is index i" for that taal.
class TaalPattern
{
public:
    explicit TaalPattern (TaalType type);

    int beatCount() const;

    // beatIndex must be in [0, beatCount()) - a contract violation outside
    // that range, not a runtime case to handle gracefully (callers always
    // keep their beat index in range via modulo).
    BeatType classify (int beatIndex) const;

private:
    TaalType type;
};
