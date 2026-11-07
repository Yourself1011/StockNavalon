#include "view.h"

PlayerView makeView(const Game &game, int player, bool everything) {
    PlayerView view;
    view.player = player;
    view.size = game.size;
    view.turn = game.turn;
    view.currentPlayer = game.currentPlayer;
    view.winner = game.winner;
    view.stars = game.players[player].stars;
    view.income = game.income(player);
    view.techs = game.players[player].techs;
    for (std::size_t i = 0; i < game.players.size(); i++) {
        const Player &p = game.players[i];
        int other = static_cast<int>(i);
        bool known = everything || other == player ||
                     game.players[player].hasMet(other);
        view.tribes.push_back(p.tribe);
        view.alive.push_back(p.alive);
        view.met.push_back(game.players[player].hasMet(other));
        view.scores.push_back(known ? game.score(other) : -1);
    }

    view.tiles.resize(game.tiles.size());
    for (std::size_t i = 0; i < game.tiles.size(); i++) {
        int tile = static_cast<int>(i);
        const Tile &t = game.tiles[i];
        PlayerView::TileView &tv = view.tiles[i];
        if (!everything && !game.explored(player, tile)) {
            continue;
        }
        tv.explored = true;
        tv.terrain = t.terrain;
        if (everything || game.resourceVisible(player, t.resource)) {
            tv.resource = t.resource;
        }
        tv.building = t.building;
        tv.level = t.level;
        tv.road = t.road;
        tv.owner = game.tileOwner(tile);

        int city = game.cityAt(tile);
        if (city >= 0) {
            const City &c = game.cities[city];
            tv.city = static_cast<int>(view.cities.size());
            view.cities.push_back({c.tile, c.owner, c.level, c.population,
                                   c.capitalOf, c.wall});
        }

        const Unit *unit = game.unitAt(tile);
        if (unit && (everything || game.canSee(player, *unit))) {
            PlayerView::UnitView uv{.type = unit->type,
                                    .passenger = unit->passenger,
                                    .owner = unit->owner,
                                    .tile = tile,
                                    .hp = unit->hp,
                                    .maxHp = unit->maxHp,
                                    .kills = unit->kills,
                                    .veteran = unit->veteran,
                                    .hidden = unit->hidden};
            if (unit->owner == player) {
                uv.canMove = unit->canMove;
                uv.canAttack = unit->canAttack;
                uv.canAct = unit->canAct;
                uv.hiddenEnemyNear = game.hiddenEnemyNear(player, tile);
            }
            tv.unit = static_cast<int>(view.units.size());
            view.units.push_back(uv);
        }
    }
    return view;
}
