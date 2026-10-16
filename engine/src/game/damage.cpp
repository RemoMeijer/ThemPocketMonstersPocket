// Damage: the one place where an attack's damage is worked out, and where energy from the Energy Zone lands.
#include <algorithm>

#include "effects/effect.h"
#include "game/game.h"

PokemonSlot* Game::find(uint16_t uid, int* owner) {
    if (!uid) return nullptr;
    for (int p = 0; p < 2; ++p) {
        PlayerState& ps = s_.players[p];
        PokemonSlot* hit = ps.active.uid == uid && ps.active.card ? &ps.active : nullptr;
        for (PokemonSlot& b : ps.bench)
            if (b.uid == uid) hit = &b;
        if (hit) {
            if (owner) *owner = p;
            return hit;
        }
    }
    return nullptr;
}

int Game::dealAttackDamage(int attacker, uint16_t attackerUid, uint16_t targetUid, int base, bool weakness) {
    int owner = 0;
    PokemonSlot* target = find(targetUid, &owner);
    PokemonSlot* source = find(attackerUid);
    if (!target || !source) return 0;
    bool active = target == &s_.players[owner].active;
    int damage = base;
    if (active)
        forEachMod(ModKind::DamageDealt, source, attacker, [&](const Modifier& m) {
            if (!m.vsEx || target->card->ex) damage += m.amount;
        });
    // Weakness counts whenever the attack does damage, even after step 2 took it to 0 or below: the floor
    // is only at the end (official battle FAQ, DESIGN.md §5).
    bool weak = weakness && active && base > 0 && target->card->weakness && source->card->type == target->card->weakness;
    if (weak) damage += rules::WeaknessBonus;
    forEachMod(ModKind::DamageTaken, target, owner, [&](const Modifier& m) {
        if (source->card->hasType(m.fromTypes) && (!m.fromEx || source->card->ex)) damage += m.amount;
    });
    bool prevented = hasMod(ModKind::PreventAll, *target, owner);
    forEachMod(ModKind::PreventDamage, target, owner, [&](const Modifier& m) { prevented |= !m.amount || damage <= m.amount; });
    if (!prevented && damage > 0 && hasMod(ModKind::PreventFlip, *target, owner)) prevented = flip();
    damage = prevented ? 0 : std::max(0, damage);
    target->damage += damage;
    if (log_)
        say("  ", damage, weak ? " damage (weakness)" : " damage", prevented ? " (prevented)" : "", " -> ",
            show(*target, owner));
    if (damage > 0 && active) {
        int reflect = sumMods(ModKind::Reflect, *target, owner);
        if (reflect) {
            source->damage += reflect;
            if (log_) say("  ", reflect, " damage back -> ", show(*source, attacker));
        }
    }
    return damage;
}

void Game::putDamage(uint16_t uid, int amount) {
    int owner = 0;
    if (PokemonSlot* p = find(uid, &owner)) {
        p->damage += amount;
        if (log_) say("  ", amount, " damage -> ", show(*p, owner));
    }
}

bool Game::protectedFromAttacks(uint16_t uid) {
    int owner = 0;
    PokemonSlot* p = find(uid, &owner);
    return p && hasMod(ModKind::PreventAll, *p, owner);
}

void Game::attachFromZone(int player, uint16_t uid, Type type, int count) {
    PokemonSlot* target = find(uid);
    if (!target || count <= 0) return;
    target->energy[int(type)] += count;
    say("P", player + 1, " attaches ", count, " ", typeName(type), " energy to ", target->card->name);
    // "Whenever you attach a {T} Energy from your Energy Zone to this Pokémon…"
    int owner = 0;
    find(uid, &owner);
    const CardDef* card = target->card;
    if (card->onAttach && (!card->onAttachTypes || (card->onAttachTypes & typeBit(type))) &&
        (!card->onAttachWhileActive || target == &s_.players[owner].active)) {
        EffectCtx ctx{*this, owner};
        ctx.self = uid;
        card->onAttach->run(ctx);
    }
}
