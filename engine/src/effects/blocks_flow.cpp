// Flow: sequences, conditions, coin flips, repeats; and plain numbers and tests.
#include <vector>

#include "effects/effect.h"
#include "game/game.h"

namespace {
class Seq : public Stmt {
public:
    explicit Seq(std::vector<const Stmt*> parts) : parts_(std::move(parts)) {}
    void run(EffectCtx& ctx) const override {
        for (const Stmt* p : parts_) {
            if (ctx.stopped) return;
            p->run(ctx);
        }
    }

private:
    std::vector<const Stmt*> parts_;
};

class If : public Stmt {
public:
    If(const Cond* c, const Stmt* then, const Stmt* otherwise) : c_(c), then_(then), else_(otherwise) {}
    void run(EffectCtx& ctx) const override {
        if (c_->test(ctx))
            then_->run(ctx);
        else if (else_)
            else_->run(ctx);
    }

private:
    const Cond* c_;
    const Stmt* then_;
    const Stmt* else_;
};

// (flip N): flip N coins. (flip-until-tails): keep flipping until tails. Both set heads and tails.
class Flip : public Stmt {
public:
    Flip(const Num* n, bool untilTails) : n_(n), untilTails_(untilTails) {}
    void run(EffectCtx& ctx) const override {
        ctx.heads = ctx.tails = 0;
        if (untilTails_) {
            while (ctx.heads < 100 && ctx.game.flip()) ++ctx.heads;  // 100: just a safety stop
            ctx.tails = 1;
        } else {
            for (int i = n_->eval(ctx); i > 0; --i) ++(ctx.game.flip() ? ctx.heads : ctx.tails);
        }
    }

private:
    const Num* n_;
    bool untilTails_;
};

class Repeat : public Stmt {
public:
    Repeat(const Num* n, const Stmt* body) : n_(n), body_(body) {}
    void run(EffectCtx& ctx) const override {
        for (int i = n_->eval(ctx); i > 0 && !ctx.stopped; --i) body_->run(ctx);
    }

private:
    const Num* n_;
    const Stmt* body_;
};

// (pick TARGET STMT): choose one Pokémon once, then refer to it as `picked` inside STMT.
class Pick : public Stmt {
public:
    Pick(const Target* t, const Stmt* body) : t_(t), body_(body) {}
    void run(EffectCtx& ctx) const override {
        Uids chosen;
        t_->select(ctx, chosen);
        if (chosen.empty()) return;
        ctx.picked = chosen[0];
        body_->run(ctx);
    }

private:
    const Target* t_;
    const Stmt* body_;
};

class Nothing : public Stmt {  // reveals and looks: no effect on the game state
public:
    void run(EffectCtx&) const override {}
};

class Coins : public Num {
public:
    explicit Coins(bool heads) : heads_(heads) {}
    int eval(EffectCtx& ctx) const override { return heads_ ? ctx.heads : ctx.tails; }

private:
    bool heads_;
};

class Arith : public Num {
public:
    Arith(char op, const Num* a, const Num* b) : op_(op), a_(a), b_(b) {}
    int eval(EffectCtx& ctx) const override {
        int a = a_->eval(ctx), b = b_->eval(ctx);
        return op_ == '*' ? a * b : op_ == '+' ? a + b : a - b;
    }

private:
    char op_;
    const Num* a_;
    const Num* b_;
};

// heads: at least one heads. tails: at least one tails. all-heads / all-tails: every coin.
class CoinTest : public Cond {
public:
    explicit CoinTest(int kind) : kind_(kind) {}
    bool test(EffectCtx& ctx) const override {
        switch (kind_) {
            case 0: return ctx.heads > 0;
            case 1: return ctx.tails > 0;
            case 2: return ctx.heads > 0 && ctx.tails == 0;
            default: return ctx.tails > 0 && ctx.heads == 0;
        }
    }

private:
    int kind_;
};

class Logic : public Cond {
public:
    Logic(char op, const Cond* a, const Cond* b) : op_(op), a_(a), b_(b) {}
    bool test(EffectCtx& ctx) const override {
        if (op_ == '!') return !a_->test(ctx);
        return op_ == '&' ? a_->test(ctx) && b_->test(ctx) : a_->test(ctx) || b_->test(ctx);
    }

private:
    char op_;
    const Cond* a_;
    const Cond* b_;
};

class Compare : public Cond {
public:
    Compare(const std::string& op, const Num* a, const Num* b) : op_(op), a_(a), b_(b) {}
    bool test(EffectCtx& ctx) const override {
        int a = a_->eval(ctx), b = b_->eval(ctx);
        return op_ == ">=" ? a >= b : op_ == "<=" ? a <= b : a == b;
    }

private:
    std::string op_;
    const Num* a_;
    const Num* b_;
};
}  // namespace

void addFlowWords(Words& w) {
    w.stmt["seq"] = [](const SExpr& e, ScriptBuilder& b) {
        std::vector<const Stmt*> parts;
        for (std::size_t i = 0; i < argCount(e); ++i) parts.push_back(b.stmt(arg(e, i)));
        return b.make<Seq>(std::move(parts));
    };
    w.stmt["if"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<If>(b.cond(arg(e, 0)), b.stmt(arg(e, 1)), argCount(e) > 2 ? b.stmt(arg(e, 2)) : nullptr);
    };
    w.stmt["flip"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<Flip>(argCount(e) ? b.num(arg(e, 0)) : b.num(atomExpr("1")), false);
    };
    w.stmt["flip-until-tails"] = [](const SExpr&, ScriptBuilder& b) { return b.make<Flip>(nullptr, true); };
    w.stmt["repeat"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Repeat>(b.num(arg(e, 0)), b.stmt(arg(e, 1))); };
    w.stmt["pick"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Pick>(b.target(arg(e, 0)), b.stmt(arg(e, 1))); };
    w.stmt["nothing"] = [](const SExpr&, ScriptBuilder& b) { return b.make<Nothing>(); };

    w.num["heads"] = [](const SExpr&, ScriptBuilder& b) { return b.make<Coins>(true); };
    w.num["tails"] = [](const SExpr&, ScriptBuilder& b) { return b.make<Coins>(false); };
    for (const char* op : {"*", "+", "-"})
        w.num[op] = [](const SExpr& e, ScriptBuilder& b) {
            return b.make<Arith>(e.head()[0], b.num(arg(e, 0)), b.num(arg(e, 1)));
        };

    const char* coins[] = {"heads", "tails", "all-heads", "all-tails"};
    for (int i = 0; i < 4; ++i) w.cond[coins[i]] = [i](const SExpr&, ScriptBuilder& b) { return b.make<CoinTest>(i); };
    w.cond["not"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Logic>('!', b.cond(arg(e, 0)), nullptr); };
    w.cond["and"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Logic>('&', b.cond(arg(e, 0)), b.cond(arg(e, 1))); };
    w.cond["or"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Logic>('|', b.cond(arg(e, 0)), b.cond(arg(e, 1))); };
    for (const char* op : {">=", "<=", "="})
        w.cond[op] = [](const SExpr& e, ScriptBuilder& b) {
            return b.make<Compare>(e.head(), b.num(arg(e, 0)), b.num(arg(e, 1)));
        };
}
