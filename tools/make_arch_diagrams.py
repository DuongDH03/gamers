"""Writes the diagrams of docs/architecture.md into docs/images/ (arch-*.svg).

Usage, from the repository root: python tools/make_arch_diagrams.py
Shared drawing helpers: tools/diagram_svg.py.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diagram_svg import Svg, esc, legend  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'images')


# ---------------------------------------------------------------- 1. layers
s = Svg(500, 'Scraps layers', 'Apps on top, then games, then views and net side by side, then core, then platform interfaces; backends sdl3, headless, udp and webview2 implement the interfaces; dynlib loads game modules for the apps. Includes only point down.')
rows = [(30, 'teal', 'apps', 'scraps_client · scraps_web · scraps_server · scraps_bench: the only place a backend is named'),
        (100, 'teal', 'games', 'sandbox: coin rules; exports a GameApi (static, or a DLL with --hot-game)')]
for y, ramp, title, sub in rows:
    s.box(40, y, 600, 56, ramp, title, sub)
s.box(40, 170, 290, 56, 'purple', 'engine/views', 'TopDown · Side · Isometric · Controls')
s.box(350, 170, 290, 56, 'purple', 'engine/net', 'protocol · Server · Client · Loopback')
s.box(40, 240, 600, 56, 'purple', 'engine/core', 'World · Entity · Command · simulate · movement · Level · scenes · serialize · jobs · sound · mixer')
s.box(40, 310, 600, 56, 'gray', 'engine/platform (interfaces only)', 'Window · Input · Renderer2D · Audio · Net · UiHost')
for i, (title, sub) in enumerate([('sdl3', 'window, GPU, pad'), ('webview2', 'React UI, WebGL'), ('headless', 'server, tests'), ('udp', 'real sockets'), ('dynlib', 'game DLLs')]):
    s.box(40 + i * 122, 400, 112, 56, 'amber', title, sub)
    if i < 4:
        s.arrow([(96 + i * 122, 398), (96 + i * 122, 370)], 'amber', dashed=True)
s.arrow([(340, 87), (340, 98)])
s.arrow([(185, 157), (185, 168)])
s.arrow([(495, 157), (495, 168)])
s.arrow([(185, 227), (185, 238)])
s.arrow([(495, 227), (495, 238)])
s.arrow([(340, 297), (340, 308)])
legend(s, 480, [('teal', 'your programs'), ('purple', 'engine'), ('gray', 'interfaces'), ('amber', 'backends: libraries at the edge')])
s.save(OUT, 'arch-layers.svg')

# ---------------------------------------------------------------- 2. world
s = Svg(370, 'The World is plain data', 'A World holds state that changes every tick and is sent in snapshots, and static rules and level. Entities live in slots with generations; a handle resolves only while its generation matches.')
s.frame(40, 30, 290, 310)
s.text(56, 50, 'World', 'th')
s.add('<g class="purple"><rect class="f" x="56" y="66" width="258" height="160" rx="8"/>'
      '<text class="th tt" x="70" y="88" dominant-baseline="central">State: changes every tick</text>'
      '<text class="mono st" x="70" y="114" dominant-baseline="central">tick, rng_state</text>'
      '<text class="mono st" x="70" y="136" dominant-baseline="central">entities[]   slots</text>'
      '<text class="mono st" x="70" y="158" dominant-baseline="central">free_slots[]</text>'
      '<text class="mono st" x="70" y="180" dominant-baseline="central">player_entities[16]</text>'
      '<text class="ts st" x="70" y="206" dominant-baseline="central">copied, sent in every snapshot</text></g>')
s.add('<g class="gray"><rect class="f" x="56" y="240" width="258" height="84" rx="8"/>'
      '<text class="th tt" x="70" y="262" dominant-baseline="central">Static: from the scene</text>'
      '<text class="mono st" x="70" y="286" dominant-baseline="central">rules   gravity, jump, axes</text>'
      '<text class="mono st" x="70" y="306" dominant-baseline="central">level   grid, tiles, archetypes</text></g>')
s.text(370, 50, 'entities[]: one slot per entity', 'th')
slots = [(True, 1), (True, 1), (True, 3), (False, 2), (True, 1), (True, 1)]
for i, (alive, gen) in enumerate(slots):
    x = 372 + i * 44
    ramp = 'purple' if alive else 'gray'
    dash = '' if alive else ' style="stroke-dasharray:3 3"'
    s.add(f'<g class="{ramp}"><rect class="f" x="{x}" y="72" width="40" height="52" rx="6"{dash}/>'
          f'<text class="ts tt" x="{x + 20}" y="88" text-anchor="middle" dominant-baseline="central">{i}</text>'
          f'<text class="ts st" x="{x + 20}" y="110" text-anchor="middle" dominant-baseline="central">g{gen}</text></g>')
s.box(372, 170, 126, 52, 'teal', '{2, gen 3}', 'resolves')
s.box(510, 170, 126, 52, 'coral', '{3, gen 1}', 'stale: fails')
s.arrow([(435, 168), (476, 128)], 'teal')
s.arrow([(573, 168), (530, 128)], 'coral')
s.box(372, 250, 264, 74, 'gray', 'World a = b;', 'a full, independent copy:')
s.text(504, 312, 'no pointers, no callbacks', 'ts', 'middle')
legend(s, 352, [('purple', 'alive slot'), ('gray', 'dead slot (dashed)'), ('teal', 'live handle'), ('coral', 'stale handle')], x=40)
s.save(OUT, 'arch-world.svg')

# ---------------------------------------------------------------- 3. simulate
s = Svg(380, 'Only simulate changes the world', 'Keyboard, network and tests produce a CommandList; simulate applies commands, moves entities and runs game systems, giving the world one tick later. Nothing else writes to the World.')
for i, (title, sub) in enumerate([('Keyboard', 'via Controls'), ('Network', 'via Server'), ('Tests', 'scripted')]):
    s.box(40, 40 + i * 70, 150, 52, 'teal', title, sub)
    y = 66 + i * 70
    s.line([(190, y), (210, y), (210, 136)])
s.arrow([(210, 136), (228, 136)])
s.box(230, 110, 130, 52, 'teal', 'CommandList', 'what to change')
s.arrow([(360, 136), (398, 136)])
s.frame(400, 30, 240, 230)
s.text(416, 48, 'simulate(world, commands, tick)', 'mono')
for i, (title, sub) in enumerate([('apply_commands', 'join, leave, move, jump'), ('step_movement', 'intent, gravity, collision'), ('game systems', 'sandbox: coins')]):
    s.box(420, 64 + i * 64, 200, 50, 'purple', title, sub)
    if i < 2:
        s.arrow([(520, 114 + i * 64), (520, 126 + i * 64)])
s.arrow([(520, 262), (520, 288)])
s.box(400, 290, 240, 56, 'purple', 'World, one tick later')
s.box(40, 290, 300, 56, 'coral', 'UI, scripts, anything else', 'never writes the World: sends a Command')
s.arrow([(342, 318), (398, 318)], 'coral', dashed=True)
s.add('<g class="coral"><text class="th tt" x="370" y="304" text-anchor="middle" dominant-baseline="central">✗</text></g>')
s.save(OUT, 'arch-simulate.svg')

# ---------------------------------------------------------------- 4. fixed step
s = Svg(300, 'Fixed steps, free rendering', 'Frames arrive at irregular times; simulation ticks happen exactly every 1/60 s. A frame between two ticks draws entities blended by alpha.')
s.text(40, 70, 'Frames', 'th')
s.text(40, 150, 'Ticks', 'th')
s.text(40, 168, 'every 1/60 s', 'ts')
s.line([(130, 70), (640, 70)])
s.line([(130, 150), (640, 150)])
for x in [140, 175, 255, 290, 390, 455, 470, 560, 612]:
    s.add(f'<g class="amber"><rect class="f" x="{x - 3}" y="58" width="6" height="24" rx="2"/></g>')
for i in range(9):
    x = 150 + i * 60
    hl = x in (360, 420)
    s.add(f'<g class="teal"><circle class="{"ff" if hl else "f"}" cx="{x}" cy="150" r="{7 if hl else 5}"/></g>')
s.line([(390, 84), (390, 140)], 'amber', dashed=True)
s.text(360, 178, 'previous', 'ts', 'middle')
s.text(420, 178, 'current', 'ts', 'middle')
s.line([(360, 196), (360, 204), (420, 204), (420, 196)], 'gray')
s.text(390, 218, 'this frame: alpha = 0.5', 'ts', 'middle')
s.text(40, 256, 'Entity drawn at', 'th')
s.add('<g class="teal"><circle class="f" cx="250" cy="256" r="10" style="stroke-dasharray:3 3;fill:none"/>'
      '<circle class="f" cx="450" cy="256" r="10" style="stroke-dasharray:3 3;fill:none"/></g>')
s.add('<g class="coral"><circle class="f" cx="350" cy="256" r="10"/></g>')
s.text(250, 280, 'previous', 'ts', 'middle')
s.text(350, 280, 'drawn: lerp by alpha', 'ts', 'middle')
s.text(450, 280, 'current', 'ts', 'middle')
s.save(OUT, 'arch-fixed-step.svg')

# ---------------------------------------------------------------- 5. views
s = Svg(310, 'One grid, three views', 'The same tiles, a wall block and a player seen top-down, from the side and isometrically.')
for i, (title, l1, l2) in enumerate([('Top-down', 'looks down −z', 'sort: height, then y'),
                                      ('Side', 'looks along +y', 'sort: far lanes first'),
                                      ('Isometric', 'looks along (1,1,1)', 'sort: x + y + z')]):
    x = 40 + i * 205
    s.frame(x, 30, 190, 260)
    s.text(x + 95, 228, title, 'th', 'middle')
    s.text(x + 95, 250, l1, 'ts', 'middle')
    s.text(x + 95, 270, l2, 'ts', 'middle')
# top-down
for r in range(3):
    for c in range(3):
        ramp = 'gray' if (r == 0 and c == 2) else 'teal'
        s.add(f'<g class="{ramp}"><rect class="{"ff" if ramp == "gray" else "f"}" x="{75 + c * 40}" y="{65 + r * 40}" width="40" height="40"/></g>')
s.add('<g class="coral"><circle class="ff" cx="135" cy="125" r="11"/></g>')
# side
for c in range(3):
    s.add(f'<g class="teal"><rect class="f" x="{280 + c * 40}" y="165" width="40" height="40"/></g>')
s.add('<g class="gray"><rect class="ff" x="360" y="125" width="40" height="40"/></g>')
s.add('<g class="coral"><rect class="ff" x="306" y="129" width="28" height="36" rx="3"/></g>')
# isometric
cx, y0 = 545, 80


def diamond(X, Y):
    return f'{X},{Y} {X + 30},{Y + 15} {X},{Y + 30} {X - 30},{Y + 15}'


for i in range(3):
    for j in range(3):
        X = cx + (i - j) * 30
        Y = y0 + (i + j) * 15
        s.add(f'<g class="teal"><polygon class="f" points="{diamond(X, Y)}"/></g>')
X, Y = cx + 60, y0 + 30
s.add(f'<g class="gray"><polygon class="ff" points="{X - 30},{Y - 15} {X},{Y} {X},{Y + 30} {X - 30},{Y + 15}"/>'
      f'<polygon class="f" points="{X},{Y} {X + 30},{Y - 15} {X + 30},{Y + 15} {X},{Y + 30}"/>'
      f'<polygon class="f" points="{X},{Y - 30} {X + 30},{Y - 15} {X},{Y} {X - 30},{Y - 15}" style="fill-opacity:0.6"/></g>')
s.add(f'<g class="coral"><rect class="ff" x="{cx - 12}" y="{y0 + 12}" width="24" height="34" rx="3"/></g>')
s.save(OUT, 'arch-views.svg')

# ---------------------------------------------------------------- 5b. textures
s = Svg(140, 'From image file to the screen', 'A PNG file is decoded to pixels by core, uploaded as a texture by the backend, mapped by the SpriteBank, and used by View::draw for each tile face and sprite.')
items = [('gray', 'PNG file', 'art/*.png'), ('purple', 'Pixels', 'load_png'), ('amber', 'Texture', 'uploaded'),
         ('purple', 'SpriteBank', 'views layer'), ('purple', 'View::draw', 'per face')]
for i, (ramp, t, sub) in enumerate(items):
    x = 40 + i * 124
    s.box(x, 30, 100, 56, ramp, t, sub)
    if i < 4:
        s.arrow([(x + 102, 58), (x + 122, 58)])
legend(s, 116, [('purple', 'engine (core, views)'), ('amber', 'backend (sdl3 or headless)'), ('gray', 'file on disk')])
s.save(OUT, 'arch-textures.svg')

# ---------------------------------------------------------------- 6. prediction
s = Svg(390, 'Prediction and reconciliation', 'The client predicts its own move at once and sends inputs every tick, repeating unacknowledged ones; the server applies them and sends a snapshot with an ack; the client resets to the snapshot and replays the inputs the server has not seen.')
s.box(60, 24, 170, 40, 'teal', 'Client (player 1)')
s.box(450, 24, 170, 40, 'purple', 'Server (authority)')
s.line([(240, 70), (240, 366)], dashed=True)
s.line([(440, 70), (440, 366)], dashed=True)
s.box(40, 86, 190, 48, 'teal', 'Tick 101: W pressed', 'moves on screen at once')
s.arrow([(242, 100), (438, 140)], 'teal')
s.text(340, 104, 'Input [101]', 'ts', 'middle')
s.box(40, 156, 190, 48, 'teal', 'Tick 102', 'resends 101 with 102')
s.arrow([(242, 170), (438, 210)], 'teal')
s.text(340, 174, 'Input [101, 102]', 'ts', 'middle')
s.box(450, 130, 190, 48, 'purple', 'Applies input 101', 'simulate, then snapshot')
s.arrow([(438, 236), (242, 290)], 'purple')
s.text(432, 222, 'Snapshot (ack 101)', 'ts', 'end')
s.box(40, 282, 190, 70, 'teal', 'Reconcile', 'world = snapshot,')
s.add('<g class="teal"><text class="ts st" x="135" y="338" text-anchor="middle" dominant-baseline="central">replay 102 onward</text></g>')
s.save(OUT, 'arch-prediction.svg')

# ---------------------------------------------------------------- 6. delta
s = Svg(450, 'Delta snapshots', 'The server sends changes since the snapshot the client last confirmed. Snapshot 41 is lost; 42 is still a delta against 40, which the client has, so nothing is resent.')
s.box(60, 24, 170, 40, 'purple', 'Server')
s.box(450, 24, 170, 40, 'teal', 'Client')
s.line([(240, 70), (240, 430)], dashed=True)
s.line([(440, 70), (440, 430)], dashed=True)
s.arrow([(242, 100), (438, 130)], 'purple'); s.text(340, 101, '40: full state', 'ts', 'middle')
s.box(450, 116, 190, 30, 'teal', 'has 40')
s.arrow([(438, 150), (242, 180)], 'teal'); s.text(340, 151, 'Input: "I have 40"', 'ts', 'middle')
s.arrow([(242, 215), (370, 234)], 'coral', dashed=True); s.text(320, 208, '41 = changes since 40', 'ts', 'middle')
s.add('<g class="coral"><text class="th tt" x="380" y="236" text-anchor="start" dominant-baseline="central">✗ lost</text></g>')
s.box(40, 250, 190, 48, 'purple', 'Keeps 32 states', 'the possible baselines')
s.arrow([(242, 265), (438, 295)], 'purple'); s.text(340, 266, '42 = changes since 40', 'ts', 'middle')
s.box(450, 281, 190, 30, 'teal', '40 + changes = 42')
s.arrow([(438, 320), (242, 350)], 'teal'); s.text(340, 321, 'Input: "I have 42"', 'ts', 'middle')
s.arrow([(242, 375), (438, 405)], 'purple'); s.text(340, 376, '43 = changes since 42', 'ts', 'middle')
s.box(450, 391, 190, 30, 'teal', '42 + changes = 43')
s.save(OUT, 'arch-delta.svg')

# ---------------------------------------------------------------- 6. delta bytes
s = Svg(170, 'Inside a delta', 'A delta is a small header, then one entry per changed entity (skip, group mask, changed groups), then two flags for the free list and player table.')
segs = [('gray', 'base', 48), ('gray', '+tick', 40), ('gray', 'rng', 86), ('gray', 'slots', 40), ('gray', 'count', 46),
        ('purple', 'skip', 40), ('purple', 'mask', 44), ('purple', 'position', 120), ('purple', '…', 40),
        ('gray', 'free', 36), ('gray', 'players', 56)]
x = 40
spans = {}
for ramp, label, w in segs:
    s.add(f'<g class="{ramp}"><rect class="f" x="{x}" y="40" width="{w}" height="40"/>'
          f'<text class="ts tt" x="{x + w / 2}" y="60" text-anchor="middle" dominant-baseline="central">{esc(label)}</text></g>')
    spans[label] = (x, x + w)
    x += w
x0, x1 = spans['base'][0], spans['count'][1]
s.line([(x0, 92), (x0, 98), (x1, 98), (x1, 92)])
s.text((x0 + x1) / 2, 114, 'header ≈ 16 bytes', 'ts', 'middle')
x0, x1 = spans['skip'][0], spans['position'][1]
s.line([(x0, 92), (x0, 98), (x1, 98), (x1, 92)])
s.text((x0 + x1) / 2, 114, 'one moving entity ≈ 14 bytes', 'ts', 'middle')
x0, x1 = spans['free'][0], spans['players'][1]
s.line([(x0, 92), (x0, 98), (x1, 98), (x1, 92)])
s.text((x0 + x1) / 2, 114, '2 bytes', 'ts', 'middle')
s.text(40, 146, 'mask = which groups follow: slot · pose · motion · control · look · game. Unchanged entities cost nothing.', 'ts')
s.save(OUT, 'arch-delta-bytes.svg')

# ---------------------------------------------------------------- 6. interpolation
s = Svg(210, 'Other players are interpolated', 'The client keeps recent snapshots and draws other players 4 ticks behind the newest one, always between two known positions.')
s.line([(90, 110), (620, 110)])
for i, t in enumerate(range(94, 101)):
    x = 100 + i * 80
    s.add(f'<g class="purple"><circle class="{"ff" if t == 100 else "f"}" cx="{x}" cy="110" r="7"/></g>')
    s.text(x, 134, f'tick {t}', 'ts', 'middle')
s.text(580, 84, 'newest snapshot', 'ts', 'middle')
s.add('<g class="coral"><polygon class="ff" points="292,82 308,82 300,98"/></g>')
s.text(300, 64, 'other player drawn here', 'ts', 'middle')
s.line([(300, 154), (300, 162), (580, 162), (580, 154)])
s.text(440, 180, '4 ticks ≈ 67 ms behind: always between two real snapshots', 'ts', 'middle')
s.save(OUT, 'arch-interpolation.svg')

# ---------------------------------------------------------------- 6b. jobs
s = Svg(250, 'The job system', 'The main thread and three workers take 128-entity chunks from an atomic counter; when the last chunk is done, the main thread alone removes fallen entities and respawns players.')
s.text(150, 30, 'parallel_for(entities, 128): chunks from an atomic counter', 'mono')
lanes = ['Main thread', 'Worker 1', 'Worker 2', 'Worker 3']
chunks = {0: [0, 4], 1: [1, 5], 2: [2, 6, 7], 3: [3]}
for li, name in enumerate(lanes):
    y = 50 + li * 40
    s.text(40, y + 15, name, 'th' if li == 0 else 't')
    for k, c in enumerate(chunks[li]):
        ramp = 'purple' if li == 0 else 'teal'
        s.box(150 + k * 78, y, 72, 30, ramp, f'chunk {c}', rx=5)
s.line([(395, 44), (395, 210)], dashed=True)
s.text(395, 226, 'waits for the last chunk', 'ts', 'middle')
s.box(410, 50, 230, 30, 'coral', 'remove fallen, respawn', rx=5)
s.text(525, 98, 'one thread: touches shared lists', 'ts', 'middle')
s.save(OUT, 'arch-jobs.svg')

# ---------------------------------------------------------------- 7. performance
s = Svg(350, 'Where frame time goes', 'Crowd scene, 1,001 entities: all steps together take about 1 ms of the 16.7 ms frame budget.')
s.text(40, 30, 'The 16.7 ms frame budget at 60 Hz', 'th')
s.add('<g class="gray"><rect class="f" x="40" y="44" width="600" height="22" rx="4"/></g>')
s.add('<g class="purple"><rect class="ff" x="40" y="44" width="36" height="22" rx="4"/></g>')
s.text(84, 55, 'everything below ≈ 1 ms (6%)', 'ts')
s.text(40, 100, 'Zoomed in, crowd scene (1,001 entities)', 'th')
rows = [('simulate, 1 thread', 0.15), ('simulate, 8 threads', 0.08), ('copy World', 0.002),
        ('serialize full (91 KB)', 0.35), ('delta encode (~14 KB)', 0.06), ('delta decode', 0.017),
        ('draw iso + shadows', 0.39)]
for i, (label, v) in enumerate(rows):
    y = 122 + i * 28
    s.text(200, y + 8, label, 'ts', 'end')
    w = max(2, v / 0.6 * 380)
    s.add(f'<g class="purple"><rect class="ff" x="210" y="{y}" width="{w:.1f}" height="16" rx="3"/></g>')
    s.text(210 + w + 8, y + 8, f'{v:g} ms', 'ts')
s.line([(210, 116), (210, 316)])
s.text(210, 330, '0', 'ts', 'middle')
s.text(590, 330, '0.6 ms', 'ts', 'middle')
s.save(OUT, 'arch-performance.svg')

# ---------------------------------------------------------------- 7. snapshot sizes
s = Svg(300, 'Snapshot size, full and delta', 'Bytes per snapshot per client on a log scale: isometric idle 694 to 35, side walking 694 to 18, top-down 1,431 to 149, crowd 91,239 to 14,236.')


def lx(v):
    return 200 + 105 * (math.log10(v) - 1)


for v, lab in [(10, '10 B'), (100, '100 B'), (1000, '1 KB'), (10000, '10 KB'), (100000, '100 KB')]:
    s.line([(lx(v), 44), (lx(v), 250)], dashed=True, width=0.6)
    s.text(lx(v), 32, lab, 'ts', 'middle')
groups = [('Isometric, idle', 694, 35), ('Side, walking', 694, 18), ('Top-down', 1431, 149), ('Crowd (1,000)', 91239, 14236)]


def fmt(v):
    return f'{v / 1000:.1f} KB' if v >= 1000 else f'{v} B'


for i, (lab, full, delta) in enumerate(groups):
    y = 58 + i * 48
    s.text(190, y + 14, lab, 'ts', 'end')
    s.add(f'<g class="gray"><rect class="f" x="200" y="{y}" width="{lx(full) - 200:.1f}" height="13" rx="3"/></g>')
    s.text(lx(full) + 6, y + 6, fmt(full), 'ts')
    s.add(f'<g class="blue"><rect class="ff" x="200" y="{y + 16}" width="{lx(delta) - 200:.1f}" height="13" rx="3"/></g>')
    s.text(lx(delta) + 6, y + 22, fmt(delta), 'ts')
s.add('<g class="gray"><rect class="f" x="200" y="270" width="12" height="12" rx="2"/></g>')
s.text(218, 276, 'full state (before)')
s.add('<g class="blue"><rect class="ff" x="360" y="270" width="12" height="12" rx="2"/></g>')
s.text(378, 276, 'delta (now)')
s.save(OUT, 'arch-snapshot-sizes.svg')

# ---------------------------------------------------------------- 6f. the UI layer in scraps_web
s = Svg(420, 'scraps_web: the UI layer', 'Native side: ClientApp runs the game; the webview2 backend turns Renderer2D calls into one frame of bytes in a shared buffer, sends small JSON view messages through UiHost, and receives UI actions and page input. Page side: renderer.js draws the frame with WebGL2 on the world canvas, React draws menus and HUD on top and sends actions; Esc belongs to the page.')
s.frame(40, 30, 290, 300)
s.text(185, 50, 'Native (scraps_web.exe)', 'th', 'middle')
s.box(58, 66, 254, 48, 'purple', 'ClientApp::frame', 'simulate, net, audio, Views')
s.box(58, 136, 254, 48, 'amber', 'Renderer2D (webview2)', 'batches + textures as bytes')
s.box(58, 206, 254, 48, 'amber', 'UiHost and Input', 'view out; actions, input in')
s.box(58, 266, 254, 48, 'gray', 'UiModel', 'action: Command or app call')
s.arrow([(185, 115), (185, 134)])
s.frame(350, 30, 290, 300)
s.text(495, 50, 'Page (WebView2)', 'th', 'middle')
s.box(368, 66, 254, 48, 'coral', 'React UI (ui/)', 'menus, HUD, Esc')
s.box(368, 136, 254, 48, 'coral', 'renderer.js: WebGL2', 'draws on canvas#world')
s.box(368, 206, 254, 48, 'coral', 'Page input', 'keys, mouse while playing')
s.arrow([(313, 160), (366, 160)], 'amber')
s.arrow([(366, 90), (340, 90), (340, 230), (314, 230)], 'coral', both=True)
s.arrow([(366, 240), (346, 240), (346, 250), (314, 250)], 'coral')
s.text(40, 352, 'Frame bytes go through a shared buffer (a 20-byte note per frame, no JSON per vertex).', 'ts')
s.text(40, 372, 'Views are sent only on change; actions and input have fixed limits.', 'ts')
legend(s, 400, [('purple', 'engine'), ('amber', 'webview2 backend'), ('coral', 'page'), ('gray', 'app')])
s.save(OUT, 'arch-webview2.svg')

print('ok')
