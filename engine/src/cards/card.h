#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "effects/modifier.h"

enum class Type : uint8_t { Grass, Fire, Water, Lightning, Psychic, Fighting, Darkness, Metal, Dragon, Colorless };
constexpr int TypeCount = 10;

std::optional<Type> typeFromLetter(char letter);
std::optional<Type> typeFromName(const std::string& name);  // "Water" or "water"
const char* typeName(Type t);
char typeLetter(Type t);
constexpr uint16_t typeBit(Type t) { return uint16_t(1u << int(t)); }

enum Trait : uint8_t { UltraBeast = 1, Ancient = 2, Future = 4 };
uint8_t traitFromName(const std::string& name);  // "ultra-beast", "ancient", "future"; 0 if unknown

// Energy per type: what is attached to a Pokémon, or what an attack costs (Colorless = any type).
using EnergyCounts = std::array<uint8_t, TypeCount>;
bool canPay(const EnergyCounts& attached, const EnergyCounts& cost);

enum class Category : uint8_t { Pokemon, Trainer };
enum class Stage : uint8_t { Basic, Stage1, Stage2 };
enum class TrainerKind : uint8_t { None, Item, Supporter, Tool, Stadium };
constexpr uint8_t stageBit(Stage s) { return uint8_t(1u << int(s)); }

class Stmt;  // effects/effect.h
struct Node;

struct AttackDef {
    std::string name;
    EnergyCounts cost{};
    int damage = 0;           // the printed number ("30" in "30+" or "30×")
    std::string damageText;   // as printed: "30", "30+", "30×", or empty
    std::string effect;       // its text, empty for a plain attack
    const Stmt* script = nullptr;
};

struct AbilityDef {
    std::string name, effect;
};

// One card as printed. Read-only during a game; game state points at these.
struct CardDef {
    std::string id, name;
    Category category = Category::Pokemon;
    Stage stage = Stage::Basic;
    TrainerKind trainer = TrainerKind::None;
    int hp = 0;
    std::optional<Type> type, weakness;
    int retreat = 0;
    bool ex = false, mega = false;
    std::string evolvesFrom;       // name of the Pokémon it evolves from, empty for Basics
    std::string evolvesFromBasic;  // Stage 2 only: the Basic two steps down (for Rare Candy)
    std::vector<AttackDef> attacks;
    std::vector<AbilityDef> abilities;
    std::string effect;            // a Trainer's (or Fossil's) text
    int points = 1;                // points the opponent scores for knocking it out
    uint8_t traits = 0;            // Trait bits, from data/traits.tsv (Serebii): Ultra Beast, Ancient, Future
    int index = -1;                // position in CardDb::all(); -1 for a card outside a CardDb (tests)

    // Built from the card's effect scripts (data/effects.tsv).
    std::string scripts;               // the script text of all its rows, so a player can tell what a Trainer does
    std::vector<Modifier> passive;     // always on: its ability, or the Tool / Stadium card itself
    const Stmt* play = nullptr;        // Item or Supporter
    const Stmt* activated = nullptr;   // "Once during your turn, you may…"
    bool activatedWhileActive = false;
    const Stmt* endOfTurn = nullptr;   // "At the end of your turn…"
    bool endOfTurnWhileActive = false;
    const Stmt* onAttach = nullptr;    // "Whenever you attach a {T} Energy from your Energy Zone to this Pokémon…"
    const Stmt* onEvolve = nullptr;    // "When you play this Pokémon from your hand to evolve 1 of your Pokémon, you may…"
    const Stmt* onDamaged = nullptr;     // "If this Pokémon is in the Active Spot and is damaged by an attack from your opponent's…"
    const Stmt* endOfEachTurn = nullptr; // "At the end of each turn…" (both players' turns; before Checkup)
    const Stmt* stadiumAction = nullptr;  // a Stadium's "Once during each player's turn, that player may…"
    const Stmt* onKnockedOut = nullptr;  // "If this Pokémon is Knocked Out…": before its points and its cards leave play
    bool onKnockedOutWhileActive = false;  // only when Knocked Out in the Active Spot
    bool onKnockedOutByAttack = false;     // only by damage from an attack from the opponent's Pokémon
    const Stmt* onKnocksOut = nullptr;   // "If your opponent's Pokémon is Knocked Out by damage from this Pokémon's attacks…"
    const Stmt* duringCheckup = nullptr; // "During Pokémon Checkup…" (every Checkup, before Poison and Burn)
    bool duringCheckupWhileActive = false;
    const Stmt* startOfTurn = nullptr;   // "At the beginning of your turn…" (after the draw)
    bool startOfTurnWhileActive = false;
    const Stmt* onBench = nullptr;       // "When you put this Pokémon from your hand onto your Bench, you may…"
    const Stmt* onOppAttach = nullptr;   // "Whenever your opponent attaches an Energy from their Energy Zone…" (picked = that Pokémon)
    bool onOppAttachWhileActive = false;
    bool activatedWhileBenched = false;  // "if this Pokémon is on your Bench"
    bool activatedRepeat = false;        // "As often as you like during your turn"
    uint16_t onAttachTypes = 0;        // 0 = any type
    bool onAttachWhileActive = false;

    bool supported = false;   // the engine can play this card
    std::string unsupported;  // why not, for the coverage report
    std::string scriptError;  // a script that didn't build

    bool isPokemon() const { return category == Category::Pokemon; }
    bool isBasic() const { return isPokemon() && stage == Stage::Basic; }
    bool isFossil() const { return isPokemon() && !effect.empty(); }  // "Play this card as if it were a … Basic Pokémon"
    bool isEvolution() const { return isPokemon() && !evolvesFrom.empty(); }
    bool isTrainer(TrainerKind k) const { return category == Category::Trainer && trainer == k; }
    bool hasType(uint16_t mask) const { return !mask || (type && (mask & typeBit(*type))); }
    uint16_t extraTypes = 0;  // also these types while in play (Urshifu's Double Type); never for Weakness (ruling)
};

class CardDb {
public:
    // cards: data/cards.tsv from export_cards.py. scripts: data/effects.tsv from compile_effects.py,
    // or empty to load no effects (then only plain cards are supported).
    static CardDb load(const std::string& cards, const std::string& scripts = "");
    const CardDef* find(const std::string& id) const;
    const std::vector<CardDef>& all() const { return cards_; }
    std::size_t supportedCount() const;

private:
    void attachScripts(const std::string& path);  // effects/load_scripts.cpp

    std::vector<CardDef> cards_;
    std::unordered_map<std::string, std::size_t> byId_;
    std::vector<std::shared_ptr<const Node>> nodes_;  // owns every effect block the cards point at
};
