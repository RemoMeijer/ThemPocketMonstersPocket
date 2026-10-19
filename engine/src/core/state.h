#pragma once
#include <array>
#include <cstdint>
#include <optional>

#include "cards/card.h"
#include "core/fixed_vec.h"
#include "core/rules.h"
#include "effects/modifier.h"

// Plain data only: a GameState is one flat block that can be copied (e.g. to look ahead later).

// Special Conditions as bits. Asleep, Paralyzed and Confused replace each other; Poisoned and Burned add on.
enum Status : uint8_t { Asleep = 1, Paralyzed = 2, Confused = 4, Poisoned = 8, Burned = 16 };
constexpr uint8_t ReplacingStatuses = Asleep | Paralyzed | Confused;

struct PokemonSlot {
    const CardDef* card = nullptr;  // the top card; nullptr = empty
    int damage = 0;
    EnergyCounts energy{};
    FixedVec<const CardDef*, 2> below{};  // the cards it evolved from, Basic first
    const CardDef* tool = nullptr;
    uint8_t status = 0;
    uint16_t uid = 0;           // identity while in play; timed effects point at it
    int enteredTurn = 0;        // turn it came into play or last evolved; 0 = during setup
    int becameActiveTurn = -1;  // turn the top card last moved into the Active Spot (evolving resets it)
    int abilityTurn = -1;       // turn it last used its "once during your turn" ability

    int energyCount() const {
        int n = 0;
        for (auto e : energy) n += e;
        return n;
    }
};

// A modifier that lasts a while, e.g. "during your opponent's next turn, this Pokémon takes −20 damage".
struct TimedEffect {
    Modifier mod;
    int8_t player = 0;  // whose Pokémon it is on, or which player it is for
    uint16_t uid = 0;   // the Pokémon it is on; 0 = the whole player (mod.scope says which of their Pokémon)
    int lastTurn = 0;   // it ends at the end of this turn
};

// Room for every card in the game: cards can change hands (Team Rocket's Thieving Machine).
constexpr std::size_t PileSize = 2 * rules::DeckSize;

struct PlayerState {
    FixedVec<const CardDef*, PileSize> deck;  // the last element is the top card
    FixedVec<const CardDef*, PileSize> hand;
    FixedVec<const CardDef*, PileSize> discard;
    PokemonSlot active;
    FixedVec<PokemonSlot, rules::BenchSize> bench;
    FixedVec<Type, rules::MaxEnergyTypes> energyTypes;
    std::optional<Type> energy;      // in the Energy Zone, not attached yet
    std::optional<Type> nextEnergy;  // shown in the Energy Zone; becomes `energy` at the start of the next turn
    EnergyCounts discardEnergy{};    // energy in the discard pile, per type (energy isn't a card)
    bool retreated = false;          // already retreated this turn
    int supporterTurn = -1;      // turn a Supporter was last played
    int stadiumTurn = -1;        // turn a Stadium was last played
    int points = 0;
};

enum class Phase : uint8_t {
    Setup,  // players place their Active and Bench, player 0 then player 1
    Main,   // current player's turn
    Over,
};

struct GameState {
    std::array<PlayerState, 2> players;
    Phase phase = Phase::Setup;
    int current = 0;  // whose turn it is (during Setup: who is placing)
    int first = 0;    // who went first
    int turn = 0;     // turns started so far, both players counted
    int winner = -1;  // set when Over; -1 = draw
    const CardDef* stadium = nullptr;
    int stadiumOwner = -1;
    FixedVec<TimedEffect, 16> timed;
    uint16_t nextUid = 1;
};
