#pragma once
#include <cstdint>

struct NameList;  // effects/effect.h
class Cond;

// An always-on change to the rules, from an ability, a Tool, the Stadium or a timed effect.
// Plain data, so timed effects can live in the game state.
enum class ModKind : uint8_t {
    DamageTaken,         // amount added to damage from attacks (negative = less); fromTypes limits the attacker
    DamageDealt,         // amount added to its attacks' damage to the opponent's Active Pokémon
    PreventDamage,       // prevent damage from attacks; amount > 0: only when that damage is amount or less
    PreventAll,          // prevent damage and effects of attacks
    PreventFlip,         // when damaged by an attack, flip: heads prevents that damage
    Reflect,             // when damaged by an attack while Active: amount of damage to the attacker
    Hp,                  // amount added to max HP
    RetreatCost,         // amount added to the Retreat Cost
    NoRetreatCost,
    CantRetreat,
    CantAttack,
    CantUseAttack,       // amount = which attack
    AttackCost,          // amount of extra Colorless energy its attacks cost
    AttackFlip,          // flip before attacking: tails, the attack doesn't happen
    CantPlaySupporter,   // player-wide
    CantPlayItem,        // player-wide
    CantAttachToActive,  // player-wide: no energy from the Energy Zone to their Active Pokémon
    WeaknessTimesTwo,    // the attacker's Weakness damage is ×2 instead of +20 (Bounded Field)
    NoConditions,        // can't be affected by Special Conditions (Steel Apron, Arceus ex)
    PreventEffects,      // prevent the effects (not the damage) of the opponent's attacks (Clear Veil, Regice)
    PreventAbilityDamage,  // prevent damage from the opponent's Abilities (Protective Poncho)
    ExtraTools,          // amount more Pokémon Tools may be attached to it (Revavroom)
    SurviveAt,           // would be Knocked Out by damage from an attack: isn't, and amount HP remains (Hala, Conkeldurr)
    ExtraPoint,          // its attack Knocks Out the opponent's Active Pokémon: amount more points (Iris)
};

// Who a modifier applies to, seen from the player who owns its source.
enum class ModScope : uint8_t { Self, MyActive, MyAll, OppActive, OppAll, All };

struct Modifier {
    ModKind kind{};
    int16_t amount = 0;
    ModScope scope = ModScope::Self;
    bool whileActive = false;  // only while its source is in the Active Spot
    bool whileBenched = false; // only while its source is on the Bench
    uint16_t types = 0;        // affected Pokémon must have one of these types (bit per Type; 0 = any)
    uint8_t stages = 0;        // affected Pokémon must be one of these stages (bit per Stage; 0 = any)
    uint16_t fromTypes = 0;    // DamageTaken: only damage from attackers of these types (0 = any)
    bool fromEx = false;       // DamageTaken: only damage from a Pokémon ex
    bool notMega = false;      // affected Pokémon must not be a Mega Evolution Pokémon ex
    uint8_t minRetreat = 0;    // affected Pokémon must have a Retreat Cost of at least this (Heavy Helmet)
    const NameList* names = nullptr;  // affected Pokémon must have one of these names (nullptr = any)
    bool vsEx = false;         // DamageDealt: only against a Pokémon ex
    const NameList* needs = nullptr;  // only while its owner has one of these Pokémon in play ("If you have Arceus…")
    const Cond* when = nullptr;       // only while this holds, asked about its source ("If this Pokémon has a Tool…")
    int8_t energy = -1;               // AttackCost: the Energy type it changes (-1 = Colorless)
    bool flip = false;                // only when a coin flip comes up heads (Conkeldurr)
};

inline bool playerWide(ModKind k) {
    return k == ModKind::CantPlaySupporter || k == ModKind::CantPlayItem || k == ModKind::CantAttachToActive;
}
