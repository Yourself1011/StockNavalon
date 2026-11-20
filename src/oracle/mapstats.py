"""Compares the real game's generated maps with ours (docs/oracle.md).

    .venv/bin/python src/oracle/mapstats.py --type lakes --size 14 --maps 1000

Generates maps with the oracle (the real generator) and with polyengine's
generate_map for the same random tribe pairs, computes statistics of each map,
and prints, per statistic, both means, their gap in standard errors (Welch's
t) and a two-sample Kolmogorov-Smirnov test of the whole distribution.
Ours is the build tree's polyengine (POLYENGINE_BUILD_PACKAGE, by default
build/release/python), not the installed one, so it measures the engine as
built now.
"""

import argparse
import json
import math
import os
import random
import subprocess
import sys
from collections import deque

# as tests/python/conftest.py: the build tree's module, past an editable
# install's import hook
_package = os.environ.get("POLYENGINE_BUILD_PACKAGE", "build/release/python")
if os.path.isdir(_package):
    sys.path.insert(0, _package)
    sys.meta_path[:] = [
        finder for finder in sys.meta_path
        if not type(finder).__module__.startswith("_editable_skbc_polyengine")
    ]
import polyengine as pe  # noqa: E402

TRIBES = [t.name.lower() for t in pe.Tribe]
TERRAINS = ["field", "forest", "mountain", "water", "ocean"]
RESOURCES = ["fruit", "crop", "game", "metal", "fish", "starfish", "aquacrop"]
# polyengine's map types under the game's MapPreset names
PRESETS = {"drylands": "dryland", "waterworld": "waterworld"}


class Map:
    """One map in a form both sources share: per tile (index y * n + x)
    terrain, resource and building names, the capitals in seat order, and
    each tile's climate region by seat."""

    def __init__(self, n, terrain, resource, building, capitals, region):
        self.n, self.terrain, self.resource = n, terrain, resource
        self.building, self.capitals, self.region = building, capitals, region


def real_map(data, tribes):
    n = data["width"]
    terrain, resource, building = [None] * n * n, [None] * n * n, [None] * n * n
    climate = [None] * n * n
    capitals = {}
    for tile in data["tiles"]:
        t = tile["coordinates"]["y"] * n + tile["coordinates"]["x"]
        terrain[t] = tile["terrain"].lower()
        if tile["resource"]:
            resource[t] = tile["resource"]["type"].lower()
        if tile["improvement"]:
            kind = tile["improvement"]["type"].lower()
            building[t] = "village" if kind == "city" else kind
        if tile["capitalOf"]:
            capitals[tile["capitalOf"]] = t
        climate[t] = tile["climate"].lower()
    seats = [capitals[player] for player in sorted(capitals)]
    for t in seats:
        building[t] = "capital"
    region = [tribes.index(c) if c in tribes else -1 for c in climate]
    return Map(n, terrain, resource, building, seats, region)


def our_map(map_data):
    n = map_data.size
    terrain = [t.terrain.name.lower() for t in map_data.tiles]
    resource = [None if t.resource == pe.ResourceType.NONE
                else t.resource.name.lower() for t in map_data.tiles]
    building = [None if t.building == pe.ImprovementType.NONE
                else t.building.name.lower() for t in map_data.tiles]
    for t in map_data.capitals:
        building[t] = "capital"
    return Map(n, terrain, resource, building, list(map_data.capitals),
               list(map_data.regions))


