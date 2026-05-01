#include "unitData.h"
#include "utils.h"

namespace Units {
const UnitData warrior = {
    .type = UnitTypes::WARRIOR,
    .trainable = true,
    .cost = 2,
    .maxHealth = 10,
    .defence = 2,
    .movement = 1,
    .range = 1,
    .attack = 2,
    .id = "wa",
    .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
        {Abilities::DASH, Abilities::FORTIFY, Abilities::LAND})};
const UnitData rider = {.type = UnitTypes::RIDER,
                        .trainable = true,
                        .cost = 3,
                        .maxHealth = 10,
                        .defence = 1,
                        .movement = 2,
                        .range = 1,
                        .attack = 2,
                        .id = "ri",
                        .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
                            {Abilities::DASH, Abilities::ESCAPE,
                             Abilities::FORTIFY, Abilities::LAND})};
const UnitData knight = {.type = UnitTypes::KNIGHT,
                         .trainable = true,
                         .cost = 8,
                         .maxHealth = 10,
                         .defence = 1,
                         .movement = 3,
                         .range = 1,
                         .attack = 3.5,
                         .id = "kn",
                         .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
                             {Abilities::DASH, Abilities::PERSIST,
                              Abilities::FORTIFY, Abilities::LAND})};
const UnitData defender = {.type = UnitTypes::DEFENDER,
                           .trainable = true,
                           .cost = 3,
                           .maxHealth = 15,
                           .defence = 3,
                           .movement = 1,
                           .range = 1,
                           .attack = 1,
                           .id = "de",
                           .abilities =
                               *vectorToBitset<Abilities::ABILITY_SIZE>(
                                   {Abilities::FORTIFY, Abilities::LAND})};
const UnitData cloak = {
    .type = UnitTypes::CLOAK,
    .trainable = true,
    .cost = 3,
    .maxHealth = 15,
    .defence = 3,
    .movement = 1,
    .range = 1,
    .attack = 1,
    .id = "cl",
    .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
        {Abilities::HIDE, Abilities::CREEP, Abilities::INFILTRATE,
         Abilities::DASH, Abilities::SCOUT, Abilities::STIFF,
         Abilities::LAND})};
const UnitData mindBender = {.type = UnitTypes::MINDBENDER,
                             .trainable = true,
                             .cost = 5,
                             .maxHealth = 10,
                             .defence = 1,
                             .movement = 1,
                             .range = 1,
                             .attack = 0,
                             .id = "mb",
                             .abilities =
                                 *vectorToBitset<Abilities::ABILITY_SIZE>(
                                     {Abilities::HEAL, Abilities::CONVERT,
                                      Abilities::STIFF, Abilities::LAND})};
const UnitData swordsman = {.type = UnitTypes::SWORDSMAN,
                            .trainable = true,
                            .cost = 5,
                            .maxHealth = 15,
                            .defence = 3,
                            .movement = 1,
                            .range = 1,
                            .attack = 3,
                            .id = "sw",
                            .abilities =
                                *vectorToBitset<Abilities::ABILITY_SIZE>(
                                    {Abilities::DASH, Abilities::LAND})};
const UnitData raft = {.type = UnitTypes::RAFT,
                       .trainable = false,
                       .cost = 0,
                       .maxHealth = 10,
                       .defence = 1,
                       .movement = 2,
                       .range = 2,
                       .attack = 0,
                       .id = "rf",
                       .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
                           {Abilities::CARRY, Abilities::STATIC,
                            Abilities::STIFF, Abilities::WATER})};
const UnitData rammer = {.type = UnitTypes::RAMMER,
                         .trainable = false,
                         .cost = 5,
                         .maxHealth = 10,
                         .defence = 3,
                         .movement = 3,
                         .range = 1,
                         .attack = 3,
                         .id = "rm",
                         .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
                             {Abilities::DASH, Abilities::CARRY,
                              Abilities::STATIC, Abilities::WATER})};
const UnitData scout = {
    .type = UnitTypes::SCOUT,
    .trainable = false,
    .cost = 5,
    .maxHealth = 10,
    .defence = 3,
    .movement = 3,
    .range = 1,
    .attack = 3,
    .id = "sc",
    .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
        {Abilities::DASH, Abilities::CARRY, Abilities::SCOUT, Abilities::STATIC,
         Abilities::WATER})};
const UnitData bomber = {
    .type = UnitTypes::BOMBER,
    .trainable = false,
    .cost = 15,
    .maxHealth = 10,
    .defence = 2,
    .movement = 2,
    .range = 3,
    .attack = 3,
    .id = "bo",
    .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
        {Abilities::CARRY, Abilities::SPLASH, Abilities::STIFF,
         Abilities::STATIC, Abilities::WATER})};
const UnitData catapult = {.type = UnitTypes::CATAPULT,
                           .trainable = true,
                           .cost = 8,
                           .maxHealth = 10,
                           .defence = 0,
                           .movement = 1,
                           .range = 3,
                           .attack = 4,
                           .id = "ca",
                           .abilities =
                               *vectorToBitset<Abilities::ABILITY_SIZE>(
                                   {Abilities::STIFF, Abilities::LAND})};
const UnitData archer = {
    .type = UnitTypes::ARCHER,
    .trainable = true,
    .cost = 3,
    .maxHealth = 10,
    .defence = 1,
    .movement = 1,
    .range = 2,
    .attack = 2,
    .id = "ar",
    .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
        {Abilities::DASH, Abilities::FORTIFY, Abilities::LAND})};
const UnitData giant = {.type = UnitTypes::GIANT,
                        .trainable = false,
                        .cost = 10,
                        .maxHealth = 40,
                        .defence = 4,
                        .movement = 1,
                        .range = 1,
                        .attack = 5,
                        .id = "gi",
                        .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
                            {Abilities::STATIC, Abilities::LAND})};
const UnitData dagger = {
    .type = UnitTypes::DAGGER,
    .trainable = false,
    .cost = 2,
    .maxHealth = 10,
    .defence = 2,
    .movement = 1,
    .range = 1,
    .attack = 2,
    .id = "da",
    .abilities = *vectorToBitset<Abilities::ABILITY_SIZE>(
        {Abilities::DASH, Abilities::SURPRISE, Abilities::INDEPENDENT,
         Abilities::STATIC, Abilities::LAND})};
} // namespace Units
