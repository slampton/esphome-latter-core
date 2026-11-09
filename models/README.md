# Wake word models

Latter builds load every micro_wake_word model from this folder (`github://slampton/esphome-latter-core/models/<name>.json@main`, pinned to the commit under test in CI), so firmware builds never depend on github.com's `/raw/` download path.

| Model | Source | License |
|---|---|---|
| okay_nabu, hey_jarvis, hey_mycroft, alexa, vad | [esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models) `models/v2/`, copied byte-for-byte | Apache License 2.0 |
| hey_luna | [dreimer1986/micro-wake-word-models](https://github.com/dreimer1986/micro-wake-word-models) | see upstream |

The SHA-256 of each file is listed next to the `micro_wake_word:` block in `modules/audio_voice.yaml`.

## Updating

On every ESPHome release intake, compare each upstream `models/v2/<name>.json` and its `.tflite` with the copies here. If upstream changed, copy both files, update the hashes in `modules/audio_voice.yaml`, and note the change in the release notes.
