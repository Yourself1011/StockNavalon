#ifndef UTILS_H
#define UTILS_H

#include <bitset>
#include <vector>

template <size_t size, typename E>
void vectorToBitset(std::bitset<size> *bitset, std::vector<E> vector) {
    for (int index : vector) {
        bitset->set(index);
    }
};

template <size_t size>
std::bitset<size> *vectorToBitset(std::vector<int> vector) {
    std::bitset<size> bitset = new std::bitset<size>(0);

    for (int index : vector) {
        bitset->set(index);
    }

    return bitset;
};

#endif // !UTILS_H
