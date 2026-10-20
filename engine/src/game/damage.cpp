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

int Game::dealAttackDamage(int attacker, uint16_t attackerUid, uint16_t targetUid, int base, bool weakness, bool ignoreDefender) {
    int owner = 0;
    PokemonSlot* target = find(targetUid, &owner);
    PokemonSlot* source = find(attackerUid);
    if (!target || !source) return 0;
    bool active = target == &s_.players[owner].active;
    int damage = base;
    forEachMod(ModKind::DamageDealt, source, attacker, [&](const Modifier& m) {  // :any-target: Benched ones too
        if ((active || m.anyTarget) && (!m.vsEx || target->card->ex))
            damage += m.perPoint ? m.amount * s_.players[attacker].points : m.amount;
    });
    // Weakness counts whenever the attack does damage, even after step 2 took it to 0 or below: the floor
    // is only at the end (official battle FAQ, DESIGN.md §5).
    bool weak = weakness && active && base > 0 && target->card->weakness && source->card->type == target->card->weakness &&
                (ignoreDefender || !hasMod(ModKind::NoWeakness, *target, owner));
    if (weak) damage = hasMod(ModKind::WeaknessTimesTwo, *source, attacker) ? damage * 2 : damage + rules::WeaknessBonus;
    if (!ignoreDefender) forEachMod(ModKind::DamageTaken, target, owner, [&](const Modifier& m) {
        if (source->hasType(m.fromTypes) && (!m.fromEx || source->card->ex) && (!m.flip || (damage > 0 && flip())))
            damage += m.amount;  // :flip: "If any damage is done to this Pokémon by attacks, flip a coin" (Bastiodon)
    });
    bool prevented = !ignoreDefender && hasMod(ModKind::PreventAll, *target, owner);
    if (!ignoreDefender) forEachMod(ModKind::PreventDamage, target, owner, [&](const Modifier& m) {
        prevented |= (!m.amount || damage <= m.amount) && (!m.fromEx || source->card->ex) &&
                     (!m.fromBasic || source->card->stage == Stage::Basic);
    });
    if (!ignoreDefender && !prevented && damage > 0 && !target->hitOnce && hasMod(ModKind::PreventFirstHit, *target, owner)) prevented = true;
    if (damage > 0) target->hitOnce = true;  // "first damaged by an attack after coming into play" (Mimikyu ex)
    if (!ignoreDefender && !prevented && damage > 0 && hasMod(ModKind::PreventFlip, *target, owner)) prevented = flip();
    damage = prevented ? 0 : std::max(0, damage);
    target->damage += damage;
    if (log_)
        say("  ", damage, weak ? " damage (weakness)" : " damage", prevented ? " (prevented)" : "", " -> ",
            show(*target, owner));
    if (damage > 0 && active) {
        target->damagedTurn = s_.turn;
        int reflect = sumMods(ModKind::Reflect, *target, owner);
        if (reflect) {
            source->damage += reflect;
            if (log_) say("  ", reflect, " damage back -> ", show(*source, attacker));
        }
        // "If this Pokémon is in the Active Spot and is damaged by an attack…": its ability, then its Tools.
        forEachSource(targetUid, [&](const CardDef* c) {
            if (!c->onDamaged) return;
            say("  ", c->name, " reacts");
            EffectCtx ctx{*this, owner};
            ctx.self = targetUid;
            ctx.attacker = attackerUid;
            ctx.card = c;
            c->onDamaged->run(ctx);
        });
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
    return p && (hasMod(ModKind::PreventAll, *p, owner) || hasMod(ModKind::PreventEffects, *p, owner));
}

void Game::attachFromZone(int player, uint16_t uid, Type type, int count) {
    PokemonSlot* target = find(uid);
    if (!target || count <= 0) return;
    target->energy[int(type)] += count;
    say("P", player + 1, " attaches ", count, " ", typeName(type), " energy to ", target->card->name);
    int owner = 0;
    find(uid, &owner);
    // "…if they attach Energy from their Energy Zone to the Defending Pokémon, that Pokémon will be Asleep" (Gothitelle):
    // any attach from the Zone, by any card (ruling)
    if (target == &s_.players[owner].active && hasMod(ModKind::AsleepOnAttach, *target, owner) &&
        !(blockedStatuses(*target, owner) & Asleep)) {
        target->status = uint8_t((target->status & ~ReplacingStatuses) | Asleep);
        say("  -> ", show(*target, owner));
    }
    // "Whenever you attach a {T} Energy from your Energy Zone to this Pokémon…"
    const CardDef* card = target->card;
    if (card->onAttach && (!card->onAttachTypes || (card->onAttachTypes & typeBit(type))) &&
        (!card->onAttachWhileActive || target == &s_.players[owner].active) && !abilitiesOff(*target)) {
        EffectCtx ctx{*this, owner};
        ctx.self = uid;
        card->onAttach->run(ctx);
    }
    // The other player's "whenever your opponent attaches an Energy from their Energy Zone…" (Jolteon ex): every
    // attach from the Zone, by an effect too (ruling).
    PlayerState& other = s_.players[1 - owner];
    Uids watchers;
    if (other.active.card) watchers.push_back(other.active.uid);
    for (const PokemonSlot& b : other.bench) watchers.push_back(b.uid);
    for (uint16_t w : watchers) {
        const PokemonSlot* p = find(w);
        if (!p || !p->card->onOppAttach || abilitiesOff(*p) || (p->card->onOppAttachWhileActive && p != &other.active)) continue;
        say("  ", p->card->name, " reacts");
        EffectCtx ctx{*this, 1 - owner};
        ctx.self = w;
        ctx.picked = uid;
        p->card->onOppAttach->run(ctx);
    }
}
