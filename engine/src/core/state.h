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
    FixedVec<const CardDef*, 2> tools{};  // Pokémon Tools: one, or two with an Ability that allows it (Revavroom)
    uint8_t status = 0;
    uint16_t uid = 0;           // identity while in play; timed effects point at it
    int enteredTurn = 0;        // turn it came into play or last evolved; 0 = during setup
    int becameActiveTurn = -1;  // turn the top card last moved into the Active Spot (evolving resets it)
    int abilityTurn = -1;       // turn it last used its "once during your turn" ability
    int damagedTurn = -1;       // turn it last took damage from an attack while in the Active Spot
    bool koByEffect = false;    // "is Knocked Out" by an effect, not by damage (Bewear, Galarian Cursola)

    bool hasType(uint16_t mask) const { return card->hasType(mask) || (card->extraTypes & mask); }  // in play
    bool hitOnce = false;       // took damage from an attack since coming into play (Mimikyu ex)
    uint8_t baseStage = 0;      // the stage it came into play as: 1 for a Silcoon put straight onto the Bench (ruling)
    uint8_t poison = 0;         // Poison damage at Checkup, 0 = the usual (Toxicroak: 20); the latest Poison sets it (ruling)
    uint8_t spot = 0;           // on the Bench: which of the 3 fixed Bench spots it's in (App); unused for the Active
    bool attacked = false;      // used an attack since coming into play; evolving keeps it (Iron Bundle ex)

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

// "At the end of your opponent's next turn, do 90 damage to the Defending Pokémon" (Mismagius), or to "the
// Pokémon in the spot you chose" (Meowscarada ex), or Knock it Out (Armaldo). Not a TimedEffect: it outlives
// clearConditions and its attacker (ruling).
struct LaterEffect {
    uint16_t uid = 0;    // that Pokémon, wherever it is then; 0 = whoever is in the spot below
    int8_t player = 0;   // by spot: whose side
    int8_t spot = -1;    // by spot: -1 the Active Spot, 0..2 a Bench spot
    int16_t damage = 0;  // plain damage, no Weakness or damage changes (ruling); 0 = Knock Out
    int turn = 0;        // it happens at the end of this turn
};

struct AttackUse {  // how often a player's Pokémon used attacks of this name in this game (Alcremie)
    const AttackDef* attack = nullptr;
    uint8_t count = 0;
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
    int stadiumUseTurn = -1;     // turn this player last used a Stadium's action…
    const CardDef* stadiumUsed = nullptr;  // …and which Stadium that was
    int points = 0;
    // Memory of earlier turns, for "during your (opponent's) last turn".
    int koTurn = -1;            // turn one of their Pokémon was last Knocked Out by damage from an attack…
    uint16_t koTypes = 0;       // …and the types of the Pokémon Knocked Out that turn (bit per Type)
    int attackTurn = -1;        // turn they last used an attack…
    const AttackDef* lastAttack = nullptr;  // …and which one
    int scoredTurn = -1;        // turn they last got points…
    int scored = 0;             // …and how many that turn
    int koCount = 0;            // times their Pokémon were Knocked Out this game
    FixedVec<AttackUse, 24> attackUses;  // attacks used this game, counted by name (Alcremie)
    int firstHeadsTurn = -1;    // turn whose next effect coin flip is heads (Will)
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
    FixedVec<LaterEffect, 8> later;
    uint16_t nextUid = 1;
};
