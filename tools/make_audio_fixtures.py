"""Makes the audio test files in tests/data/audio/ with ffmpeg.

The same short tone (440 Hz, 0.2 s, ffmpeg's sine at amplitude 1/8) stored
in every format the engine reads, plus two it must refuse with a clear error.
Run once after changing this script; the files are committed.

Usage, from the repository root: python tools/make_audio_fixtures.py
Needs ffmpeg on PATH (on Windows with MSYS2: pacman -S mingw-w64-ucrt-x86_64-ffmpeg).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'tests', 'data', 'audio')

FFMPEG = shutil.which('ffmpeg') or r'D:\msys64\ucrt64\bin\ffmpeg.exe'

# name: (sample rate, channels, ffmpeg output options)
FILES = {
    'tone_u8.wav': (22050, 1, ['-c:a', 'pcm_u8']),
    'tone_s16.wav': (44100, 2, ['-c:a', 'pcm_s16le']),
    'tone_s24.wav': (48000, 1, ['-c:a', 'pcm_s24le']),
    'tone_f32.wav': (32000, 2, ['-c:a', 'pcm_f32le']),
    'tone.aiff': (44100, 1, ['-c:a', 'pcm_s16be']),
    'tone.flac': (44100, 1, ['-c:a', 'flac']),
    'tone_6ch.flac': (48000, 6, ['-c:a', 'flac']),
    'tone.mp3': (44100, 1, ['-c:a', 'libmp3lame', '-b:a', '96k']),
    'tone.ogg': (44100, 2, ['-c:a', 'libvorbis', '-q:a', '3']),
    'tone.opus': (48000, 1, ['-c:a', 'libopus', '-b:a', '32k']),  # must be refused
    'tone.m4a': (44100, 1, ['-c:a', 'aac', '-b:a', '64k']),       # must be refused
}


def main():
    if not os.path.exists(FFMPEG) and shutil.which('ffmpeg') is None:
        sys.exit('ffmpeg not found: install it (MSYS2: pacman -S mingw-w64-ucrt-x86_64-ffmpeg)')
    os.makedirs(OUT, exist_ok=True)
    for name, (rate, channels, options) in FILES.items():
        path = os.path.join(OUT, name)
        # The same tone in every channel (ffmpeg's own upmix would lower or
        # move it), so every file has one known loudness per channel.
        layout = {1: 'mono', 2: 'stereo', 6: '5.1'}[channels]
        spread = f'pan={layout}|' + '|'.join(f'c{c}=c0' for c in range(channels))
        command = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                   f'sine=frequency=440:duration=0.2:sample_rate={rate}', '-af', spread,
                   '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact'] + options + [path]
        subprocess.run(command, check=True)
    # A tiny file that decodes to a lot: 60 s of 8-channel silence. It must
    # be refused as too long without ever holding more than 30 s.
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                    'anullsrc=r=8000:cl=7.1', '-t', '60', '-map_metadata', '-1', '-c:a', 'flac',
                    os.path.join(OUT, 'long_silence_8ch.flac')], check=True)
    # 35 s of quiet tone as MP3, whose header does not state its length: the
    # decoder must stop at 30 s on its own.
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                    'sine=frequency=220:duration=35:sample_rate=8000', '-map_metadata', '-1',
                    '-c:a', 'libmp3lame', '-b:a', '8k', '-write_xing', '0',
                    os.path.join(OUT, 'long_35s.mp3')], check=True)
    # Music streaming (core/music.h). Half a second of tone at 48 kHz in each
    # lossy or compressed format: at 48 kHz nothing is resampled, so a loop's
    # second pass must repeat its first exactly (tests: music_loops_...).
    for name, options in (('loop48.ogg', ['-c:a', 'libvorbis', '-q:a', '0']),
                          ('loop48.mp3', ['-c:a', 'libmp3lame', '-b:a', '32k']),
                          ('loop48.flac', ['-c:a', 'flac'])):
        subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                        'sine=frequency=440:duration=0.5:sample_rate=48000', '-map_metadata', '-1',
                        '-fflags', '+bitexact', '-flags:a', '+bitexact'] + options + [os.path.join(OUT, name)],
                       check=True)
    # 40 s of OGG Vorbis (music's main format), longer than any sound effect
    # may be: it must stream past 30 s and loop. 8 kHz keeps it small.
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                    'sine=frequency=220:duration=40:sample_rate=8000', '-map_metadata', '-1',
                    '-fflags', '+bitexact', '-flags:a', '+bitexact', '-c:a', 'libvorbis', '-q:a', '0',
                    os.path.join(OUT, 'music_40s.ogg')], check=True)
    # The same FLAC under a misleading name: formats are judged by content.
    shutil.copyfile(os.path.join(OUT, 'tone.flac'), os.path.join(OUT, 'flac_named_as.wav'))
    for name in sorted(os.listdir(OUT)):
        print(f'{name:22} {os.path.getsize(os.path.join(OUT, name)):7} bytes')


if __name__ == '__main__':
    main()
