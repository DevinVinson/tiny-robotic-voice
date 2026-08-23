#!/usr/bin/env python3
"""Development fixture for the physical-speaker streaming acceptance flow."""

import json
import subprocess
import sys
import threading
import time


def send(process: subprocess.Popen[str], message: dict) -> None:
    assert process.stdin is not None
    process.stdin.write(json.dumps(message) + "\n")
    process.stdin.flush()


def main() -> int:
    executable = sys.argv[1] if len(sys.argv) > 1 else "./build/trv"
    process = subprocess.Popen(
        [executable, "stream"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    assert process.stdout is not None
    events: list[dict] = []

    def reader() -> None:
        for line in process.stdout:
            event = json.loads(line)
            events.append(event)
            print(line, end="")

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    deadline = time.monotonic() + 5
    while not any(event.get("type") == "ready" for event in events):
        if time.monotonic() > deadline:
            process.kill()
            raise RuntimeError("TRV did not become ready")
        time.sleep(0.02)

    send(process, {"type": "start", "session": "streamed"})
    fragments = [
        "The interesting thing about this ",
        "project is that Tiny Robotic Voice can begin ",
        "speaking before the complete response has been generated.",
    ]
    for fragment in fragments:
        send(process, {"type": "append", "session": "streamed", "text": fragment})
        time.sleep(1)
    send(process, {"type": "finish", "session": "streamed"})
    while not any(event.get("type") == "finished" for event in events):
        time.sleep(0.02)

    send(process, {"type": "start", "session": "obsolete"})
    send(
        process,
        {
            "type": "append",
            "session": "obsolete",
            "text": "This response will be interrupted. It has obsolete queued speech. " * 4,
        },
    )
    while not any(
        event.get("type") == "speaking" and event.get("session") == "obsolete"
        for event in events
    ):
        time.sleep(0.02)
    send(process, {"type": "interrupt", "session": "obsolete"})
    send(process, {"type": "start", "session": "replacement"})
    send(
        process,
        {
            "type": "append",
            "session": "replacement",
            "text": "That response was interrupted. This is the replacement.",
        },
    )
    send(process, {"type": "finish", "session": "replacement"})
    while not any(
        event.get("type") == "finished" and event.get("session") == "replacement"
        for event in events
    ):
        time.sleep(0.02)
    send(process, {"type": "shutdown"})
    return process.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
