# Audio Assets

This directory contains audio files for the game.

## Required Audio Files for Terror Radius

The following audio files are expected by `assets/terror_radius/default_killer.json`:

| Filename | Purpose | Approx. Duration |
|----------|---------|------------------|
| `tr_far.wav` | Distant terror sound - low ambience when killer is far | 10-30s loop |
| `tr_mid.wav` | Mid-range tension building | 10-30s loop |
| `tr_close.wav` | Close proximity heartbeat/intensity | 10-30s loop |
| `tr_chase.wav` | Chase music layer (only plays during active chase) | 10-30s loop |

## Supported Formats

- `.wav` (preferred for uncompressed quality)
- `.ogg` (good compression, open format)
- `.mp3` (widely supported)
- `.flac` (lossless compression)

## Console Commands for Testing

Once audio files are placed here, test with:

```
audio_play tr_far music
audio_loop tr_close music
audio_stop_all
```

## Placeholder Generation

To generate placeholder test tones (Linux/WSL):

```bash
# Requires ffmpeg or sox
# Far layer - low ambient drone
ffmpeg -f lavfi -i "sine=frequency=80:duration=15" -af "volume=0.3" tr_far.wav

# Mid layer - higher tension
ffmpeg -f lavfi -i "sine=frequency=120:duration=15" -af "volume=0.4" tr_mid.wav

# Close layer - tense rhythm
ffmpeg -f lavfi -i "sine=frequency=180:duration=15:beeper=off" -af "volume=0.5" tr_close.wav

# Chase layer - high intensity
ffmpeg -f lavfi -i "sine=frequency=240:duration=15" -af "volume=0.6" tr_chase.wav
```

## Asset Version

Current format: Unversioned (audio files are raw assets)
JSON profiles use `asset_version` for schema tracking.
