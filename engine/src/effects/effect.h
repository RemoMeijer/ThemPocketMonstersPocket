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
};
class Num : public Node {  // a number: 30, heads, (energy self)
public:
    virtual int eval(EffectCtx&) const = 0;
};
class Cond : public Node {  // a test: heads, (has-damage opp-active)
public:
    virtual bool test(EffectCtx&) const = 0;
};
class Target : public Node {  // which Pokémon: self, (choose opp bench)
public:
    virtual void select(EffectCtx&, Uids& out) const = 0;
};

class ScriptBuilder;
template <class T>
using Word = std::function<const T*(const SExpr&, ScriptBuilder&)>;

// Every word of the language, by kind. Each blocks_*.cpp file adds its own.
struct Words {
    std::unordered_map<std::string, Word<Stmt>> stmt;
    std::unordered_map<std::string, Word<Num>> num;
    std::unordered_map<std::string, Word<Cond>> cond;
    std::unordered_map<std::string, Word<Target>> target;
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
void hit(EffectCtx& ctx);                 // the attack's main damage to the opponent's Active Pokémon
bool affects(EffectCtx& ctx, uint16_t uid);  // false when an attack's effects are prevented on that Pokémon
