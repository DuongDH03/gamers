"""Makes the sandbox game's sound effects and music in games/sandbox/scenes/sounds/.

Synthesised here (like tools/make_sandbox_art.py draws the art), then stored
in three different formats on purpose, to show that scenes can mix them:
jump.wav (WAV), coin.ogg (OGG Vorbis) and drop.mp3 (MP3).

music.ogg is a 30-second chiptune loop composed here (an original tune, not
taken from any game or song): a pulse-wave lead, a thin arpeggio, a
triangle bass and noise drums, 16 bars at 128 BPM in A minor. Every note
that rings past the end of bar 16 is wrapped around to the start, so the
loop has no seam. It is mono at 22,050 Hz and low bitrate, to stay small.

Usage, from the repository root: python tools/make_sandbox_sounds.py
WAV needs nothing; OGG and MP3 need ffmpeg on PATH (MSYS2:
pacman -S mingw-w64-ucrt-x86_64-ffmpeg).
"""
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'games', 'sandbox', 'scenes', 'sounds')
RATE = 44100
FFMPEG = shutil.which('ffmpeg') or r'D:\msys64\ucrt64\bin\ffmpeg.exe'


def sweep(f0, f1, seconds, volume=0.4, square=0.0):
    """A tone gliding from f0 to f1 Hz, with a quick attack and a smooth fade."""
    n = int(seconds * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / n
        freq = f0 + (f1 - f0) * t
        phase += 2 * math.pi * freq / RATE
        s = math.sin(phase)
        s = (1 - square) * s + square * (1.0 if s >= 0 else -1.0) * 0.6
        envelope = min(1.0, i / (0.004 * RATE)) * (1 - t) ** 1.5
        out.append(volume * envelope * s)
    return out


def write_wav(path, samples):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, s)) * 32767)) for s in samples))


def convert(wav_path, out_path, options):
    if shutil.which('ffmpeg') is None and not os.path.exists(FFMPEG):
        sys.exit('ffmpeg not found: needed for the OGG and MP3 sounds')
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-i', wav_path, '-map_metadata', '-1']
                   + options + [out_path], check=True)


# ---------------------------------------------------------------- music

MUSIC_RATE = 22050
BPM = 128
BARS = 16
SIXTEENTH = 60.0 / BPM / 4                      # seconds
LOOP = int(round(BARS * 16 * SIXTEENTH * MUSIC_RATE))  # 30 s exactly: 661,500 samples

# One chord per bar (A minor, with E major for the leading tone).
CHORDS = ['Am', 'Am', 'F', 'G', 'Am', 'Am', 'Dm', 'E',
          'F', 'G', 'Em', 'Am', 'Dm', 'G', 'E', 'E']
CHORD_NOTES = {  # root, third, fifth as MIDI notes around the 3rd octave
    'Am': (57, 60, 64), 'F': (53, 57, 60), 'G': (55, 59, 62), 'Dm': (50, 53, 57),
    'E': (52, 56, 59), 'Em': (52, 55, 59),
}

