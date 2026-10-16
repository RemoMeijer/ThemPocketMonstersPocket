#pragma once
#include <stdexcept>
#include <string>
#include <vector>

// One piece of an effect script: an atom such as `heal`, `30`, `self` or `"Onix"`, or a list such as
// `(heal self 30)`. Scripts are written by compile_effects.py (see DESIGN.md §4).
struct SExpr {
    std::string atom;
    std::vector<SExpr> items;
    bool list = false;
    bool quoted = false;  // the atom was written as "…"

    const std::string& head() const;  // a list's first atom
    std::string text() const;         // back to script text, for messages
};

struct ScriptError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

SExpr parseScript(const std::string& text);  // throws ScriptError on bad syntax

inline SExpr atomExpr(std::string atom) {
    SExpr e;
    e.atom = std::move(atom);
    return e;
}
