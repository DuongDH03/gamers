"""Shared SVG drawing helpers for the Scraps docs diagrams (tools/make_*_diagrams.py).

Each SVG is standalone: colours live in its own <style>, with a dark variant
under prefers-color-scheme, so it reads well in VS Code and on GitHub.
"""
import os

RAMPS = {  # 50, 100, 200, 600, 800
    'purple': ('#EEEDFE', '#CECBF6', '#AFA9EC', '#534AB7', '#3C3489'),
    'teal':   ('#E1F5EE', '#9FE1CB', '#5DCAA5', '#0F6E56', '#085041'),
    'coral':  ('#FAECE7', '#F5C4B3', '#F0997B', '#993C1D', '#712B13'),
    'amber':  ('#FAEEDA', '#FAC775', '#EF9F27', '#854F0B', '#633806'),
    'gray':   ('#F1EFE8', '#D3D1C7', '#B4B2A9', '#5F5E5A', '#444441'),
    'blue':   ('#E6F1FB', '#B5D4F4', '#85B7EB', '#185FA5', '#0C447C'),
}


def style():
    light, dark = [], []
    light.append('text{font-family:"Segoe UI",system-ui,-apple-system,Helvetica,Arial,sans-serif;fill:#2C2C2A}')
    light.append('.t{font-size:14px}.ts{font-size:12px;fill:#5F5E5A}.th{font-size:14px;font-weight:600}')
    light.append('.mono{font-family:Consolas,"Cascadia Mono",Menlo,monospace;font-size:12px}')
    light.append('.ln{stroke:#73726C;fill:none;stroke-width:1.2}.dash{stroke-dasharray:4 3}')
    light.append('.frame{fill:none;stroke:#B4B2A9;stroke-width:0.8;stroke-dasharray:4 3}')
    light.append('.mk{fill:#73726C}')
    light.append('.bg{fill:#FCFCFB}')
    dark.append('.bg{fill:#1F1F1E}text{fill:#E8E6DE}.ts{fill:#B4B2A9}.ln{stroke:#9C9A92}.mk{fill:#9C9A92}.frame{stroke:#5F5E5A}')
    for name, (c50, c100, c200, c600, c800) in RAMPS.items():
        light.append(f'.{name} .f{{fill:{c50};stroke:{c600};stroke-width:0.8}}'
                     f'.{name} .ff{{fill:{c600}}}.{name} .tt{{fill:{c800}}}.{name} .st{{fill:{c600}}}'
                     f'.l-{name}{{stroke:{c600};fill:none;stroke-width:1.6}}.m-{name}{{fill:{c600}}}'
                     f'.x-{name}{{fill:{c600}}}')
        dark.append(f'.{name} .f{{fill:{c800};stroke:{c200}}}.{name} .ff{{fill:{c200}}}'
                    f'.{name} .tt{{fill:{c100}}}.{name} .st{{fill:{c200}}}'
                    f'.l-{name}{{stroke:{c200}}}.m-{name}{{fill:{c200}}}.x-{name}{{fill:{c200}}}')
    return ('<style>' + ''.join(light) + '@media (prefers-color-scheme: dark){' + ''.join(dark) + '}</style>')


def markers():
    out = ['<marker id="a-gray" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" '
           'orient="auto-start-reverse"><path class="mk" d="M1 1L9 5L1 9z"/></marker>']
    for name in RAMPS:
        out.append(f'<marker id="a-{name}" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" '
                   f'markerHeight="7" orient="auto-start-reverse"><path class="m-{name}" d="M1 1L9 5L1 9z"/></marker>')
    return '<defs>' + ''.join(out) + '</defs>'


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


class Svg:
    def __init__(self, height, title, desc):
        self.h = height
        self.parts = []
        self.title = title
        self.desc = desc

    def add(self, s):
        self.parts.append(s)

    # Every SVG paints its own background card, so its text stays readable even
    # when the viewer's page theme and the OS theme (which the SVG follows) differ.

    def text(self, x, y, s, cls='ts', anchor='start'):
        self.add(f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anchor}" dominant-baseline="central">{esc(s)}</text>')

    def box(self, x, y, w, h, ramp, title, sub=None, rx=8, mono_sub=False, align='middle'):
        tx = x + w / 2 if align == 'middle' else x + 14
        g = [f'<g class="{ramp}"><rect class="f" x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}"/>']
        if sub is None:
            g.append(f'<text class="th tt" x="{tx}" y="{y + h / 2}" text-anchor="{align}" dominant-baseline="central">{esc(title)}</text>')
        else:
            g.append(f'<text class="th tt" x="{tx}" y="{y + h / 2 - 10}" text-anchor="{align}" dominant-baseline="central">{esc(title)}</text>')
            cls = 'mono st' if mono_sub else 'ts st'
            g.append(f'<text class="{cls}" x="{tx}" y="{y + h / 2 + 10}" text-anchor="{align}" dominant-baseline="central">{esc(sub)}</text>')
        g.append('</g>')
        self.add(''.join(g))

    def arrow(self, pts, color='gray', dashed=False, both=False):
        d = 'M' + ' L'.join(f'{x} {y}' for x, y in pts)
        cls = 'ln' if color == 'gray' else f'l-{color}'
        extra = ' dash' if dashed else ''
        start = f' marker-start="url(#a-{color})"' if both else ''
        self.add(f'<path class="{cls}{extra}" d="{d}" marker-end="url(#a-{color})"{start}/>')

    def line(self, pts, color='gray', dashed=False, width=None):
        d = 'M' + ' L'.join(f'{x} {y}' for x, y in pts)
        cls = 'ln' if color == 'gray' else f'l-{color}'
        extra = ' dash' if dashed else ''
        w = f' style="stroke-width:{width}"' if width else ''
        self.add(f'<path class="{cls}{extra}" d="{d}"{w}/>')

    def frame(self, x, y, w, h, rx=10):
        self.add(f'<rect class="frame" x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}"/>')

    def save(self, out_dir, name):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="680" height="{self.h}" viewBox="0 0 680 {self.h}" '
               f'role="img"><title>{esc(self.title)}</title><desc>{esc(self.desc)}</desc>'
               + style() + markers() + f'<rect class="bg" width="680" height="{self.h}" rx="10"/>' + ''.join(self.parts) + '</svg>\n')
        with open(os.path.join(out_dir, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(svg)


def legend(s, y, items, x=40):
    for ramp, label in items:
        s.add(f'<g class="{ramp}"><rect class="f" x="{x}" y="{y - 6}" width="12" height="12" rx="2"/></g>')
        s.text(x + 18, y, label)
        x += 18 + len(label) * 6.6 + 26


