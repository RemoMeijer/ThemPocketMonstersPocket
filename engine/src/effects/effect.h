#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cards/card.h"
#include "core/fixed_vec.h"
#include "core/rules.h"
#include "effects/modifier.h"
#include "effects/script.h"

class Game;
struct PokemonSlot;

// Everything a running effect needs.
struct EffectCtx {
    Game& game;
    int player;                // who controls the effect
    uint16_t self = 0;         // the Pokémon using the attack or ability (0 for a Trainer)
    uint16_t defending = 0;    // the opponent's Active Pokémon when the attack started
    bool attack = false;       // part of an attack
    const AttackDef* attackDef = nullptr;  // that attack
    int damage = 0;            // the attack's damage so far; (hit) deals it
    int heads = 0, tails = 0;  // the last coin flips
    bool stopped = false;      // "this attack does nothing": the rest is skipped
    uint16_t picked = 0;       // the Pokémon chosen by (pick …)
    bool healed = false;       // (heal …) removed some damage
    bool endsTurn = false;     // (end-turn): "Your turn ends." after the effect
    uint16_t attacker = 0;     // the Attacking Pokémon, for "when damaged by an attack" effects
    const CardDef* card = nullptr;  // the card whose text runs, for a Tool's "discard this card"
    bool noPoints = false;       // (no-points): the opponent gets no points for this knock-out
    bool toHandInstead = false;  // (to-hand-instead): the knocked-out Pokémon goes to the hand (Rescue Scarf), or the
                                 // Item just played does (Lucky Ice Pop)
    int dealt = 0;             // damage (hit) did, for "heal the same amount"
    int copyDepth = 0;         // inside (copy-attack): a copied attack doesn't copy again
};

using Uids = FixedVec<uint16_t, 2 * (1 + rules::BenchSize)>;  // at most every Pokémon in play

// The building blocks. Each word of the script language is one small class (effects/blocks_*.cpp),
// built once when the cards load and shared by every game.
struct Node {
    virtual ~Node() = default;
};

// Pokémon names a modifier is limited to, e.g. "your Ninetales, Rapidash, or Magmar".
struct NameList : Node {
    std::vector<std::string> names;
    bool has(const std::string& name) const {
        for (const std::string& n : names)
            if (n == name) return true;
        return false;
    }
};
class Stmt : public Node {  // does something
public:
    virtual void run(EffectCtx&) const = 0;
    // Whether it has something to act on right now. A Trainer can only be played when its effect does (App,
    // DESIGN.md §10). Blocks with nothing to check say yes.
    virtual bool possible(EffectCtx&) const { return true; }
};
class Num : public Node {  // a number: 30, heads, (energy self)
public:
    virtual int eval(EffectCtx&) const = 0;
};
class Cond : public Node {  // a test: heads, (has-damage opp-active)
public:
    virtual bool test(EffectCtx&) const = 0;
    virtual bool usesCoins() const { return false; }  // decided by coins that haven't been flipped yet
};
class Target : public Node {  // which Pokémon: self, (choose opp bench)
public:
    virtual void select(EffectCtx&, Uids& out) const = 0;
    virtual void candidates(EffectCtx&, Uids& out) const = 0;  // every Pokémon it could pick, without asking anyone
};

class ScriptBuilder;
template <class T>
using Word = std::function<const T*(const SExpr&, ScriptBuilder&)>;

// One word: how many arguments it takes, and how to build it. A script with another number of
// arguments doesn't load, so nothing in it is silently ignored.
constexpr std::size_t AnyCount = 99;
template <class T>
struct Entry {
    std::size_t min = 0, max = 0;
    Word<T> build;
};

// Every word of the language, by kind. Each blocks_*.cpp file adds its own.
struct Words {
    std::unordered_map<std::string, Entry<Stmt>> stmt;
    std::unordered_map<std::string, Entry<Num>> num;
    std::unordered_map<std::string, Entry<Cond>> cond;
    std::unordered_map<std::string, Entry<Target>> target;
};
void addFlowWords(Words&);
void addTargetWords(Words&);
void addDamageWords(Words&);
void addEnergyWords(Words&);
void addCardWords(Words&);

// Turns parsed script text into blocks. An unknown word throws ScriptError, which leaves the card unsupported.
class ScriptBuilder {
public:
    explicit ScriptBuilder(std::vector<std::shared_ptr<const Node>>& arena) : arena_(arena) {}

    const Stmt* stmt(const SExpr& e);
    const Num* num(const SExpr& e);
    const Cond* cond(const SExpr& e);
    const Target* target(const SExpr& e);
    Modifier modifier(const SExpr& e, ModScope defaultScope = ModScope::Self);

    template <class T, class... Args>
    const T* make(Args&&... args) {
        auto node = std::make_shared<T>(std::forward<Args>(args)...);
        arena_.push_back(node);
        return node.get();
    }

private:
    std::vector<std::shared_ptr<const Node>>& arena_;
};

// Helpers for the blocks.
[[noreturn]] void scriptFail(const std::string& why, const SExpr& e);
const SExpr& arg(const SExpr& e, std::size_t i);  // the i-th argument of a list (0 = first after the word)
std::size_t argCount(const SExpr& e);
int intArg(const SExpr& e);
Type typeArg(const SExpr& e);  // `water` → Type::Water
uint16_t typeMask(const SExpr& e, std::size_t from);  // type atoms from argument `from` on
bool oppArg(const SExpr& e);  // `my` → false, `opp` → true
uint8_t parseStatuses(const SExpr& e, std::size_t from);  // asleep, paralyzed, confused, poisoned, burned, any
// The attack's main damage to the opponent's Active Pokémon; "isn't affected by Weakness / by any effects on your
// opponent's Active Pokémon" (all of them, ruling) turn the last two off.
void hit(EffectCtx& ctx, bool weakness = true, bool ignoreDefender = false);
bool affects(EffectCtx& ctx, uint16_t uid);  // false when an attack's effects are prevented on that Pokémon
// Whether some Pokémon that t could pick passes test (for Stmt::possible).
bool anyCandidate(EffectCtx& ctx, const Target* t, const std::function<bool(const PokemonSlot&)>& test);
int randomEnergy(EffectCtx& ctx, const PokemonSlot& p, uint16_t types);  // a random attached energy type (weighted), or -1
