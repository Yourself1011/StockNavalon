# Game

This game is a c++ replica of Polytopia. It exists to allow us to train a bot faster than with the real game. As such, no graphics are needed, just the logic.

## General Overview

Polytopia is a turn-based strategy game. All actions happen on a grid. Players gain stars at the start of their turn, and may use those stars to take actions.

## Actions

### Unit actions

A unit, on its turn, may move, according to its movement stat, or attack, according to its range and damage stats. Depending on its abilities, it may be allowed further moves.
