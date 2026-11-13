# Scene file format (`.scene`, version 1)

A scene is a plain text file: a small 3D grid of blocks plus what lives in it.
Decided in [2026-11-12-level-model](decisions/2026-11-12-level-model.md).
Parser: [engine/core/scene.cpp](../engine/core/scene.cpp). Errors name the line.

**Where the apps look** (`apps/common/scene_dir.h`): `--scene-dir <dir>` when
given; otherwise `scenes/` next to the executable (an installed or unzipped
release); otherwise the game's scenes folder in the source tree, compiled in
for development (`scraps_add_game(... SCENES <dir>)`). Both apps print the
folder they chose at start-up. A release ships the game's whole scenes folder
(art, sounds, music) as `scenes/`.

## Coordinates

```
            z (height)
            │
            │   y (depth, rows go "down" the page)
            │  ╱
            │ ╱
            └──────── x (width, characters along a row)
```

Cell `(x, y, z)` is the unit cube `[x, x+1] × [y, y+1] × [z, z+1]`. An entity
position is the **bottom center** of its box, so something standing on a block
in layer 0 has `z = 1`.

Outside the grid in x or y is solid (invisible walls). Below and above the grid
is open: falling below `kill_z` (−8) respawns players and removes other entities.

## Lines

The first line must be `scraps-scene 1`. Lines starting with `#` are comments,
except inside a layer's rows, where every line is a row. No end-of-line comments.