# The lead, bar by bar: (start, length) in sixteenths, then the note name.
MELODY = [
    [(0, 4, 'E5'), (4, 2, 'A5'), (6, 2, 'G5'), (8, 4, 'E5'), (12, 2, 'C5'), (14, 2, 'D5')],
    [(0, 6, 'E5'), (6, 2, 'D5'), (8, 2, 'C5'), (10, 2, 'B4'), (12, 4, 'A4')],
    [(0, 4, 'C5'), (4, 2, 'F5'), (6, 2, 'E5'), (8, 4, 'C5'), (12, 2, 'A4'), (14, 2, 'C5')],
    [(0, 6, 'D5'), (6, 2, 'B4'), (8, 4, 'G4'), (12, 2, 'B4'), (14, 2, 'D5')],
    [(0, 2, 'E5'), (2, 2, 'A5'), (4, 4, 'C6'), (8, 2, 'B5'), (10, 2, 'A5'), (12, 2, 'G5'), (14, 2, 'E5')],
    [(0, 8, 'A5'), (8, 2, 'G5'), (10, 2, 'E5'), (12, 2, 'D5'), (14, 2, 'C5')],
    [(0, 4, 'D5'), (4, 2, 'F5'), (6, 2, 'A5'), (8, 2, 'G5'), (10, 2, 'F5'), (12, 2, 'E5'), (14, 2, 'D5')],
    [(0, 4, 'E5'), (4, 4, 'G#5'), (8, 4, 'B5'), (12, 4, 'G#5')],
    [(0, 4, 'A5'), (4, 2, 'G5'), (6, 2, 'F5'), (8, 4, 'C5'), (12, 4, 'F5')],
    [(0, 4, 'G5'), (4, 2, 'F5'), (6, 2, 'D5'), (8, 4, 'B4'), (12, 4, 'D5')],
    [(0, 2, 'E5'), (2, 2, 'G5'), (4, 4, 'B5'), (8, 2, 'A5'), (10, 2, 'G5'), (12, 4, 'E5')],
    [(0, 6, 'C6'), (6, 2, 'B5'), (8, 8, 'A5')],
    [(0, 4, 'F5'), (4, 2, 'E5'), (6, 2, 'D5'), (8, 4, 'A5'), (12, 4, 'F5')],
    [(0, 2, 'G5'), (2, 2, 'A5'), (4, 4, 'B5'), (8, 4, 'D6'), (12, 4, 'B5')],
    [(0, 4, 'G#5'), (4, 4, 'B5'), (8, 4, 'E6'), (12, 2, 'D6'), (14, 2, 'B5')],
    [(0, 4, 'G#5'), (4, 6, 'E5'), (12, 2, 'B4'), (14, 2, 'D5')],
]

NOTE_INDEX = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}


def midi(name):
    return 12 * (int(name[-1]) + 1) + NOTE_INDEX[name[:-1]]


def frequency(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def at(sixteenth):
    """The sample where a sixteenth (counted from the start of the loop) begins."""
    return int(round(sixteenth * SIXTEENTH * MUSIC_RATE))


def add_note(buffer, start, length, freq, volume, wave, duty=0.5, release=0.04, vibrato=0.0):
    """Adds one note, wrapping around the end of the loop (so tails continue at the start)."""
    attack = int(0.005 * MUSIC_RATE)
    decay = int(0.06 * MUSIC_RATE)
    tail = int(release * MUSIC_RATE)
    phase = 0.0
    for i in range(length + tail):
        if i < attack:
            env = i / attack
        elif i < attack + decay:
            env = 1.0 - 0.3 * (i - attack) / decay
        else:
            env = 0.7
        if i >= length:
            env *= 1.0 - (i - length) / tail
        f = freq
        if vibrato and i > MUSIC_RATE * 0.15:
            f *= 1.0 + vibrato * math.sin(2 * math.pi * 5.5 * i / MUSIC_RATE)
        phase = (phase + f / MUSIC_RATE) % 1.0
        if wave == 'pulse':
            s = 1.0 if phase < duty else -1.0
        else:  # triangle
            s = 4.0 * abs(phase - 0.5) - 1.0
        buffer[(start + i) % LOOP] += volume * env * s


class Noise:
    """A small deterministic noise source (the same file every run)."""

    def __init__(self, seed):
        self.state = seed

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
        return self.state / 0x3FFFFFFF - 1.0


def add_kick(buffer, start, volume):
    n = int(0.16 * MUSIC_RATE)
    phase = 0.0
    for i in range(n):
        t = i / MUSIC_RATE
        phase += (45 + 110 * math.exp(-t * 30)) / MUSIC_RATE
        buffer[(start + i) % LOOP] += volume * math.exp(-t * 18) * math.sin(2 * math.pi * phase)


def add_snare(buffer, start, volume, noise):
    n = int(0.14 * MUSIC_RATE)
    for i in range(n):
        t = i / MUSIC_RATE
        tone = 0.4 * math.sin(2 * math.pi * 185 * t) * math.exp(-t * 40)
        buffer[(start + i) % LOOP] += volume * (noise.next() * math.exp(-t * 22) + tone)


def add_hat(buffer, start, volume, noise):
    n = int(0.04 * MUSIC_RATE)
    previous = 0.0
    for i in range(n):
        value = noise.next()
        high = value - previous  # a crude high-pass: hats are bright
        previous = value
        buffer[(start + i) % LOOP] += volume * 0.5 * high * math.exp(-i / MUSIC_RATE * 90)


def chiptune():
    """The sandbox's music: one 30-second loop, as floats in -1..1."""
    buffer = [0.0] * LOOP
    noise = Noise(2026)
    for bar, chord in enumerate(CHORDS):
        root, third, fifth = CHORD_NOTES[chord]
        base = bar * 16
        # Bass: eighth notes, root and octave, two octaves down.
        for step, note in enumerate([root, root, root + 12, root, root, root + 12, root + 7, root + 12]):
            add_note(buffer, at(base + 2 * step), at(2) - at(0) - 300, frequency(note - 12), 0.20, 'triangle',
                     release=0.03)
        # Arpeggio: sixteenths up and down the chord, an octave up, thin and quiet.
        pattern = [root, third, fifth, root + 12, fifth + 12, root + 12, fifth, third]
        for step in range(16):
            add_note(buffer, at(base + step), at(1) - at(0) - 200, frequency(pattern[step % 8] + 12), 0.045,
                     'pulse', duty=0.125, release=0.05)
        # Lead.
        for start, length, name in MELODY[bar]:
            add_note(buffer, at(base + start), at(base + start + length) - at(base + start) - 400,
                     frequency(midi(name)), 0.13, 'pulse', duty=0.25, release=0.08, vibrato=0.006)
        # Drums: kick on 1 and 3, snare on 2 and 4, hats between; the last
        # bar ends in a snare roll that leads back to the start.
        if bar < BARS - 1:
            add_kick(buffer, at(base), 0.45)
            add_kick(buffer, at(base + 8), 0.45)
            add_snare(buffer, at(base + 4), 0.14, noise)
            add_snare(buffer, at(base + 12), 0.14, noise)
        else:
            add_kick(buffer, at(base), 0.45)
            add_snare(buffer, at(base + 4), 0.14, noise)
            for step in range(8, 16):
                add_snare(buffer, at(base + step), 0.06 + 0.012 * (step - 8), noise)
        for step in (2, 6, 10, 14):
            add_hat(buffer, at(base + step), 0.10, noise)

    # A gentle low-pass, run around the loop twice so the filter's state at
    # the start is the one it has at the end: still no seam.
    smooth = 0.0
    alpha = 0.55
    for _ in range(2):
        for i in range(LOOP):
            smooth += alpha * (buffer[i] - smooth)
            buffer[i] = smooth
    peak = max(abs(s) for s in buffer)
    return [0.8 * s / peak for s in buffer]


def write_wav_at(path, samples, rate):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b''.join(struct.pack('<h', int(round(max(-1, min(1, s)) * 32767))) for s in samples))