def chebyshev(n, a, b):
    return max(abs(a % n - b % n), abs(a // n - b // n))


def components(n, member, diagonal):
    """Sizes of the connected groups of tiles where member is true."""
    seen = [False] * (n * n)
    sizes = []
    steps = [(dx, dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)
             if (dx or dy) and (diagonal or not (dx and dy))]
    for start in range(n * n):
        if seen[start] or not member[start]:
            continue
        seen[start] = True
        queue, size = deque([start]), 0
        while queue:
            t = queue.popleft()
            size += 1
            x, y = t % n, t // n
            for dx, dy in steps:
                if 0 <= x + dx < n and 0 <= y + dy < n:
                    u = (y + dy) * n + x + dx
                    if member[u] and not seen[u]:
                        seen[u] = True
                        queue.append(u)
        sizes.append(size)
    return sizes


def stats(m):
    n, tiles = m.n, m.n * m.n
    s = {}
    for kind in TERRAINS:
        s[f"share {kind}"] = sum(t == kind for t in m.terrain) / tiles
    water = [t in ("water", "ocean") for t in m.terrain]
    land = [not w for w in water]
    land_count = max(1, sum(land))
    s["forest share of land"] = sum(t == "forest" for t in m.terrain) / land_count
    s["mountain share of land"] = sum(t == "mountain" for t in m.terrain) / land_count
    for kind in RESOURCES:
        s[f"count {kind}"] = sum(r == kind for r in m.resource)
    for kind in ("village", "ruin", "lighthouse"):
        s[f"count {kind}"] = sum(b == kind for b in m.building)
    ruins = [t for t in range(tiles) if m.building[t] == "ruin"]
    s["ruins on water"] = sum(water[t] for t in ruins)

    water_groups = components(n, water, diagonal=False)
    s["water bodies"] = len(water_groups)
    s["largest water body"] = max(water_groups, default=0)
    s["water bodies of 1-2 tiles"] = sum(size <= 2 for size in water_groups)
    land_groups = components(n, land, diagonal=True)
    s["landmasses"] = len(land_groups)
    s["largest landmass share"] = max(land_groups, default=0) / land_count
    edge = [t for t in range(tiles)
            if min(t % n, t // n, n - 1 - t % n, n - 1 - t // n) == 0]
    s["edge ring land share"] = sum(land[t] for t in edge) / len(edge)

    caps = m.capitals
    if len(caps) >= 2:
        s["capital distance"] = chebyshev(n, caps[0], caps[1])
    per_capital = {}
    for c in caps:
        values = {
            "resources within 1": sum(
                m.resource[t] is not None and chebyshev(n, t, c) <= 1
                for t in range(tiles)),
            "resources within 2": sum(
                m.resource[t] is not None and chebyshev(n, t, c) <= 2
                for t in range(tiles)),
            "villages within 3": sum(
                m.building[t] == "village" and chebyshev(n, t, c) <= 3
                for t in range(tiles)),
            "water within 2": sum(
                water[t] and chebyshev(n, t, c) <= 2 for t in range(tiles)),
            "coastal capital": int(any(
                water[t] and chebyshev(n, t, c) == 1 for t in range(tiles))),
            "distance to edge": min(c % n, c // n, n - 1 - c % n, n - 1 - c // n),
        }
        for key, v in values.items():
            per_capital.setdefault(key, []).append(v)
    for key, vs in per_capital.items():
        s[f"capital: {key}"] = sum(vs) / len(vs)
        if len(vs) == 2:
            s[f"capital gap: {key}"] = abs(vs[0] - vs[1])
    # each seat's share of the land in its climate region
    shares = [sum(land[t] and m.region[t] == seat for t in range(tiles)) / land_count
              for seat in range(len(caps))]
    if shares:
        s["climate region land share, largest"] = max(shares)
    return s


def tribe_counts(m, tribes):
    """Per seat, counts inside its climate region: terrain of the land the
    generator chose freely (not settlements or corners), and resources by
    terrain, next to a settlement (inner) or two away (outer), leaving out
    the capital's own neighbours, where starting resources go; those are
    counted separately."""
    n, tiles = m.n, m.n * m.n
    settlements = [t for t in range(tiles) if m.building[t] in ("village", "capital")]
    out = []
    for seat, tribe in enumerate(tribes):
        c = {}
        def add(key, k=1):
            c[key] = c.get(key, 0) + k
        capital = m.capitals[seat]
        for t in range(tiles):
            if m.region[t] != seat:
                continue
            add("tiles")
            terrain = m.terrain[t]
            if terrain in ("water", "ocean"):
                add("water")
            corner = (t % n in (0, n - 1)) and (t // n in (0, n - 1))
            if t in settlements or corner:
                continue
            if terrain not in ("water", "ocean"):
                add("land")
                add(f"terrain {terrain}")
            if chebyshev(n, t, capital) == 1:
                if m.resource[t]:
                    add(f"start {m.resource[t]}")
                continue
            d = min((chebyshev(n, t, v) for v in settlements), default=n)
            if d > 2:
                continue
            band = "inner" if d == 1 else "outer"
            add(f"{terrain} {band}")
            if m.resource[t]:
                add(f"{terrain} {band} {m.resource[t]}")
        out.append((tribe, c))
    return out


# per-tribe rates: (name, numerator key, denominator key)
TRIBE_RATES = [
    ("water share of region", "water", "tiles"),
    ("field share of land", "terrain field", "land"),
    ("forest share of land", "terrain forest", "land"),
    ("mountain share of land", "terrain mountain", "land"),
] + [
    (f"{res} on {ter}, {band}", f"{ter} {band} {res}", f"{ter} {band}")
    for ter, ress in (("field", ("fruit", "crop")), ("forest", ("game",)),
                      ("mountain", ("metal",)), ("water", ("fish", "aquacrop")),
                      ("ocean", ("fish",)))
    for res in ress for band in ("inner", "outer")
]
START = ["fruit", "crop", "game", "metal", "fish"]


def by_tribe(real, ours, pairs, maps):
    """Prints, per tribe, each rate real against ours with its z-score."""
    totals = {}
    for source, items in (("real", real), ("ours", ours)):
        for m, pair in zip(items, pairs):
            for tribe, c in tribe_counts(m, pair):
                t = totals.setdefault((source, tribe), {"seats": 0})
                t["seats"] += 1
                for k, v in c.items():
                    t[k] = t.get(k, 0) + v
    print(f"{'tribe':10} {'rate':28} {'real':>7} {'ours':>7} {'z':>6}")
    for tribe in sorted({tr for _, tr in totals}):
        r, o = totals.get(("real", tribe)), totals.get(("ours", tribe))
        if not r or not o:
            continue
        rows = []
        for name, num, den in TRIBE_RATES:
            if not r.get(den) or not o.get(den):
                continue
            pr, po = r.get(num, 0) / r[den], o.get(num, 0) / o[den]
            se = math.sqrt(pr * (1 - pr) / r[den] + po * (1 - po) / o[den])
            z = (po - pr) / se if se > 0 else 0.0
            rows.append((name, pr, po, z))
        for res in START:
            pr = r.get(f"start {res}", 0) / r["seats"]
            po = o.get(f"start {res}", 0) / o["seats"]
            rows.append((f"start {res} per capital", pr, po, float("nan")))
        for name, pr, po, z in rows:
            flag = " *" if abs(z) > 4 else ""
            if pr == 0 and po == 0:
                continue
            print(f"{tribe:10} {name:28} {pr:7.3f} {po:7.3f} {z:6.1f}{flag}")
    print("* |z| > 4; start rows are counts per capital")


def ks(a, b):
    """Two-sample Kolmogorov-Smirnov statistic and asymptotic p-value."""
    a, b = sorted(a), sorted(b)
    i = j = 0
    d = 0.0
    while i < len(a) and j < len(b):
        x = min(a[i], b[j])
        while i < len(a) and a[i] == x:
            i += 1
        while j < len(b) and b[j] == x:
            j += 1
        d = max(d, abs(i / len(a) - j / len(b)))
    en = math.sqrt(len(a) * len(b) / (len(a) + len(b)))
    lam = (en + 0.12 + 0.11 / en) * d
    p = 2 * sum((-1) ** (k - 1) * math.exp(-2 * k * k * lam * lam)
                for k in range(1, 101))
    return d, min(1.0, max(0.0, p)) if lam > 0 else 1.0


def welch(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    va = sum((x - ma) ** 2 for x in a) / max(1, len(a) - 1)
    vb = sum((x - mb) ** 2 for x in b) / max(1, len(b) - 1)
    se = math.sqrt(va / len(a) + vb / len(b))
    return ma, mb, (mb - ma) / se if se > 0 else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--type", default="lakes",
                        choices=[t.name.lower() for t in pe.MapType])
    parser.add_argument("--size", type=int, default=14)
    parser.add_argument("--maps", type=int, default=500)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--oracle", default="build/release/oracle")
    parser.add_argument("--build", default="game/current")
    parser.add_argument("--save", help="write the real maps here, one JSON a line")
    parser.add_argument("--by-tribe", action="store_true",
                        help="terrain and resource rates inside each tribe's region")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    pairs = [rng.sample(TRIBES, 2) for _ in range(args.maps)]
    preset = PRESETS.get(args.type, args.type)
    requests = "".join(
        f"new {preset} {args.size} might crazy {a} {b}\nmap {i}\nfree {i}\n"
        for i, (a, b) in enumerate(pairs))
    out = subprocess.run([args.oracle, args.build], input=requests,
                         capture_output=True, text=True, check=True).stdout
    lines = [json.loads(line) for line in out.splitlines()]
    errors = [line["error"] for line in lines if "error" in line]
    if errors:
        raise SystemExit(f"oracle: {errors[0]}")
    real_data = [line for line in lines if line.get("$type") == "MapData"]
    if args.save:
        with open(args.save, "w") as f:
            for data, pair in zip(real_data, pairs):
                f.write(json.dumps({"tribes": pair, "map": data}) + "\n")

    map_type = pe.MapType[args.type.upper()]
    real_maps = [real_map(data, pair) for data, pair in zip(real_data, pairs)]
    our_maps = [our_map(pe.generate_map([pe.Tribe[a.upper()], pe.Tribe[b.upper()]],
                                        map_type, args.size, seed=args.seed + i))
                for i, (a, b) in enumerate(pairs)]
    if args.by_tribe:
        print(f"{args.type} {args.size}x{args.size}, {len(real_maps)} maps each, by tribe")
        by_tribe(real_maps, our_maps, pairs, len(real_maps))
        return
    real = [stats(m) for m in real_maps]
    ours = [stats(m) for m in our_maps]

    print(f"{args.type} {args.size}x{args.size}, {len(real)} real maps, "
          f"{len(ours)} of ours ({pe._engine.__file__})")
    print(f"{'statistic':42} {'real':>8} {'ours':>8} {'gap (SE)':>9} "
          f"{'KS D':>6} {'KS p':>8}")
    rows = []
    for key in real[0]:
        a = [s[key] for s in real if key in s]
        b = [s[key] for s in ours if key in s]
        if not a or not b:
            continue
        ma, mb, t = welch(a, b)
        d, p = ks(a, b)
        rows.append((p, key, ma, mb, t, d))
    for p, key, ma, mb, t, d in sorted(rows, key=lambda r: (r[0], -abs(r[4]))):
        flag = " *" if p < 0.001 else ""
        print(f"{key:42} {ma:8.3f} {mb:8.3f} {t:9.1f} {d:6.3f} {p:8.2g}{flag}")
    print("* differs (KS p < 0.001)")


if __name__ == "__main__":
    main()
