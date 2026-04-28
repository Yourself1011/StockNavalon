#ifndef UTILS_H
#define UTILS_H

#include <bitset>
#include <cstddef>
#include <vector>

template <size_t size, typename E>
void vectorToBitset(std::bitset<size> *bitset, std::vector<E> vector) {
    for (int index : vector) {
        bitset->set(index);
    }
};

template <size_t size>
std::bitset<size> *vectorToBitset(std::vector<int> vector) {
    std::bitset<size> *bitset = new std::bitset<size>(0);

    for (int index : vector) {
        bitset->set(index);
    }

    return bitset;
};

struct PositionAccessor {
    int operator()(int pos) const { return pos; }
};

template <size_t size, typename Accessor> struct BitsetIndexIterator {
    using difference_type = std::ptrdiff_t;
    using value_type = std::invoke_result_t<Accessor, int>;

    const std::bitset<size> *bs;
    int index;
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

template <size_t size, typename Accessor = PositionAccessor>
struct BitsetIndex {
    const std::bitset<size> &bs;
    Accessor accessor;

    BitsetIndexIterator<size, Accessor> begin() {
        BitsetIndexIterator<size, Accessor> iterator{&bs, 0, accessor};
        iterator.advance();
        return iterator;
    }
    BitsetIndexIterator<size, Accessor> end() { return {&bs, size, accessor}; }

    BitsetIndex(const std::bitset<size> &bs, Accessor accessor = {})
        : bs{bs}, accessor{accessor} {}
};

#endif // !UTILS_H