def main():
    os.makedirs(OUT, exist_ok=True)
    jump = sweep(260, 720, 0.16, volume=0.35, square=0.3)
    coin = sweep(988, 988, 0.07, volume=0.3, square=0.2) + sweep(1319, 1319, 0.28, volume=0.3, square=0.2)
    drop = sweep(620, 240, 0.14, volume=0.35, square=0.1)
    write_wav(os.path.join(OUT, 'jump.wav'), jump)
    with tempfile.TemporaryDirectory() as tmp:
        write_wav(os.path.join(tmp, 'coin.wav'), coin)
        write_wav(os.path.join(tmp, 'drop.wav'), drop)
        convert(os.path.join(tmp, 'coin.wav'), os.path.join(OUT, 'coin.ogg'), ['-c:a', 'libvorbis', '-q:a', '4'])
        convert(os.path.join(tmp, 'drop.wav'), os.path.join(OUT, 'drop.mp3'), ['-c:a', 'libmp3lame', '-b:a', '96k'])
        # The music: bit-exact flags give the same file every run (a fixed
        # Ogg serial number), so re-running this script leaves it unchanged.
        write_wav_at(os.path.join(tmp, 'music.wav'), chiptune(), MUSIC_RATE)
        convert(os.path.join(tmp, 'music.wav'), os.path.join(OUT, 'music.ogg'),
                ['-c:a', 'libvorbis', '-q:a', '1', '-fflags', '+bitexact', '-flags:a', '+bitexact'])
    for name in sorted(os.listdir(OUT)):
        print(f'{name:10} {os.path.getsize(os.path.join(OUT, name)):7} bytes')


if __name__ == '__main__':
    main()
