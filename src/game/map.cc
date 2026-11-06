#include "map.h"
#include "improvement.h"
#include "tile.h"
#include "unit.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

Map::Map(Game *game, int size, MapTypes::MapType mapType)
    : mapType{mapType}, game{game}, size{size} {
    tiles.reserve(size * size);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            tiles.emplace_back(game, x, y, TerrainTypes::FIELD);
        }
    }
}

// file format: "<size> <mapType>" followed by size * size terrain characters,
// row by row
Map::Map(Game *game, const std::string &fileName) : game{game} {
    std::ifstream file(fileName);
    if (!file) {
        throw std::runtime_error("Could not open map file " + fileName);
    }
    int mapInt;
    if (!(file >> size >> mapInt) || size <= 0 || mapInt < 0 ||
        mapInt >= MapTypes::MAP_TYPE_SIZE) {
        throw std::runtime_error("Invalid map header in " + fileName);
    }
    mapType = static_cast<MapTypes::MapType>(mapInt);

    tiles.reserve(size * size);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            tiles.emplace_back(game, x, y, TerrainTypes::FIELD);
        }
    }

    const auto &chars = TerrainTypes::terrainChars;
    std::string tileString;
    int i = 0;
    while (file >> tileString) {
        if (i >= size * size) {
            throw std::runtime_error("Too many tiles in " + fileName);
        }
        auto found = std::find(chars.begin(), chars.end(), tileString.at(0));
        if (found == chars.end()) {
            throw std::runtime_error("Unknown terrain '" + tileString +
                                     "' in " + fileName);
        }
        at(i % size, i / size)->terrainType =
            static_cast<TerrainTypes::TerrainType>(found - chars.begin());
        i++;
    }
    if (i != size * size) {
        throw std::runtime_error("Too few tiles in " + fileName);
    }
}

bool Map::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < size && y < size;
}

const Tile *Map::at(int x, int y) const {
    if (!inBounds(x, y)) {
        throw std::out_of_range("Tile (" + std::to_string(x) + ", " +
                                std::to_string(y) + ") is off the map");
    }
    return &tiles[x * size + y];
}

Tile *Map::at(int x, int y) {
    return const_cast<Tile *>(std::as_const(*this).at(x, y));
}

void Map::print() const {
    std::cout << "   ";
    for (int i = 0; i < size; i++) {
        std::cout << i << "  ";
        if (i < 10) {
            std::cout << ' ';
        }
    }
    std::cout << '\n';

    for (int y = 0; y < size; y++) {
        std::cout << y << ' ';
        if (y < 10) {
            std::cout << ' ';
        }

        for (int x = 0; x < size; x++) {
            const Tile *tile = at(x, y);
            std::cout << TerrainTypes::terrainChars.at(tile->terrainType);
            if (tile->unit) {
                std::cout << tile->unit->id;
            } else {
                std::cout << "  ";
            }
            std::cout << ' ';
        }
        std::cout << "\n   ";

        for (int x = 0; x < size; x++) {
            const Tile *tile = at(x, y);
            if (tile->improvement) {
                std::cout << tile->improvement->staticData.displayName << ' ';
            } else {
                std::cout << "   ";
            }
            // this will print improvement data eventually
            std::cout << ' ';
        }
        std::cout << '\n';
    }
}
