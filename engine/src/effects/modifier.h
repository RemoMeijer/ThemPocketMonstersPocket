#pragma once
#include <cstdint>

struct NameList;  // effects/effect.h

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
};

// Who a modifier applies to, seen from the player who owns its source.
enum class ModScope : uint8_t { Self, MyActive, MyAll, OppActive, OppAll, All };

struct Modifier {
    ModKind kind{};
    int16_t amount = 0;
    ModScope scope = ModScope::Self;
    bool whileActive = false;  // only while its source is in the Active Spot
    uint16_t types = 0;        // affected Pokémon must have one of these types (bit per Type; 0 = any)
    uint8_t stages = 0;        // affected Pokémon must be one of these stages (bit per Stage; 0 = any)
    uint16_t fromTypes = 0;    // DamageTaken: only damage from attackers of these types (0 = any)
    bool fromEx = false;       // DamageTaken: only damage from a Pokémon ex
    const NameList* names = nullptr;  // affected Pokémon must have one of these names (nullptr = any)
    bool vsEx = false;         // DamageDealt: only against a Pokémon ex
    const NameList* needs = nullptr;  // only while its owner has one of these Pokémon in play ("If you have Arceus…")
};

inline bool playerWide(ModKind k) {
    return k == ModKind::CantPlaySupporter || k == ModKind::CantPlayItem || k == ModKind::CantAttachToActive;
}
