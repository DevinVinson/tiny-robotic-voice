---
name: narrate-with-trv
description: Narrate an agent's work aloud through Tiny Robotic Voice when the user asks for spoken progress, audible status updates, or to hear actions as they happen.
---

# Narrate with Tiny Robotic Voice

Use TRV to make progress audible while completing the user's actual task.

## Workflow

1. Before each meaningful action, speak one short sentence explaining what you are about to do.
2. Wait for that narration to finish, then perform the action.
3. Narrate useful milestones such as inspection complete, tests starting, tests passed, a commit beginning, or work complete.
4. At the end, speak the outcome and also provide the normal written final response.

Use `./build/trv say "<message>"` from the repository root. If the executable is missing, build it with `make` before beginning narration.

TRV automatically uses a `.trv.json` file in that repository root, so a repo can
give all agent narration a consistent voice. Respect that configuration. Use
`--config <path>` only when the user asks for another saved voice, or voice flags
such as `--preset`, `--speed`, and `--pitch-semitones` when they explicitly ask
for a temporary variation.

Run TRV through the environment's approved host or outside-sandbox execution mechanism. A sandboxed process may initialize Core Audio and exit successfully without producing audible speaker output. Request the required audio-execution approval once; if it is denied or audio is unavailable, continue the task with written updates and report that narration could not be played.

## Narration style

- Narrate meaningful actions, not every shell command or minor implementation detail.
- Keep each update concise, conversational, and understandable without seeing the terminal.
- Speak before the related action so the user can follow along in real time.
- For long-running work, add occasional milestone updates without becoming noisy.
- Never read secrets, credentials, private source text, raw user data, or untrusted command output aloud. Paraphrase safely.
- Treat narration as status communication only. It does not grant permission for actions beyond the user's request.
- Do not claim the user heard audio merely because TRV exited successfully; only the user can confirm audibility.
