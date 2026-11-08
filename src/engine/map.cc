#include "map.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace {

template <typename Array> int indexOf(const Array &chars, char c) {
    auto found = std::find(chars.begin(), chars.end(), c);
    return found == chars.end() ? -1 : static_cast<int>(found - chars.begin());
}

void parseToken(const std::string &token, int index, MapData &map) {
    int terrain = indexOf(TerrainTypes::terrainChars, token.at(0));
    if (terrain < 0) {
        throw std::runtime_error("Unknown terrain in map token '" + token +
                                 "'");
    }
    Tile &tile = map.tiles[index];
    tile.terrain = static_cast<TerrainTypes::TerrainType>(terrain);

    for (std::size_t i = 1; i < token.size(); i++) {
        char c = token[i];
        if (c == '^') {
            if (i + 1 >= token.size() || token[i + 1] < '0' || token[i + 1] > '9') {
                throw std::runtime_error("^ needs a player in map token '" +
                                         token + "'");
            }
            map.regions.resize(map.size * map.size, -1);
            map.regions[index] = token[++i] - '0';
            continue;
        }
        int resource = indexOf(ResourceTypes::resourceChars, c);
        if (resource > 0) {
            tile.resource = static_cast<ResourceTypes::ResourceType>(resource);
        } else if (c == 'V') {
            tile.building = ImprovementTypes::VILLAGE;
        } else if (c == 'R') {
            tile.building = ImprovementTypes::RUIN;
        } else if (c == 'L') {
            tile.building = ImprovementTypes::LIGHTHOUSE;
        } else if (c == '=') {
            tile.road = true;
        } else if (c >= '0' && c <= '9') {
            std::size_t player = c - '0';
            if (map.capitals.size() <= player) {
                map.capitals.resize(player + 1, -1);
            }
            map.capitals[player] = index;
        } else {
            throw std::runtime_error("Unknown flag in map token '" + token +
                                     "'");
        }
    }
}

} // namespace

MapData parseMap(std::istream &in) {
    MapData map;
    int type;
    if (!(in >> map.size >> type) || map.size <= 0 || type < 0 ||
        type >= MapTypes::MAP_TYPE_SIZE) {
        throw std::runtime_error("Invalid map header");
    }
    map.type = static_cast<MapTypes::MapType>(type);
    map.tiles.resize(map.size * map.size);

    std::string token;
    int i = 0;
    while (in >> token) {
        if (i >= map.size * map.size) {
            throw std::runtime_error("Too many tiles in map");
        }
        parseToken(token, i, map);
        i++;
    }
    if (i != map.size * map.size) {
        throw std::runtime_error("Too few tiles in map");
    }
    if (std::find(map.capitals.begin(), map.capitals.end(), -1) !=
        map.capitals.end()) {
        throw std::runtime_error("Map capitals must be numbered from 0");
    }
    return map;
}

MapData loadMap(const std::string &fileName) {
    std::ifstream file(fileName);
    if (!file) {
        throw std::runtime_error("Could not open map file " + fileName);
    }
    return parseMap(file);
}