| Directive | Meaning |
|---|---|
| `view top-down \| side \| isometric` | The view this scene is made for. Sets the movement rules (gravity, jumping, locked axes). Required. |
| `size <width> <depth> <height>` | Grid size, each 1 to 512. Required, once, before any layer. |
| `image <name> <path> [frames N] [size W H] [fps F]` | A PNG, or a sprite sheet with `frames` pictures side by side, each `W` x `H` pixels (default: width / frames by full height). `fps` is the animation speed. `path` is relative to the scene file. |
| `tile <symbol> <name> <solid\|decor> <r> <g> <b> [texture <image>]` | A block type. `symbol` is one printable character, not `.`. `decor` blocks are drawn but can be walked through. The colour is used when there is no texture or it fails to load. |
| `archetype <name> [kind N] [size X Y Z] [color R G B] [speed S] [flags a,b] [sprite <image>]` | A recipe for entities. `kind` is a number the game gives meaning to. Flags: `moves`, `gravity`, `collides`, `bounces`. Without a sprite, the entity is drawn as a coloured box. |
| `player <archetype>` | What a joining player becomes. Required. |
| `spawn <x> <y> <z>` | A player spawn point; players use them in turn. At least one. |
| `entity <archetype> <x> <y> <z>` | One entity at a position. |
| `crowd <archetype> <count>` | `count` entities scattered at random on the players' floor (the height of the first spawn point), moving in random directions if the archetype moves. |
| `fill <z> <symbol>` | Fills a whole layer with one tile (`.` empties it). |
| `layer <z>` | The next `depth` lines are rows y = 0, 1, … of layer z, each exactly `width` characters. `.` is empty. |
| `sky <r> <g> <b> [<r> <g> <b>]` | The background: one colour, or a blend from the first colour at the top of the screen to the second at the bottom. Default: dark blue-grey (24, 26, 34). |
| `sound <name> <path> [volume V]` | A sound effect, in any common audio format (see below). The game plays it by name. `volume` 0 to 4, default 1. |
| `music <path> [volume V]` | Background music: plays, looping, when the scene loads (see [Music](#music)). At most one per scene. `volume` 0 to 4, default 1. |

Directives that refer to something (`player`, `entity`, `crowd`, layer
symbols) must come after it is declared.

Limits: at most 4,096 entities from `entity` lines and `crowd`s together (so a
full network snapshot always fits), 63 tile types, 64 archetypes, 64 images,
64 spawn points, and positions within ±100,000 units of the origin.

## Images, textures and sprites

Decided in [2026-11-12-images-and-sprites](decisions/2026-11-12-images-and-sprites.md).

- **Paths** are relative to the scene file, up to 63 characters of letters,
  digits, `-`, `_`, `.` and `/`, with no `..`. A scene can never point outside
  its folder.
- **Tile textures:** frame 0 is the top face of a block, frame 1 its side
  faces, and an optional frame 2 the side faces of a block with another block
  on top (e.g. grass on top, grass-edged dirt on the sides, plain dirt deeper
  down). A one-frame image is used for every face. Faces are darkened by the
  view's shading; the texture's own colours are kept.
- **Sprites** stand upright on the entity's feet, facing the camera, about
  1.3 times as wide as the entity's box (the box is for physics). The frame
  shown is `(tick x fps / 60) mod frames` while the entity moves; things
  without the `moves` flag (coins) animate all the time. A sprite is drawn
  mirrored when its entity faces left on screen, and keeps facing the way it
  last moved after it stops.
- Every entity casts a soft **shadow** on the ground below it (smaller and
  fainter the higher it is, gone at 4 units).
- **Only clients load images**; the server never reads them. A missing or
  broken image is reported, and whatever uses it falls back to its colour.
- The sandbox's art is generated by `tools/make_sandbox_art.py` into
  `games/sandbox/scenes/art/`.

## Sounds

Decided in [2026-11-12-audio-formats](decisions/2026-11-12-audio-formats.md).

- **Any common format, any name:** WAV (PCM 8/16/24/32-bit, float, ADPCM,
  A-law, µ-law; W64, RF64), AIFF, FLAC, MP3 and OGG Vorbis. The file is
  recognised by its first bytes; the extension does not matter. Any sample
  rate, 1 to 8 channels.
- Opus, AAC/M4A and WMA are recognised and refused with an error naming the
  format. At most 32 MB and 30 seconds per sound.
- **Only clients load sounds**; a sound that fails is reported and stays
  silent. The sandbox plays `jump`, `coin` and `drop`; its sounds come from
  `tools/make_sandbox_sounds.py` into `games/sandbox/scenes/sounds/` (WAV,
  OGG and MP3 on purpose).

## Music

Spec: [2026-11-14-offline-essentials-and-ui](specs/2026-11-14-offline-essentials-and-ui.md), part 2.

- **Streamed, not loaded whole:** the file is kept in memory still encoded and
  decoded a chunk at a time while it plays ([core/music.h](../engine/core/music.h)),
  so the 30-second limit for sound effects does not apply. At most 64 MB per
  file (a long WAV is better stored as OGG).
- **Formats:** OGG Vorbis, MP3, FLAC and WAV (also AIFF), recognised by
  content like sounds; any sample rate, 1 to 8 channels. Same path rules as
  images and sounds.
- **Looping:** the track loops from its last sample straight to its first,
  with nothing added or dropped. Whether that sounds seamless depends on the
  file: WAV, FLAC and OGG Vorbis are sample-exact; MP3 encoders pad the ends a
  little. A track shorter than 1,024 samples is refused when it first wraps.
- **Fades:** the first track fades in over 1 s. A scene with a different
  `music` file crossfades over 1.5 s (equal power: the old track follows a
  cosine down while the new one follows a sine up, so the loudness stays
  steady). A scene without a `music` line fades the music out over 1 s.
- **Volumes:** `volume` is the track's own level. The mixer also has a music
  volume and an effects volume, separate from each other (for a settings menu,
  and for ducking the music during a pause).
- **Only clients play music**; the server ignores the line. A missing or broken
  file is reported, and the game stays silent instead of stopping. The sandbox
  plays `sounds/music.ogg`, a 30-second chiptune loop made by
  `tools/make_sandbox_sounds.py`.

## Hot reload

`scraps_server` and the offline client watch the scene file. Saving it while
the game runs rebuilds the world from the new file: every connected player
re-joins at a spawn point, and every client reloads the scene and its
images and sounds. Music that the reloaded scene still names keeps playing
without a restart (only its `volume` follows the file); another file
crossfades in. A file with an error is not applied; the error is printed (and shown
on the offline client's HUD) and the old world keeps running. Online, every
client needs the edited file too, or it stops with "differs from the
server's copy".

## Example

```
scraps-scene 1
view isometric
size 4 3 2
image grass art/grass.png frames 3
image hero  art/hero.png  frames 4 fps 8
tile g grass solid 96 164 80 texture grass
tile # wall  solid 150 140 160
archetype hero kind 1 size 0.6 0.6 0.9 color 235 90 70 speed 4.5 flags moves,gravity,collides sprite hero
player hero
spawn 1.5 1.5 1
fill 0 g
layer 1
####
#...
#...
```

## Networking

Client and server each load the scene from their own disk. The server sends the
scene's name (letters, digits, `-`, `_`; no paths) and a hash of its text; the
client refuses to play if its copy differs. Line endings (CRLF or LF) do not
change the hash.
