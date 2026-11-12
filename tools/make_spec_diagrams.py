"""Writes the diagrams of docs/specs/2026-11-12-hardening-and-look.md into
docs/images/ (spec-hl-*.svg).

Usage, from the repository root: python tools/make_spec_diagrams.py
Shared drawing helpers: tools/diagram_svg.py.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from diagram_svg import Svg, legend  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'images')

# ---------------------------------------------------------------- handshake
s = Svg(440, 'Handshake with a challenge cookie', 'The client sends a padded Hello; the server answers with a small Challenge carrying a cookie and keeps nothing; the client repeats the Hello with the cookie; only then does the server reserve a slot and send the Welcome.')
s.box(60, 24, 170, 40, 'teal', 'Client')
s.box(450, 24, 170, 40, 'purple', 'Server')
s.line([(240, 70), (240, 420)], dashed=True)
s.line([(440, 70), (440, 420)], dashed=True)
s.arrow([(242, 100), (438, 125)], 'teal'); s.text(340, 90, 'Hello, padded to 32 B', 'ts', 'middle')
s.box(450, 130, 190, 48, 'purple', 'Keeps nothing', 'cookie = hash(address, time)')
s.arrow([(438, 190), (242, 215)], 'purple'); s.text(340, 180, 'Challenge (cookie), 13 B', 'ts', 'middle')
s.box(40, 208, 190, 48, 'teal', 'Has a cookie', 'proves it gets our replies')
s.arrow([(242, 265), (438, 290)], 'teal'); s.text(340, 255, 'Hello + cookie', 'ts', 'middle')
s.box(450, 290, 190, 48, 'purple', 'Cookie checks out', 'now reserve a slot')
s.arrow([(438, 350), (242, 375)], 'purple'); s.text(340, 340, 'Welcome (player, session token)', 'ts', 'middle')
s.arrow([(242, 395), (438, 418)], 'teal'); s.text(330, 385, 'Input + token: the player joins', 'ts', 'middle')
s.save(OUT, 'spec-hl-handshake.svg')

# ---------------------------------------------------------------- spoofing
s = Svg(140, 'A spoofed Hello', 'An attacker sends a Hello with a victim\'s address. The server replies with one 13-byte Challenge to the victim, smaller than the Hello, and keeps no slot.')
s.box(40, 30, 180, 56, 'coral', 'Attacker', 'fakes the victim\'s address')
s.box(250, 30, 180, 56, 'purple', 'Server', 'replies 13 B, keeps nothing')
s.box(460, 30, 180, 56, 'gray', 'Victim', 'gets one small packet')
s.arrow([(222, 58), (248, 58)], 'coral')
s.arrow([(432, 58), (458, 58)], 'purple')
s.text(40, 116, 'Reply smaller than the request: no amplification. No slot: nothing to exhaust. Rate limits cap the rest.', 'ts')
s.save(OUT, 'spec-hl-spoof.svg')

# ---------------------------------------------------------------- rate limits
s = Svg(450, 'Rate limits', 'Datagrams from connected clients are limited to 8 per tick each. From unknown addresses only a Hello is answered, through a per-address bucket and a global bucket, before a Challenge or Welcome is sent.')
s.box(250, 30, 180, 48, 'gray', 'A datagram arrives')
s.line([(340, 78), (340, 92), (190, 92), (190, 100)])
s.line([(340, 92), (490, 92), (490, 100)])
s.arrow([(190, 100), (190, 106)])
s.arrow([(490, 100), (490, 106)])
s.box(60, 108, 260, 56, 'purple', 'From a connected client', 'needs its session token')
s.box(360, 108, 260, 56, 'gray', 'From an unknown address', 'only a Hello gets an answer')
s.arrow([(190, 166), (190, 186)])
s.arrow([(490, 166), (490, 186)])
s.box(60, 188, 260, 56, 'purple', 'About 8 per tick', '480/s, burst 64; extra dropped')
s.box(360, 188, 260, 56, 'amber', 'Per-address bucket', '5 replies/s, burst 10, 64 addresses')
s.arrow([(490, 246), (490, 266)])
s.box(360, 268, 260, 56, 'amber', 'Global bucket', '200 replies/s for all strangers')
s.arrow([(490, 326), (490, 346)])
s.box(360, 348, 260, 56, 'teal', 'Challenge or Welcome sent')
legend(s, 428, [('amber', 'new limits'), ('purple', 'per-client work'), ('teal', 'reply')])
s.save(OUT, 'spec-hl-limits.svg')

# ---------------------------------------------------------------- look
s = Svg(330, 'Look and feel', 'Three changes: a sky colour or gradient per scene, a facing direction that stays when a character stops, and a drop shadow on the ground that shrinks as the entity rises.')
for i, title in enumerate(['Sky per scene', 'Facing stays', 'Drop shadows']):
    x = 40 + i * 205
    s.frame(x, 30, 190, 280)
    s.text(x + 95, 50, title, 'th', 'middle')
# sky: physical colours, the same in both themes
s.add('<defs><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#7EC8F0"/><stop offset="1" stop-color="#DDF1FA"/></linearGradient></defs>')
s.add('<rect x="55" y="68" width="160" height="150" rx="4" fill="url(#sky)"/>')
for c in range(4):
    s.add(f'<rect x="{55 + c * 40}" y="196" width="40" height="22" fill="#5E9C4C" stroke="#3B6D11" stroke-width="0.6"/>')
s.add('<circle cx="180" cy="98" r="12" fill="#FAC775"/>')
s.text(135, 244, 'sky 126 200 240', 'mono', 'middle')
s.text(135, 262, '    221 241 250', 'mono', 'middle')
s.text(135, 286, 'one colour, or top → bottom', 'ts', 'middle')


def walker(s, cx, cy, facing, ramp, dashed=False):
    dash = ' style="stroke-dasharray:3 3"' if dashed else ''
    nose = f'{cx + 9},{cy - 6} {cx + 17},{cy - 2} {cx + 9},{cy + 2}' if facing > 0 else \
        f'{cx - 9},{cy - 6} {cx - 17},{cy - 2} {cx - 9},{cy + 2}'
    s.add(f'<g class="{ramp}"><rect class="f" x="{cx - 9}" y="{cy - 16}" width="18" height="32" rx="4"{dash}/>'
          f'<polygon class="ff" points="{nose}"/></g>')


# facing
s.text(255, 92, 'walking left', 'ts')
walker(s, 395, 92, -1, 'teal')
s.text(255, 160, 'stops, before', 'ts')
walker(s, 395, 160, +1, 'coral', dashed=True)
s.text(255, 228, 'stops, new', 'ts')
walker(s, 395, 228, -1, 'teal')
s.text(340, 286, 'facing kept in the World', 'ts', 'middle')
# shadows
s.line([(460, 230), (630, 230)])
s.add('<g class="gray"><ellipse class="ff" cx="505" cy="232" rx="20" ry="6" style="fill-opacity:0.55"/></g>')
s.add('<g class="coral"><rect class="f" x="493" y="194" width="24" height="36" rx="4"/></g>')
s.add('<g class="gray"><ellipse class="ff" cx="590" cy="232" rx="12" ry="4" style="fill-opacity:0.35"/></g>')
s.add('<g class="coral"><rect class="f" x="578" y="128" width="24" height="36" rx="4"/></g>')
s.line([(590, 168), (590, 222)], dashed=True)
s.text(505, 254, 'on the ground', 'ts', 'middle')
s.text(590, 254, 'jumping', 'ts', 'middle')
s.text(545, 286, 'smaller and fainter when high', 'ts', 'middle')
s.save(OUT, 'spec-hl-look.svg')

# ---------------------------------------------------------------- audio
s = Svg(400, 'Sound from any common format', 'Loading: a sound file is recognised by its first bytes, decoded by the matching decoder and converted to 48 kHz stereo float. Playing: a game event starts a voice in the core mixer, which the backend feeds to the speakers.')
s.text(40, 22, 'Load: once per scene (core)', 'th')
s.text(380, 22, 'Play: every frame (client)', 'th')
left = [('gray', 'Sound file', 'any name, any extension'),
        ('purple', 'Recognise by content', 'first bytes: RIFF, FORM, fLaC, OggS, ID3…'),
        ('purple', 'Decode', 'dr_wav · dr_flac · dr_mp3 · stb_vorbis'),
        ('purple', 'Convert', '48 kHz stereo float, any rate or channels')]
right = [('teal', 'Game event', 'jump, coin picked up, coin dropped'),
         ('purple', 'Mixer (core)', '32 voices, volume, no allocation'),
         ('amber', 'Audio::submit (backend)', 'keeps about 60 ms queued'),
         ('gray', 'Speakers')]
for i, (ramp, t, sub) in enumerate(left):
    s.box(40, 40 + i * 80, 300, 56, ramp, t, sub)
    if i < 3:
        s.arrow([(190, 98 + i * 80), (190, 118 + i * 80)])
for i, item in enumerate(right):
    ramp, t = item[0], item[1]
    sub = item[2] if len(item) > 2 else None
    s.box(380, 40 + i * 80, 260, 56, ramp, t, sub)
    if i < 3:
        s.arrow([(510, 98 + i * 80), (510, 118 + i * 80)])
s.arrow([(342, 308), (360, 308), (360, 148), (378, 148)])
legend(s, 384, [('purple', 'core'), ('amber', 'backend'), ('teal', 'game, client side'), ('gray', 'outside the engine')])
s.save(OUT, 'spec-hl-audio.svg')
print('ok')
