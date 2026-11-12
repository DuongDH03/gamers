# Images and sprites: PNG via stb_image, sprite sheets

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12: "PNG via stb_image", "Sprite sheets")

## Problem

The constitution left image and animation formats open. Plan item 2 (sprites)
needed them; until then everything was drawn as flat-coloured boxes.

## Options

1. **PNG decoded by stb_image**, one public-domain header vendored in the repo.
2. BMP with our own ~100-line loader. No library, but large files and poor tool support.
3. PNG with our own decoder (PNG + zlib/deflate). A big learning project on its own.

For animation: sprite sheets (frames side by side in one image) or static sprites only.

## Decision

**PNG via stb_image, and sprite sheets.**

- PNG is what every art tool exports. stb_image is a library at the edge, as
  the constitution allows: pinned to a commit (`engine/third_party/README.md`),
  compiled once, PNG only, in its own target without our warning flags.
- We read files ourselves and give stb the bytes (`core/image.cpp`); pixels
  reach the GPU through our own `Renderer2D::create_texture`, so no backend
  knows about PNG and no core code knows about textures.
- A **sprite sheet** is one image with frames in a row. An image declares
  `frames`, an optional frame `size`, and `fps`. The frame shown is
  `(world tick x fps / 60) mod frames` while the entity moves (always, for
  things that never move, like coins), so every machine shows the same frame.
- **Tile textures** use the same images: frame 0 is a block's top face, frame
  1 its sides, and an optional frame 2 its sides under another block.
- Only clients load pixels. The level stores image names and paths (plain
  data, serialized), so the server never touches image files.

Audio formats and the level-authoring tool remain open.
