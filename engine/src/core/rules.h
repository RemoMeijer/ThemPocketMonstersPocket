#pragma once

// Every rule number in one place. "Unconfirmed" ones are guesses to check against the game.
namespace rules {
constexpr int DeckSize = 20;
constexpr int MaxCopies = 2;            // per card name
constexpr int MaxEnergyTypes = 3;
constexpr int OpeningHand = 5;
constexpr int BenchSize = 3;
constexpr int PointsToWin = 3;
constexpr int WeaknessBonus = 20;
constexpr int PoisonDamage = 10;        // assumed (standard TCG), at every Pokémon Checkup
constexpr int BurnDamage = 20;          // assumed (standard TCG), then a coin flip: heads recovers
constexpr int HandLimit = 10;           // unconfirmed: no draw while the hand is full
constexpr int TurnCap = 30;             // unconfirmed: turns of both players together, then a draw
constexpr bool FirstPlayerDrawsTurn1 = true;  // unconfirmed
constexpr bool SupporterOnFirstTurn = true;   // unconfirmed: may the player going first play a Supporter on turn 1?
}  // namespace rules
