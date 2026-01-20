#include "map.h"
#include "tile.h"
#include "unit.h"
#include <iostream>
#include <stdexcept>
#include <vector>

Map::Map(int size, MapTypes::MapType mapType) : size{size}, mapType{mapType} {
    tiles = std::vector<Tile>();
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            tiles.push_back(Tile(x, y, TerrainTypes::FIELD));
        }
    }
};

Tile *Map::at(int x, int y) {
    if (x >= size || y >= size) {
        throw std::out_of_range("Matrix index out of range");
    }
    return &tiles.at(x * size + y);
};

void Map::print() {
    std::cout << "   ";
    for (int i = 0; i < size; i++) {
        std::cout << i << "  ";
        if (i < 10) {
            std::cout << ' ';
        }
    }
    std::cout << std::endl;

    for (int y = 0; y < size; y++) {
        std::cout << y << ' ';
        if (y < 10) {
            std::cout << ' ';
        }

        for (int x = 0; x < size; x++) {
            Tile *tile = at(x, y);
            std::cout << (char)tile->terrainType;
            if (tile->unit) {
                std::cout << tile->unit->id;
            } else {
                std::cout << "  ";
            }
            std::cout << ' ';
        }
        std::cout << std::endl << "   ";

        for (int x = 0; x < size; x++) {
            Tile *tile = at(x, y);
            // this will print improvement data eventually
            std::cout << "   ";
            std::cout << ' ';
        }
        std::cout << std::endl;
    }
}
