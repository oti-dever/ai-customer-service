from __future__ import annotations

import argparse
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from statistics import mean


DEFAULT_LOG_DIR = Path(__file__).resolve().parents[1] / "rpa" / "logs" / "qianniu"

TIMING_MARKERS = [
    "qianniu observer timing",
    "qianniu scan_unread_timing",
    "qianniu session_scan_timing",
    "qianniu visual_unread_timing",
    "qianniu select_session_timing",
    "qianniu read_messages_timing",
    "qianniu parse_messages_timing",
    "qianniu sender_timing",
    "qianniu input_text_timing",
    "qianniu send_message timing",
    "rpa_event_store timing",
    "[IpcService] platform event dispatch timing",
    "[IpcService] event WebSocket parse timing",
    "[QianniuRPAAdapter] event timing",
    "[QianniuRPAAdapter] sendMessage timing",
    "[MessageRouter] send timing",
    "[MessageRouter] send confirm timing",
    "[AggregateChatForm] send click timing",
    "[AggregateChatForm] unified message UI timing",
    "[AggregateChatForm] inbound message UI timing",
    "[AggregateChatForm] outbound message UI timing",
]

FIELD_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=\s*\"?([^\"\s]+)\"?")


@dataclass
class TimingGroup:
    count: int = 0
    values: dict[str, list[float]] = field(default_factory=lambda: defaultdict(list))
    slowest: list[tuple[float, str]] = field(default_factory=list)

    def add(self, fields: dict[str, str], line: str) -> None:
        self.count += 1
        max_ms = 0.0
        for key, raw in fields.items():
            if not is_timing_field(key):
                continue
            try:
                value = float(raw)
            except ValueError:
                continue
            self.values[key].append(value)
            max_ms = max(max_ms, value)
        if max_ms > 0:
            self.slowest.append((max_ms, line.strip()))
            self.slowest.sort(key=lambda item: item[0], reverse=True)
            del self.slowest[5:]


def is_timing_field(key: str) -> bool:
    lower = key.lower()
    return lower in {"ms"} or lower.endswith("_ms") or lower.endswith("elapsedms")


def category_for(line: str) -> str | None:
    for marker in TIMING_MARKERS:
        if marker in line:
            return marker
    if "timing" in line and "qianniu" in line.lower():
        return "other qianniu timing"
    return None


def parse_fields(line: str) -> dict[str, str]:
    return {match.group(1): match.group(2) for match in FIELD_RE.finditer(line)}


def iter_lines(paths: list[Path]):
    for path in paths:
        if path.is_dir():
            for child in sorted(path.rglob("*.log")):
                yield from iter_lines([child])
            continue
        try:
            with path.open("r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    yield path, line
        except OSError as exc:
            print(f"skip {path}: {exc}")


def print_summary(groups: dict[str, TimingGroup]) -> None:
    if not groups:
        print("No qianniu timing logs found.")
        return

    print("Timing summary")
    print("==============")
    for name in sorted(groups):
        group = groups[name]
        print(f"\n{name}  count={group.count}")
        for key in sorted(group.values):
            values = group.values[key]
            if not values:
                continue
            print(
                f"  {key}: avg={mean(values):.1f}ms "
                f"max={max(values):.1f}ms min={min(values):.1f}ms samples={len(values)}"
            )
        if group.slowest:
            print("  slowest:")
            for value, line in group.slowest:
                print(f"    {value:.1f}ms  {line[:240]}")


def print_trace_index(traces: dict[str, list[str]], limit: int) -> None:
    if not traces:
        return
    print("\nTrace index")
    print("===========")
    for trace_id, lines in sorted(traces.items(), key=lambda item: (-len(item[1]), item[0]))[:limit]:
        print(f"\n{trace_id}  lines={len(lines)}")
        for line in lines[:8]:
            print(f"  {line[:240]}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze qianniu timing logs. Default path: python/rpa/logs/qianniu."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help=f"Log files or directories. Defaults to {DEFAULT_LOG_DIR}.",
    )
    parser.add_argument("--trace-limit", type=int, default=10, help="Number of trace groups to print.")
    args = parser.parse_args()

    groups: dict[str, TimingGroup] = defaultdict(TimingGroup)
    traces: dict[str, list[str]] = defaultdict(list)
    trace_keys = (
        "request_id",
        "event_id",
        "eventId",
        "client_message_id",
        "clientMessageId",
        "platform_msg_id",
        "platformMsgId",
    )

    paths = args.paths or [DEFAULT_LOG_DIR]
    for _path, line in iter_lines(paths):
        category = category_for(line)
        if not category:
            continue
        fields = parse_fields(line)
        groups[category].add(fields, line)
        for key in trace_keys:
            value = fields.get(key)
            if value:
                traces[f"{key}:{value}"].append(line.strip())

    print_summary(groups)
    print_trace_index(traces, args.trace_limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
