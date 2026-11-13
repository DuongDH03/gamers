# Third-party code

Libraries at the edges (constitution section 1). Each is pinned to an exact
version so builds are reproducible. None of them is compiled with our
warnings-as-errors flags.

| Library | File | Version | Source | License |
|---|---|---|---|---|
| stb_image | `stb/stb_image.h` | v2.30, commit `4e9b368bfc071e97206ea5c6adba68dd0cbf8897`, SHA-256 `594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3` | <https://github.com/nothings/stb> | Public domain or MIT (your choice), see the end of the file |
| stb_vorbis | `stb/stb_vorbis.c` | v1.22, same commit, SHA-256 `4C7CB2FF1F7011E9D67950446B7EB9CA044F2E464D76BFBB0B84DD2E23E65636` | <https://github.com/nothings/stb> | Public domain or MIT |
| dr_wav | `dr_libs/dr_wav.h` | v0.14.6, commit `3c6605b26b2ae37b8bc58f4ae6f7c872c77e51ad`, SHA-256 `50CACB0FEC97BBE9A43F465E00ECA05E71DD7B3EB4A2A74AC8E8281D059B6149` | <https://github.com/mackron/dr_libs> | Public domain or MIT-0 |
| dr_flac | `dr_libs/dr_flac.h` | v0.13.4, same commit, SHA-256 `898AD822E21DA1127EF8065ECA655BCFF8DC5F6060A411724C5C48017488B1B6` | <https://github.com/mackron/dr_libs> | Public domain or MIT-0 |
| dr_mp3 | `dr_libs/dr_mp3.h` | v0.7.4, same commit, SHA-256 `390E4E0EF0CFF80FB86EBF1B461D6947CFA4ED699A4FE107E5A5E69A47DB2950` | <https://github.com/mackron/dr_libs> | Public domain or MIT-0 |

stb_image is compiled once, PNG only, in `stb/stb_image_impl.cpp` (target
`scraps_stb`). Only `engine/core/image.cpp` includes its header. Decision:
`docs/decisions/2026-11-12-images-and-sprites.md`.

The audio decoders are compiled once, memory only (no stdio), in
`audio_decoders.cpp` (target `scraps_audio_decoders`), behind the small C++
face `audio_decoders.h`; only `engine/core/sound.cpp` (whole sounds) and
`engine/core/music.cpp` (streamed music, `PullDecoder`) use it. Decision:
`docs/decisions/2026-11-12-audio-formats.md`.

The vendored files are unchanged. One known weakness of stb_vorbis 1.22 is
worked around in `audio_decoders.cpp` instead: it trusts the length fields of
an OGG file's comment header, and a damaged file can make it write outside
its memory. Every OGG file's comment header is checked before stb_vorbis
sees it (test: `core.music_rejects_bad_files`).

SDL3 is not vendored: CMake downloads a pinned release (`cmake/ScrapsSDL3.cmake`).
