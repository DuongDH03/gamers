"""Writes the diagrams of docs/specs/2026-11-14-offline-essentials-and-ui.md into
docs/images/ (spec-ou-*.svg).

Usage, from the repository root: python tools/make_offline_ui_diagrams.py
Shared drawing helpers: tools/diagram_svg.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diagram_svg import Svg, legend  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'images')

# ---------------------------------------------------------------- waves
s = Svg(470, 'Build plan in two waves', 'Wave 1: four engine-dev agents in parallel git worktrees build save and load with pause, music, gamepad, and packaging; then a merge, a review and a bench. Wave 2, after the UI choice: UI prerequisites, then the UI layer; then review, bench, commit and push.')
s.text(40, 28, 'Wave 1: four agents at once, each on its own branch', 'th')
agents = [('Save, load, pause', 'core/save, pause'), ('Music', 'streaming, mixer'), ('Gamepad', 'Input, Controls'), ('Packaging', 'game choice, zip')]
for i, (title, sub) in enumerate(agents):
    x = 40 + i * 152
    s.box(x, 44, 140, 56, 'teal', title, sub)
    s.arrow([(x + 70, 101), (x + 70, 124)], 'teal')
s.box(40, 126, 600, 48, 'purple', 'Merge into main, then reviewer (Go/No-Go) and bench', 'one commit per part')
s.text(40, 214, 'Wave 2: after you choose UI layout A or B', 'th')
s.arrow([(495, 175), (495, 228)])
s.box(40, 230, 290, 56, 'teal', 'UI prerequisites', 'event list, text, one-frame loop')
s.arrow([(331, 258), (348, 258)], 'teal')
s.box(350, 230, 290, 56, 'teal', 'UI layer', 'backend + React screens')
s.arrow([(495, 287), (495, 318)])
s.box(350, 320, 290, 48, 'purple', 'Review, bench', 'commit')
s.arrow([(495, 369), (495, 396)])
s.box(350, 398, 290, 40, 'coral', 'Push to origin/main')
legend(s, 452, [('teal', 'engine-dev agent'), ('purple', 'checks and merge'), ('coral', 'goes public')])
s.save(OUT, 'spec-ou-waves.svg')

# ---------------------------------------------------------------- pause
s = Svg(260, 'Pause', 'Pressing P or the gamepad start button pauses both the embedded server and the client: no ticks, no inputs, timeouts frozen; frames keep drawing and music ducks. On resume the step clock is reset so no burst of ticks runs.')
s.box(40, 30, 160, 56, 'gray', 'P or pad start', 'offline only')
s.arrow([(201, 58), (228, 58)])
s.box(230, 30, 190, 56, 'purple', 'Server paused', 'no ticks, timeouts frozen')
s.box(450, 30, 190, 56, 'purple', 'Client paused', 'no prediction, no inputs')
s.box(230, 110, 410, 48, 'gray', 'Frames keep drawing the last world', 'HUD: Paused, music at 40 %')
s.arrow([(118, 87), (118, 200), (228, 200)])
s.box(230, 176, 410, 48, 'teal', 'Resume: unpause both, reset the step clock', 'the next tick follows; no catch-up burst')
legend(s, 244, [('purple', 'stopped'), ('gray', 'still running'), ('teal', 'resume')])
s.save(OUT, 'spec-ou-pause.svg')

# ---------------------------------------------------------------- UI layouts
s = Svg(330, 'UI layouts A and B', 'Layout A: a WebView2 window owns the screen; React draws menus in the page and a WebGL canvas draws the world from the triangle batches the engine Views already produce, sent through shared memory. Layout B: the sdl3 window draws the world on the GPU as today, and an offscreen browser engine renders React into a bitmap drawn on top as a texture.')
s.frame(40, 30, 290, 270)
s.text(185, 50, 'A. WebView draws everything', 'th', 'middle')
s.box(58, 66, 254, 48, 'coral', 'React: menus, HUD', 'DOM in the page')
s.box(58, 124, 254, 48, 'coral', 'WebGL canvas: the world', 'batches + textures')
s.arrow([(185, 238), (185, 174)], 'purple')
s.box(58, 240, 254, 48, 'purple', 'Native engine and Views', 'shared memory, no JSON')
s.frame(350, 30, 290, 270)
s.text(495, 50, 'B. HTML overlay', 'th', 'middle')
s.box(368, 66, 250, 48, 'coral', 'UI texture on top', 'redrawn when it changes')
s.box(368, 124, 250, 48, 'purple', 'World on the SDL GPU', 'as today')
s.arrow([(623, 264), (632, 264), (632, 90), (624, 90)], 'coral')
s.box(368, 240, 250, 48, 'coral', 'Offscreen browser engine', 'Ultralight or CEF')
legend(s, 316, [('coral', 'new UI pieces'), ('purple', 'engine, unchanged')])
s.save(OUT, 'spec-ou-ui.svg')

print('wrote spec-ou-waves.svg, spec-ou-pause.svg, spec-ou-ui.svg')
