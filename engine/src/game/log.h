#pragma once
#include <iostream>
#include <string>

// Where a game reports what happens. Pass none to a Game and nothing is formatted at all.
class GameLog {
public:
    virtual ~GameLog() = default;
    virtual void line(const std::string& text) = 0;
};

class PrintLog : public GameLog {
public:
    void line(const std::string& text) override { std::cout << text << '\n'; }
};
