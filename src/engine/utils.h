#pragma once

#include <bitset>
#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <vector>

template <std::size_t size, typename E>
void setBits(std::bitset<size> &bitset, const std::vector<E> &indices) {
    for (E index : indices) {
        bitset.set(index);
    }
}

template <std::size_t size>
std::bitset<size> makeBitset(std::initializer_list<int> indices) {
    std::bitset<size> bitset;
    for (int index : indices) {
        bitset.set(index);
    }
    return bitset;
}

struct PositionAccessor {
    int operator()(std::size_t pos) const { return static_cast<int>(pos); }
};

template <std::size_t size, typename Accessor> struct BitsetIndexIterator {
    using difference_type = std::ptrdiff_t;
    using value_type = std::invoke_result_t<Accessor, std::size_t>;

    const std::bitset<size> *bs;
    std::size_t index;
    Accessor accessor;

    value_type operator*() const { return accessor(index); }

    void advance() {
        while (index < size && !bs->test(index)) {
            ++index;
        }
    }

    BitsetIndexIterator &operator++() {
        ++index;
        advance();
        return *this;
    }
    BitsetIndexIterator operator++(int) {
        auto tmp = *this;
        ++*this;
        return tmp;
    }

    bool operator==(const BitsetIndexIterator &other) const {
        return bs == other.bs && index == other.index;
    }

    // should be done automatically but clang doesn't
    bool operator!=(const BitsetIndexIterator &other) const {
        return !(*this == other);
    }
};

// iterate over the set bits of a bitset, passing each index through accessor
template <std::size_t size, typename Accessor = PositionAccessor>
struct BitsetIndex {
    const std::bitset<size> &bs;
    Accessor accessor;

    BitsetIndexIterator<size, Accessor> begin() const {
        BitsetIndexIterator<size, Accessor> iterator{&bs, 0, accessor};
        iterator.advance();
        return iterator;
    }
    BitsetIndexIterator<size, Accessor> end() const {
        return {&bs, size, accessor};
    }

    BitsetIndex(const std::bitset<size> &bs, Accessor accessor = {})
        : bs{bs}, accessor{accessor} {}
};
