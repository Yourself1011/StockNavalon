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

#endif // !UTILS_H
