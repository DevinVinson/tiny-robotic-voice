# Development measurements

Measurements from the first verified MVP build on macOS arm64 (2026-08-23).
They are engineering observations, not product guarantees.

| Measurement | Result |
| --- | ---: |
| Unstripped executable | 3,024,736 bytes |
| `doctor --json` wall time | 0.19 seconds on first measured run |
| `say` wall time, including ~2.25 seconds of speech and drain | 2.53 seconds |
| KAL sample rate | 8,000 Hz mono signed 16-bit PCM |
| First real synthesis fixture | 18,012 frames |

The environment sandbox prevented reliable resident-memory sampling through
`time -l`/`ps`, so no memory number is recorded yet. Queue memory is bounded by
design: 64 KiB of owned pending text, 20 seconds of queued mono PCM, and a 1 MiB
protocol-line limit.

The development acceptance fixture produced `speaking` after the first of three
timed fragments, before fragments two and three were sent. It then interrupted an
obsolete session and completed a replacement without restarting the process.
`say` and the full fixture both exited successfully through the default Core Audio
device. Human confirmation of what was physically audible remains a release step.
