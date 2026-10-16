#include "effects/script.h"

#include <cctype>

namespace {
class Parser {
public:
    explicit Parser(const std::string& text) : t_(text) {}

    SExpr parse() {
        SExpr e = expr();
        skipSpace();
        if (i_ != t_.size()) fail("text after the end");
        return e;
    }

private:
    SExpr expr() {
        skipSpace();
        if (i_ >= t_.size()) fail("unexpected end");
        SExpr e;
        if (t_[i_] == '(') {
            ++i_;
            e.list = true;
            for (skipSpace(); i_ < t_.size() && t_[i_] != ')'; skipSpace()) e.items.push_back(expr());
            if (i_ >= t_.size()) fail("missing )");
            ++i_;
            if (e.items.empty() || e.items[0].list) fail("a list must start with a word");
        } else if (t_[i_] == '"') {
            std::size_t end = t_.find('"', i_ + 1);
            if (end == std::string::npos) fail("missing closing quote");
            e.atom = t_.substr(i_ + 1, end - i_ - 1);
            e.quoted = true;
            i_ = end + 1;
        } else if (t_[i_] == ')') {
            fail("unexpected )");
        } else {
            std::size_t start = i_;
            while (i_ < t_.size() && !std::isspace(static_cast<unsigned char>(t_[i_])) && t_[i_] != '(' && t_[i_] != ')')
                ++i_;
            e.atom = t_.substr(start, i_ - start);
        }
        return e;
    }

    void skipSpace() {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_;
    }

    [[noreturn]] void fail(const std::string& why) {
        throw ScriptError(why + " at position " + std::to_string(i_) + " in: " + t_);
    }

    const std::string& t_;
    std::size_t i_ = 0;
};
}  // namespace

const std::string& SExpr::head() const {
    static const std::string none;
    return list ? items[0].atom : none;
}

std::string SExpr::text() const {
    if (!list) return quoted ? "\"" + atom + "\"" : atom;
    std::string s = "(";
    for (std::size_t i = 0; i < items.size(); ++i) s += (i ? " " : "") + items[i].text();
    return s + ")";
}

SExpr parseScript(const std::string& text) { return Parser(text).parse(); }
