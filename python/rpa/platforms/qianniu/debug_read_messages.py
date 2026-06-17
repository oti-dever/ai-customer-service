from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict

from .reader import QianniuReader


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump Qianniu visible raw texts and parsed messages.")
    parser.add_argument("--limit", type=int, default=30, help="Maximum parsed messages to keep.")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    args = parser.parse_args()

    q_reader = QianniuReader()
    result, messages = q_reader.read_visible_messages_debug(limit=args.limit)

    if args.json:
        print(
            json.dumps(
                {
                    "read_result": asdict(result),
                    "raw_texts": result.texts,
                    "messages": [asdict(message) for message in messages],
                },
                ensure_ascii=False,
                indent=2,
            )
        )
        return 0 if result.ok else 1

    print("=== Qianniu Visible Texts ===")
    print(f"ok={result.ok} source={result.source} detail={result.detail or '-'} text_count={len(result.texts)}")
    for index, text in enumerate(result.texts, start=1):
        print(f"\n--- raw[{index}] ---")
        print(text)

    print("\n=== Parsed Messages ===")
    print(f"message_count={len(messages)}")
    for index, message in enumerate(messages, start=1):
        print(f"\n--- message[{index}] ---")
        print(f"sender={message.sender}")
        print(f"timestamp={message.timestamp}")
        print(f"direction={message.direction}")
        print(f"status={message.status or '-'}")
        print("text:")
        print(message.text)
        print("raw:")
        print(message.raw)

    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
