#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/action.h"
#include "core/view.h"

// What a network sees of one decision: one player's PlayerView (never a hidden zone) and the legal actions, as
// fixed-size padded arrays plus masks. A change to anything below that moves a number is a new EncodingVersion.
namespace enc {
constexpr int EncodingVersion = 1;

// --- The static card table: one row per card, row cardIndex(card); row 0 = no card ---
// Fixed columns, then one per script word (CardTable::words): its count in the card's scripts, capped at 3, /3.
constexpr int CatPokemon = 0;       // 5 columns: Pokémon, Item, Supporter, Tool, Stadium
constexpr int CatFossil = 5;
constexpr int CatStage = 6;         // 3: Basic, Stage 1, Stage 2 (Pokémon only)
constexpr int CatEx = 9, CatMega = 10;
constexpr int CatHp = 11;           // HP/100
constexpr int CatType = 12;         // 10, one-hot
constexpr int CatWeakness = 22;     // 10, one-hot
constexpr int CatRetreat = 32;      // Retreat Cost/4
constexpr int CatPoints = 33;       // points for knocking it out/3
constexpr int CatAttack = 34;       // 2 attacks × AttackColumns
constexpr int AttackColumns = 16;   // cost per Energy type/4 (10), damage/100, damage text plain/+/×/− (4), has effect text
constexpr int CatAbility = 66;
constexpr int CatTraits = 67;       // 3: Ultra Beast, Ancient, Future
constexpr int CardFixedFeatures = 70;

struct CardTable {
    std::vector<std::string> words;  // every unquoted, non-number atom in the cards' scripts, sorted
    std::vector<std::string> ids;    // card id of row i+1
    int rows = 0, cols = 0;          // cols = CardFixedFeatures + words.size()
    std::vector<float> values;       // rows × cols, row-major
};
CardTable staticFeatures(const CardDb& db);

inline int cardIndex(const CardDef* c) { return c ? c->index + 1 : 0; }  // 0 = none (or a card outside a CardDb)

// --- Tokens, at fixed positions; TokenType 0 = padding ---
constexpr int HandTokens = 10;
constexpr int TokGlobal = 0, TokHand = 1, TokMine = TokHand + HandTokens, TokOpp = TokMine + 4, TokStadium = TokOpp + 4;
constexpr int Tokens = TokStadium + 1;  // 20; Pokémon: Active, then Bench spot 0, 1, 2 (PokemonSlot::spot)
enum TokenType : int32_t { TNone, TGlobal, THand, TMyActive, TMyBench, TOppActive, TOppBench, TStadium };
constexpr int TokenTypeCount = 8;

// Token floats, one layout per token type (unused ones 0).
// Pokémon:
constexpr int PHpLeft = 0, PDamage = 1;  // /100
constexpr int PEnergy = 2;               // 10: attached per type/5
constexpr int PStatus = 12;              // 5: Asleep, Paralyzed, Confused, Poisoned, Burned
constexpr int PCanAttack = 17;           // 2: attack 0, 1 allowed and paid for now (as if Active)
constexpr int PEntered = 19, PEvolved = 20, PAbilityUsed = 21, PIsActive = 22;  // this turn; in the Active Spot
// Global (sizes and counts both players: mine, then the opponent's):
constexpr int GTurn = 0;               // turn/30
constexpr int GPoints = 1;             // 2: /3
constexpr int GHand = 3;               // 2: /10
constexpr int GDeck = 5;               // 2: /20
constexpr int GDiscard = 7;            // 2: cards/20
constexpr int GDiscardEnergy = 9;      // 20: Energy per type/10, mine then theirs
constexpr int GZone = 29, GNext = 39;  // 10 each: my Energy Zone's current and next type, one-hot
constexpr int GSupporterUsed = 49, GRetreated = 50, GNoEnergy = 51;  // this turn: Supporter played, retreated, no Energy to attach
constexpr int GOwnFirstTurn = 52, GMyTurn = 53;
// Stadium:
constexpr int SMine = 0, SUsed = 1;  // I played it; I used its action this turn
constexpr int TokenFloats = 54;

// --- Actions: one per legal action, in the game's order; kind 0 = padding ---
constexpr int Actions = 64;
constexpr int AAttack = 0;         // 8: attack number one-hot
constexpr int AChoice = 8;         // 5: ChoiceKind one-hot (Choose only)
constexpr int AEnergyType = 13;    // 10: an Energy type option, one-hot
constexpr int AOption = 23;        // Choose: option index/10
constexpr int ActionFloats = 24;

struct Observation {
    std::array<int32_t, Tokens> tokenType, tokenCard, tokenTool;  // tool: the first Tool on a Pokémon
    std::array<std::array<float, TokenFloats>, Tokens> tokenFloat;
    std::array<int32_t, Actions> actionKind;  // ActionKind + 1
    std::array<int32_t, Actions> actionCard;  // the hand card played, or a Choose option's card
    std::array<int32_t, Actions> actionPtr;   // token: the hand card, else the Pokémon (or Stadium); -1 none
    std::array<int32_t, Actions> actionPtr2;  // token: the Pokémon an Evolve or Tool goes on; -1 none
    std::array<std::array<float, ActionFloats>, Actions> actionFloat;
    int32_t legalActions;  // before cutting at Actions
    std::array<uint8_t, Tokens> tokenMask;
    std::array<uint8_t, Actions> actionMask;
};

// Needs a view a Game made (rule questions). Returns false when there were more than Actions legal actions.
bool encode(const PlayerView& view, const std::vector<Action>& actions, Observation& out);
}  // namespace enc
