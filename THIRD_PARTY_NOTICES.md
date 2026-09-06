# Third-party notices

The source revisions below are vendored and built locally. Their upstream license
files are retained alongside their sources.

## Flite

- Revision: `6c9f20dc915b17f5619340069889db0aa007fcdc`
- Source: <https://github.com/festvox/flite>
- Use: speech core, US English frontend, CMU lexicon, `cmu_us_kal16` diphone
  voice, and `cmu_us_rms` ClusterGen voice
- License text: [`third_party/flite/COPYING`](third_party/flite/COPYING)
- Local configuration: built with `--with-audio=none --with-langvox=ben`; upstream
  source is otherwise unmodified.

Flite's notice requires the software to be identified as not being part of the
Festival Speech Synthesis System and not being a derivative of Festival. Tiny
Robotic Voice is an independent utility that uses Flite as a library.

## miniaudio

- Revision: `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`
- Source: <https://github.com/mackron/miniaudio>
- Use: Core Audio playback abstraction
- License text: [`third_party/miniaudio/LICENSE`](third_party/miniaudio/LICENSE)

Tiny Robotic Voice uses miniaudio under its MIT No Attribution option.

## yyjson

- Revision: `db37a64d63d38a8ddea35aa811b1163831028490`
- Source: <https://github.com/ibireme/yyjson>
- Use: strict JSON parsing and protocol-event serialization
- License text: [`third_party/yyjson/LICENSE`](third_party/yyjson/LICENSE)
