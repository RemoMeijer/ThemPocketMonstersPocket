#pragma once

// Every rule number in one place. DESIGN.md §10 lists every rule and where it comes from.
namespace rules {
constexpr int DeckSize = 20;
constexpr int MaxCopies = 2;            // per card name
constexpr int MaxEnergyTypes = 3;
constexpr int OpeningHand = 5;
constexpr int BenchSize = 3;
constexpr int PointsToWin = 3;
constexpr int WeaknessBonus = 20;
constexpr int PoisonDamage = 10;        // at every Pokémon Checkup
constexpr int BurnDamage = 20;          // at every Pokémon Checkup, then a coin flip: heads recovers
constexpr int HandLimit = 10;           // no draw while the hand is full
constexpr int TurnCap = 30;             // turns of both players together, then a tie
constexpr bool FirstPlayerDrawsTurn1 = true;
constexpr bool SupporterOnFirstTurn = true;   // the player going first may play a Supporter on turn 1
}  // namespace rules
