# Audio: every common format, decoded in core, mixed by the engine

- Date: 2026-11-12
- Status: **Accepted** (owner, 2026-11-12: "make it lenient for EVERY format";
  spec `docs/specs/2026-11-12-hardening-and-look.md` approved)

## Problem

The constitution left the audio asset format open. Sound effects from files
(plan item "look and feel") needed an answer. Offering one format was the
wrong question: the owner wants any sound file a game maker has to just work.

## Decision

**Read every common game-audio format, recognised by content, not by name:**

| Format | Decoder (vendored single file, public domain / MIT-0) |
|---|---|
| WAV (PCM 8/16/24/32-bit, float, ADPCM, A-law, µ-law; W64, RF64), AIFF, AIFF-C | dr_wav |
| FLAC (also in Ogg) | dr_flac |
| MP3 | dr_mp3 |
| OGG Vorbis | stb_vorbis |

- Any sample rate and 1 to 8 channels; everything is converted once at load
  to 48 kHz stereo float (`core/sound.h`). More than two channels are mixed
  down (centre and surround to both sides at −3 dB, LFE left out).
- **Recognised but refused with a clear error:** Opus (needs libopus, a full
  library, not a single file), AAC/M4A and WMA (patent-encumbered, need system
  codecs). Each can be added later behind the same `decode_sound`.
- **Mixing is ours** (`core/mixer.h`, 32 voices, no allocation), pure and
  tested sample by sample. The backend only receives finished samples through
  `Audio::submit` (Rule 3). The client mixes about 60 ms ahead on its own
  thread: no audio-thread callbacks, no locks.
- Decoders are pinned (`engine/third_party/README.md`), compiled once in
  their own target without our warning flags, behind a small C++ face
  (`third_party/audio_decoders.h`), like stb_image.
- Test files for every format are made by `tools/make_audio_fixtures.py`
  with ffmpeg (a development tool, installed through MSYS2 with the owner's
  approval; not shipped) and committed under `tests/data/audio/`.

## Consequences

- Sounds are presentation only (Rule 1): a game decides which events make
  which sound (`sandbox::sound_events`), the client plays them by name.
- Sound effects are capped at 30 s and 32 MB per file; music streaming
  (decoding while playing) is a later, separate piece of work.
  (Later, 2026-11-14: music streams through the same decoders,
  `core/music.h`, spec `docs/specs/2026-11-14-offline-essentials-and-ui.md`
  part 2. The cap for effects stays.)
