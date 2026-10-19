#pragma once
#include <cstdint>

enum class ActionKind : uint8_t {
    PlaceActive,   // index = hand position (Setup only)
    PlaceBench,    // index = hand position
    Evolve,        // index = hand position of the evolution card, target = Pokémon to evolve
    PlayTrainer,   // index = hand position of an Item, Supporter or Stadium
    AttachTool,    // index = hand position of the Tool, target = Pokémon
    AttachEnergy,  // target = Pokémon
    UseAbility,    // target = Pokémon whose "once during your turn" ability to use
    UseStadium,    // the Stadium in play's "once during each player's turn" action
    Retreat,       // index = bench position to swap in
    Attack,        // index = attack number on the Active
    EndTurn,       // during Setup: done placing
    Choose,        // index = option; asked inside an effect or after a knock-out
};
constexpr int ActionKindCount = 12;

struct Action {
    ActionKind kind;
    int8_t index = 0;
    int8_t target = 0;  // -1 = the Active, else a bench position
};
