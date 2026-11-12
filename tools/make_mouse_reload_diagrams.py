"""Writes the diagrams of docs/specs/2026-11-12-mouse-and-hot-reload.md into
docs/images/ (spec-mr-*.svg).

Usage, from the repository root: python tools/make_mouse_reload_diagrams.py
Shared drawing helpers: tools/diagram_svg.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diagram_svg import Svg, legend  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'images')

# ---------------------------------------------------------------- picking
s = Svg(336, 'From the cursor back to the world', 'Each View turns the cursor point into a world point on its own pointer plane through the player: top-down and isometric use the plane at the player\'s height, side view uses the player\'s lane. The formulas are the exact inverse of world_to_screen.')
panels = [
    ('Top-down', ['plane z = player.z', 'x = cx + dx / zoom', 'y = cy + dy / zoom']),
    ('Side', ['plane y = player lane', 'x = cx + dx / zoom', 'z = cz - dy / zoom']),
    ('Isometric', ['plane z = player.z', 'a = 2 dx / zoom', 'b = 4 dy / zoom', 'b += 2 (z - cz)', 'x = cx + (a + b) / 2', 'y = cy + (b - a) / 2']),
]
for i, (title, lines) in enumerate(panels):
    x0 = 40 + i * 205
    s.frame(x0, 30, 190, 245)
    s.text(x0 + 95, 48, title, 'th', 'middle')
    if i == 0:  # a patch of ground seen from above
        s.add(f'<g class="teal"><rect class="f" x="{x0 + 55}" y="64" width="80" height="56" rx="2"/></g>')
        s.line([(x0 + 95, 64), (x0 + 95, 120)])
        s.line([(x0 + 55, 92), (x0 + 135, 92)])
        s.add(f'<circle class="x-purple" cx="{x0 + 112}" cy="78" r="4"/>')
    elif i == 1:  # ground seen from the side, one block on it
        s.add(f'<g class="teal"><rect class="f" x="{x0 + 35}" y="106" width="120" height="14" rx="2"/></g>')
        s.add(f'<g class="gray"><rect class="f" x="{x0 + 55}" y="88" width="18" height="18" rx="2"/></g>')
        s.add(f'<circle class="x-purple" cx="{x0 + 120}" cy="84" r="4"/>')
    else:  # an isometric diamond
        s.add(f'<g class="teal"><polygon class="f" points="{x0 + 95},64 {x0 + 155},92 {x0 + 95},120 {x0 + 35},92"/></g>')
        s.add(f'<circle class="x-purple" cx="{x0 + 110}" cy="94" r="4"/>')
    for j, line in enumerate(lines):
        s.text(x0 + 16, 146 + j * 20, line, 'mono')
s.text(40, 296, 'dx, dy: pixels from the viewport centre. c: camera.target.', 'ts')
s.text(40, 314, 'Each View picks its own plane (Rule 4).', 'ts')
s.save(OUT, 'spec-mr-picking.svg')

# ---------------------------------------------------------------- pointer path
s = Svg(250, 'A click becomes a command', 'A mouse event is read once per frame into Input in pixels; Controls.sample turns a click into a world point through the View and latches it; at the next tick Controls.emit sends a Command whose vec is that point; simulate on the server checks reach and changes the World or not. Wheel and hover only move the camera or draw an outline.')
row1 = [('Mouse event', 'read each frame'), ('Input', 'pixels, latched'), ('Controls.sample', 'screen to world'), ('Latched click', 'button + point')]
for i, (title, sub) in enumerate(row1):
    x = 40 + i * 152
    s.box(x, 30, 140, 56, 'purple', title, sub)
    if i < 3:
        s.arrow([(x + 141, 58), (x + 151, 58)], 'purple')
s.line([(566, 87), (566, 110), (110, 110)])
s.arrow([(110, 110), (110, 128)])
row2 = [('Next tick: emit', 'vec = the point'), ('simulate', 'checks reach'), ('World', 'coin placed?')]
for i, (title, sub) in enumerate(row2):
    x = 40 + i * 152
    s.box(x, 130, 140, 56, 'teal', title, sub)
    if i < 2:
        s.arrow([(x + 141, 158), (x + 151, 158)], 'teal')
s.box(496, 130, 140, 56, 'gray', 'Wheel, hover', 'camera only')
legend(s, 222, [('purple', 'every frame'), ('teal', 'every tick (Rule 1)'), ('gray', 'presentation, no Command')])
s.save(OUT, 'spec-mr-pointer.svg')

# ---------------------------------------------------------------- ownership
s = Svg(320, 'What lives in the exe and what lives in the module', 'The exe owns all state: the World, Server and Client, the job system, Controls, views and backends, sounds and HUD. The game module holds code only: its GameApi with version and layout hash, simulate with the engine code it calls, and its bindings, sound and HUD functions. Each tick the exe lends the World to the module\'s simulate; nothing in the exe keeps a pointer into the module.')
s.frame(40, 30, 380, 240)
s.text(230, 48, 'scraps_client.exe / scraps_server.exe', 'th', 'middle')
exe = [
    (60, 70, 'purple', 'World', 'all game state'),
    (240, 70, 'gray', 'Server, Client', 'set_simulate()'),
    (60, 140, 'gray', 'JobSystem', 'worker threads'),
    (240, 140, 'gray', 'Controls', 'rebind(bindings)'),
    (60, 210, 'gray', 'Views, backends', 'need a restart'),
    (240, 210, 'gray', 'Sounds, HUD', 'strings copied'),
]
for x, y, ramp, title, sub in exe:
    s.box(x, y, 165, 48, ramp, title, sub)
s.frame(450, 30, 200, 240)
s.text(550, 48, 'Game module (.dll)', 'th', 'middle')
s.box(465, 70, 170, 48, 'teal', 'GameApi', 'version, layout hash')
s.box(465, 140, 170, 48, 'teal', 'simulate', '+ engine code it calls')
s.box(465, 210, 170, 48, 'teal', 'Bindings, hud', 'sound_events')
s.line([(406, 94), (435, 94), (435, 164)])
s.arrow([(435, 164), (463, 164)], 'teal')
s.text(40, 294, 'The World is lent to simulate each tick. Nothing points into the module.', 'ts')
s.save(OUT, 'spec-mr-ownership.svg')

# ---------------------------------------------------------------- reload
s = Svg(470, 'Swapping the game code', 'Build only the module target; the FileWatcher sees the DLL change and waits until the file is stable; the app copies it to a numbered live file and loads it; it checks the API version and layout hash. On a mismatch it keeps the old code and shows a message. Otherwise, between two frames, it swaps simulate in Server and Client and rebinds the controls, then unloads the old copy.')
steps = [
    ('teal', 'Build only the module', 'cmake --build --preset mingw-game'),
    ('teal', 'FileWatcher sees the .dll change', 'then waits for a stable file'),
    ('teal', 'Copy to live-N.dll and load it', 'the original stays writable'),
    ('purple', 'Check api_version, layout_hash', 'same engine structs?'),
    ('teal', 'Swap between two frames', 'set_simulate, rebind controls'),
    ('teal', 'Unload the old copy', 'HUD: game code reloaded'),
]
for i, (ramp, title, sub) in enumerate(steps):
    y = 30 + i * 68
    s.box(60, y, 320, 48, ramp, title, sub)
    if i < len(steps) - 1:
        s.arrow([(220, y + 49), (220, y + 66)])
s.text(232, 291, 'yes', 'ts')
s.arrow([(381, 258), (428, 258)], 'coral')
s.text(404, 246, 'no', 'ts', 'middle')
s.box(430, 234, 210, 48, 'coral', 'Keep the old code', 'HUD: engine mismatch')
legend(s, 446, [('teal', 'step'), ('purple', 'safety check'), ('coral', 'refused, nothing breaks')])
s.save(OUT, 'spec-mr-reload.svg')

print('wrote spec-mr-picking.svg, spec-mr-pointer.svg, spec-mr-ownership.svg, spec-mr-reload.svg')
