# Tiny Robotic Voice

Local streaming text-to-speech for AI agents.

- Streams partial responses before generation is complete
- Runs locally with no API key, cloud speech, or network access
- Uses bundled Flite `cmu_us_kal` speech rather than operating-system TTS
- Interrupts obsolete speech without restarting the process
- Keeps text and audio memory bounded

Supported: **macOS on Apple Silicon**. The bundled voice is English-focused and
intentionally robotic.

## Quick start

Install Apple's Command Line Tools if necessary, then build:

```sh
make
./build/trv doctor
./build/trv say "Tiny Robotic Voice is working."
```

The repository vendors pinned source revisions of Flite, miniaudio, and yyjson.
Building does not download dependencies, models, or voices. Flite is configured
without its own audio layer; TRV sends its in-memory PCM directly to miniaudio.

## Streaming

Start `./build/trv stream` as a foreground child process. Standard input and
output use newline-delimited JSON. Standard output contains protocol events only;
diagnostics go to standard error. `ready` means the speech engine, bundled voice,
playback device, audio callback, and workers are all usable.

```jsonl
{"type":"ready"}
{"type":"start","session":"response-42"}
{"type":"session_started","session":"response-42"}
{"type":"append","session":"response-42","text":"The interesting thing "}
{"type":"accepted","session":"response-42","bytes":22}
{"type":"append","session":"response-42","text":"is that speech can begin early."}
{"type":"accepted","session":"response-42","bytes":31}
{"type":"speaking","session":"response-42"}
{"type":"finish","session":"response-42"}
{"type":"finished","session":"response-42"}
{"type":"shutdown"}
{"type":"shutting_down"}
```

Each line sent to the process must contain exactly one JSON object. Producer
fragment boundaries have no speech meaning: TRV reconstructs text and commits
phrases at sentence, newline, clause, preferred-size, or hard-size boundaries. A
400 ms generation pause also flushes useful buffered text. `finish` flushes the
remainder and emits `finished` only after valid audio drains.

An optional integer `seq` on `append` is echoed by `accepted` or `backpressure`
errors. Unknown properties are ignored for forward compatibility. Lines larger
than 1 MiB are rejected.

### Interrupt and replace

```jsonl
{"type":"start","session":"old"}
{"type":"append","session":"old","text":"This is now obsolete and should stop."}
{"type":"interrupt","session":"old"}
{"type":"interrupted","session":"old"}
{"type":"start","session":"new"}
{"type":"append","session":"new","text":"This is the replacement."}
{"type":"finish","session":"new"}
```

Interruption invalidates buffered text, queued synthesis, synthesis already in
progress, queued PCM, and the PCM currently being consumed. A replacement session
can start immediately through the already-open device.

## Commands and exit codes

- `trv doctor [--json]` checks platform, Flite, the voice, Core Audio, and the
  default playback device without speaking.
- `trv say <text>` synthesizes, plays, drains, and exits.
- `trv stream` runs the foreground JSONL protocol until `shutdown`, stdin EOF,
  SIGINT, or SIGTERM.

Exit codes are `0` for success, `2` for CLI misuse, `3` for an unsupported
environment, `4` when audio is unavailable, and `5` for an internal failure.
Protocol errors do not terminate a healthy stream.

Stable protocol error codes currently include `invalid_json`, `invalid_message`,
`missing_field`, `invalid_field`, `unknown_type`, `line_too_large`,
`invalid_state`, `backpressure`, `synthesis_failed`, `audio_unavailable`,
`unsupported_environment`, and `internal_error`.

## Bounds and privacy

Pending unsynthesized text is capped at 64 KiB, queued audio at approximately 20
seconds, and individual protocol lines at 1 MiB. When text capacity is exhausted,
an append is rejected with a recoverable `backpressure` error; accepted text is
never silently dropped. The reader remains available for `interrupt` and
`shutdown` while audio is saturated.

TRV has no telemetry and makes no network requests. Streamed text is parsed as a
JSON string and passed only to the speech engine. It never becomes a command,
option, filename, lifecycle instruction, or log message.

## Development and acceptance

Run hardware-independent tests, including real in-memory Flite synthesis:

```sh
make test
```

Then perform the speaker test:

```sh
./tools/acceptance_producer.py ./build/trv
```

You should hear streamed speech start before all three fragments arrive, obsolete
speech stop, and replacement speech finish. This physical-speaker check is
required for release and cannot be replaced by CI.

## Current limitations

This MVP supports one session and one English-focused robotic voice. It has no
daemon, server, GUI, configuration file, SSML, Markdown processing, alternate
voice, speed control, neural model, cloud fallback, Linux build, or Windows build.
