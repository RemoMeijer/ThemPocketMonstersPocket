#pragma once
#include <array>
#include <cassert>
#include <cstddef>

// A vector with a fixed capacity and no heap use, so a game state is one flat, cheap-to-copy block.
template <class T, std::size_t N>
class FixedVec {
public:
    std::size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    bool full() const { return n_ == N; }
    void clear() { n_ = 0; }

    T& operator[](std::size_t i) { return a_[i]; }
    const T& operator[](std::size_t i) const { return a_[i]; }
    T* begin() { return a_.data(); }
    T* end() { return a_.data() + n_; }
    const T* begin() const { return a_.data(); }
    const T* end() const { return a_.data() + n_; }
    T& back() { return a_[n_ - 1]; }
    const T& back() const { return a_[n_ - 1]; }

    void push_back(const T& v) { assert(n_ < N); a_[n_++] = v; }
    T pop_back() { assert(n_ > 0); return a_[--n_]; }

    // Removes element i and returns it, keeping the order of the rest.
    T take(std::size_t i) {
        assert(i < n_);
        T v = a_[i];
        for (; i + 1 < n_; ++i) a_[i] = a_[i + 1];
        --n_;
        return v;
    }

private:
    std::array<T, N> a_{};
    std::size_t n_ = 0;
};
