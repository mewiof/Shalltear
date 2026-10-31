# Audio import fixtures

Audio.md M2's import suites read these. Every file is a synthesized tone written for this repository - no recorded or third-party content - so each channel is identifiable by its frequency and level:

| File | Channels | Rate | Frames | Content |
| --- | --- | --- | --- | --- |
| `stereo_44100_pcm16.wav`, `.aiff`, `.flac` | 2 | 44,100 | 4,410 | left 441 Hz at 0.5, right 882 Hz at 0.25, 16-bit |
| `stereo_44100.mp3`, `stereo_44100.ogg` | 2 | 44,100 | 4,410 before encoding | the same pair, lossy |
| `mono_22050_float.wav` | 1 | 22,050 | 2,205 | 441 Hz at 0.5, 32-bit float |
| `quad_48000_pcm16.flac` | 4 | 48,000 | 2,400 | channel _c_ at 480·(_c_+1) Hz and 0.1·(_c_+1), in FLAC's quad order |
| `three_48000.ogg` | 3 | 48,000 | 2,400 | the same scheme in Vorbis's left, centre, right order |
| `back51_48000_pcm16.wav` | 6 | 48,000 | 2,400 | the same scheme, `WAVEFORMATEXTENSIBLE` with speaker mask `0x3F` - back surrounds, where a six-channel count means side surrounds by default |

Generated 2026-09-23 with python-soundfile 0.14.0 over libsndfile 1.2.2, except `back51_48000_pcm16.wav`, which is written byte by byte so its mask is exactly `0x3F`. Regenerating changes the lossy files' bytes and nothing a test asserts: those tests read channels, rate, length within an encoder's padding, and level, never samples.
