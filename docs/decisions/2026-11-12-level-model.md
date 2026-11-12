# Level model and tile format: one 3D block grid, text scene files

- Date: 2026-11-12
- Status: **Accepted** (owner chose "3D tile grid, all views", 2026-11-12).
  The text format details below were chosen during implementation; the owner
  may still change them.

## Problem

Rule 4 asks for one engine for top-down, side-view and isometric games, with
isometric designed first. Two open decisions follow: the tile map format, and
whether isometric tiles are diamond or staggered.

## Options

1. **A 3D grid of unit blocks** (x width, y depth, z height). Each view is a
   different camera on the same data.
2. **A 2D tile layer now**, height and isometric later. Faster first pixels,
   but views would diverge into separate code paths (the risk Rule 4 names).

## Decision

**One 3D grid**, `TileGrid { width, depth, height, cells[] }`, with tile types
that are `solid` (block movement) or `decor` (drawn only).

- Top-down looks down −z, side looks along +y, isometric looks along (1,1,1).
  Collision is the same 3D box-against-cells test for all three.
- Isometric is **2:1 diamond** (not staggered): `screen = ((x−y)/2, (x+y)/4 − z/2)`
  in tile units. Diamond keeps the projection one linear map, so input mapping
  is its exact inverse and depth is simply `x+y+z`.
- A scene can be shown through any view (the `V` key) because they share data;
  the view a scene declares decides its movement rules.

**Scene file format:** line-based text (`.scene`), documented in
[docs/scene-format.md](../scene-format.md). Text because it is readable,
diffable and needs no tools yet. Layers are drawn as character rows, one
character per cell, so a level is visible in the file itself.
