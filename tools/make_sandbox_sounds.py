"""Makes the sandbox game's sound effects in games/sandbox/scenes/sounds/.

Synthesised here (like tools/make_sandbox_art.py draws the art), then stored
in three different formats on purpose, to show that scenes can mix them:
jump.wav (WAV), coin.ogg (OGG Vorbis) and drop.mp3 (MP3).

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
    for name in sorted(os.listdir(OUT)):
        print(f'{name:10} {os.path.getsize(os.path.join(OUT, name)):7} bytes')


if __name__ == '__main__':
    main()
