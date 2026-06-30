from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict
from pathlib import Path

from .detector import QQDetector
from .media_context_menu import (
    COPY_MENU_NAMES,
    _context_menu_click_point,
    _find_qq_copy_menu_item,
    _right_click_point,
)
from .messages import QQVisibleMessage, read_visible_messages
from .navigator import QQNavigator, bring_window_to_foreground, rect_inside
from .reader import QQReader
from .sender import QQSender
from .uia import summarize_control, trim, uia_guard, walk_controls


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="QQ PC UIA probe")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("windows", help="list visible QQ.exe top-level windows")

    summary = sub.add_parser("summary", help="print best window and candidate controls")
    summary.add_argument("--limit", type=int, default=8)

    tree = sub.add_parser("tree", help="dump UIA tree for the best QQ window")
    tree.add_argument("--output", default="", help="optional JSONL output path")
    tree.add_argument("--max-depth", type=int, default=18)
    tree.add_argument("--max-nodes", type=int, default=8000)

    messages = sub.add_parser("messages", help="print visible text candidates")
    messages.add_argument("--limit", type=int, default=80)
    messages.add_argument("--visible-only", action="store_true", help="only print controls with a non-zero rectangle")
    messages.add_argument("--content-only", action="store_true", help="hide chrome, timestamps, links, and simple status text")
    messages.add_argument("--visual-order", action="store_true", help="sort output by screen position instead of UIA tree order")
    messages.add_argument("--structured", action="store_true", help="print grouped visible messages")
    messages.add_argument("--include-media", action="store_true", help="include structured visible media messages")
    messages.add_argument("--jsonl", action="store_true", help="print structured messages as JSON lines")
    messages.add_argument("--evidence", action="store_true", help="capture visible media evidence screenshots")
    messages.add_argument("--evidence-dir", default="", help="optional media evidence output directory")

    conversations = sub.add_parser("conversations", help="print visible conversation list items")
    conversations.add_argument("--limit", type=int, default=40)

    media_layout = sub.add_parser("media-layout", help="print candidate media controls in the current chat area")
    media_layout.add_argument("--limit", type=int, default=80)
    media_layout.add_argument("--output", default="", help="optional JSONL output path")

    media_messages = sub.add_parser("media-messages", help="print structured visible media messages")
    media_messages.add_argument("--limit", type=int, default=40)
    media_messages.add_argument("--jsonl", action="store_true", help="print each media message as JSON")
    media_messages.add_argument("--evidence", action="store_true", help="capture visible media evidence screenshots")
    media_messages.add_argument("--evidence-dir", default="", help="optional media evidence output directory")

    media_menu = sub.add_parser("media-menu", help="right-click a visible media row and print QQ menu items")
    media_menu.add_argument("--limit", type=int, default=40)
    media_menu.add_argument("--index", type=int, default=1, help="1-based media message index")

    unread = sub.add_parser("unread", help="print visible unread conversation candidates")
    unread.add_argument("--limit", type=int, default=40)

    next_unread = sub.add_parser("next-unread", help="switch to the first visible unread conversation")
    next_unread.add_argument("--limit", type=int, default=40)
    next_unread.add_argument("--wait", type=float, default=0.6)
    next_unread.add_argument("--read", action="store_true", help="read structured messages after selecting unread")

    switch = sub.add_parser("switch", help="switch to a visible conversation by title")
    switch.add_argument("--title", required=True)
    switch.add_argument("--wait", type=float, default=0.6)
    switch.add_argument("--no-activate", action="store_true", help="do not bring QQ to foreground before clicking")
    switch_mode = switch.add_mutually_exclusive_group()
    switch_mode.add_argument("--uia", action="store_true", help="only try UIA patterns without click fallback")
    switch_mode.add_argument("--no-uia", action="store_true", help="skip UIA patterns and use foreground click")

    draft = sub.add_parser("draft", help="fill current QQ input box draft; does not send")
    draft.add_argument("--text", required=True)
    draft.add_argument("--no-activate", action="store_true", help="do not bring QQ to foreground before paste fallback")
    draft_mode = draft.add_mutually_exclusive_group()
    draft_mode.add_argument("--uia", action="store_true", help="only try UIA ValuePattern without paste fallback")
    draft_mode.add_argument("--no-uia", action="store_true", help="use foreground paste path; this is the default")

    args = parser.parse_args()
    detector = QQDetector()

    if args.command == "windows":
        windows = detector.find_process_windows()
        print(f"process={detector.q.process_name} visible_windows={len(windows)}")
        for item in windows:
            print(
                f"hwnd=0x{item.hwnd:X} pid={item.pid} class={item.class_name or '-'} "
                f"area={item.area} rect={item.rect} title={trim(item.title) or '-'}"
            )
        return 0 if windows else 1

    if args.command == "summary":
        logs: list[str] = []
        handle = detector.find_current_chat(log=logs.append)
        for line in logs:
            print(line)
        if not handle:
            print("no QQ chat window candidate found")
            return 1
        print_candidate_summary(detector, handle.chat_root, args.limit)
        return 0

    if args.command == "tree":
        handle = detector.find_current_chat()
        if not handle:
            print("no QQ chat window candidate found")
            return 1
        with uia_guard("qq_probe_tree"):
            rows = collect_tree_rows(handle.chat_root, args.max_depth, args.max_nodes)
        if args.output:
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("w", encoding="utf-8") as file:
                for row in rows:
                    file.write(json.dumps(row, ensure_ascii=False) + "\n")
            print(f"written={output} nodes={len(rows)}")
        else:
            for row in rows[:200]:
                indent = "  " * int(row["depth"])
                print(
                    f"{indent}{row['depth']:02d} type={row['control_type'] or '-'} "
                    f"class={row['class_name'] or '-'} aid={row['automation_id'] or '-'} "
                    f"name={trim(row['name']) or '-'} rect={row['rect']} children={row['child_count']}"
                )
            if len(rows) > 200:
                print(f"... truncated display; nodes={len(rows)}")
        return 0

    if args.command == "messages":
        reader = QQReader()
        if args.include_media and not args.structured:
            if args.jsonl:
                print(json.dumps({"ok": False, "error": "include_media_requires_structured"}, ensure_ascii=False))
            else:
                print("error=include_media_requires_structured")
            return 2
        if args.evidence and not (args.structured and args.include_media):
            if args.jsonl:
                print(json.dumps({"ok": False, "error": "evidence_requires_structured_include_media"}, ensure_ascii=False))
            else:
                print("error=evidence_requires_structured_include_media")
            return 2
        if args.jsonl and not args.structured:
            print(json.dumps({"ok": False, "error": "jsonl_requires_structured"}, ensure_ascii=False))
            return 2
        if args.structured:
            if args.include_media:
                result = read_visible_messages(
                    limit=args.limit,
                    include_media=True,
                    capture_evidence=args.evidence,
                    evidence_dir=args.evidence_dir or None,
                    reader=reader,
                )
                if args.jsonl:
                    if not result.ok:
                        print(
                            json.dumps(
                                {
                                    "ok": False,
                                    "source": result.source,
                                    "title": result.title,
                                    "detail": result.detail,
                                    "media_count": result.media_count,
                                },
                                ensure_ascii=False,
                            )
                        )
                    for item in result.messages:
                        print(json.dumps(item.as_dict(), ensure_ascii=False))
                else:
                    print(
                        f"ok={result.ok} source={result.source} title={result.title or '-'} "
                        f"detail={result.detail or '-'} media_count={result.media_count}"
                    )
                    for item in result.messages:
                        print_visible_message_item(item)
                return 0 if result.ok else 1

            result = reader.read_structured_messages(limit=args.limit)
            if args.jsonl:
                if not result.ok:
                    print(
                        json.dumps(
                            {
                                "ok": False,
                                "source": result.source,
                                "title": result.title,
                                "detail": result.detail,
                            },
                            ensure_ascii=False,
                        )
                    )
                for index, item in enumerate(result.messages, start=1):
                    print(json.dumps(structured_text_message_json(index, item), ensure_ascii=False))
                return 0 if result.ok else 1

            print(f"ok={result.ok} source={result.source} title={result.title or '-'} detail={result.detail or '-'}")
            for index, item in enumerate(result.messages, start=1):
                print(
                    f"{index:03d} direction={item.direction} sender={item.sender or '-'} "
                    f"time={item.time_text or '-'} confidence={item.confidence:.2f} "
                    f"raw_count={item.raw_count} rect={item.rect} text={trim(item.text, 220)}"
                )
            return 0 if result.ok else 1

        result = reader.read_visible_texts(limit=args.limit)
        print(f"ok={result.ok} source={result.source} title={result.title or '-'} detail={result.detail or '-'}")
        texts = result.texts
        if args.visible_only:
            texts = [item for item in texts if has_visible_rect(item.rect)]
        if args.content_only:
            texts = [item for item in texts if looks_like_message_content(item.text)]
        if args.visual_order:
            texts = sorted(texts, key=visual_sort_key)
        for index, item in enumerate(texts, start=1):
            print(
                f"{index:03d} depth={item.depth} source={item.source} type={item.control_type or '-'} "
                f"class={item.class_name or '-'} aid={item.automation_id or '-'} rect={item.rect} "
                f"text={trim(item.text, 180)}"
            )
        return 0 if result.ok else 1

    if args.command == "conversations":
        result = QQReader().read_conversations(limit=args.limit)
        print(
            f"ok={result.ok} source={result.source} current_title={result.current_title or '-'} "
            f"detail={result.detail or '-'}"
        )
        for item in result.conversations:
            print_conversation_item(item)
        return 0 if result.ok else 1

    if args.command == "media-layout":
        result = QQReader().read_media_layout(limit=args.limit)
        print(f"ok={result.ok} source={result.source} title={result.title or '-'} detail={result.detail or '-'}")
        if args.output and result.ok:
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("w", encoding="utf-8") as file:
                for item in result.items:
                    file.write(json.dumps(asdict(item), ensure_ascii=False) + "\n")
            print(f"written={output} items={len(result.items)}")
        for item in result.items:
            print_media_layout_item(item)
        return 0 if result.ok else 1

    if args.command == "media-messages":
        result = QQReader().read_media_messages(
            limit=args.limit,
            capture_evidence=args.evidence,
            evidence_dir=args.evidence_dir or None,
        )
        if args.jsonl:
            if not result.ok:
                print(
                    json.dumps(
                        {
                            "ok": False,
                            "source": result.source,
                            "title": result.title,
                            "detail": result.detail,
                        },
                        ensure_ascii=False,
                    )
                )
                return 1
            for item in result.messages:
                print(json.dumps(asdict(item), ensure_ascii=False))
            return 0

        print(f"ok={result.ok} source={result.source} title={result.title or '-'} detail={result.detail or '-'}")
        for item in result.messages:
            print_media_message_item(item)
        return 0 if result.ok else 1

    if args.command == "media-menu":
        handle = detector.find_current_chat()
        if not handle:
            print("ok=False stage=find_chat detail=qq_window_not_found")
            return 1
        reader = QQReader(detector.config)
        result = reader.read_media_messages(limit=args.limit)
        if not result.ok:
            print(f"ok=False stage=read_media detail={result.detail or '-'}")
            return 1
        if not result.messages:
            print("ok=True stage=no_media_visible menu_count=0")
            return 0
        index = max(1, min(int(args.index or 1), len(result.messages)))
        item = result.messages[index - 1]
        rect = parse_rect(item.media_rect or item.rect)
        if rect is None:
            print("ok=False stage=rect detail=invalid_media_rect")
            return 1
        if not rect_inside(rect, handle.window.window.rect):
            print("ok=False stage=bounds detail=media_rect_outside_window")
            return 1
        if not bring_window_to_foreground(handle.window.window.hwnd):
            print("ok=False stage=foreground detail=set_foreground_failed")
            return 1
        with uia_guard("qq_probe_media_menu"):
            point = _context_menu_click_point(rect, content_type=item.content_type)
            if not _right_click_point(point):
                print("ok=False stage=right_click detail=right_click_failed")
                return 1
            menu_item, names = _find_qq_copy_menu_item(COPY_MENU_NAMES, anchor_point=point)
        print(
            f"ok=True stage=menu index={index} type={item.content_type or '-'} "
            f"platform_msg_id={item.platform_msg_id or '-'} click_point={point} "
            f"copy_found={bool(menu_item)} menu_count={len(names)}"
        )
        for name in names:
            print(f"menu={name}")
        return 0

    if args.command == "unread":
        result = QQReader().read_conversations(limit=args.limit)
        print(
            f"ok={result.ok} source={result.source} current_title={result.current_title or '-'} "
            f"detail={result.detail or '-'}"
        )
        unread_items = visible_unread_items(result.conversations)
        for item in unread_items:
            print_conversation_item(item)
        print(f"unread_visible={len(unread_items)}")
        return 0 if result.ok else 1

    if args.command == "next-unread":
        reader = QQReader()
        result = reader.read_conversations(limit=args.limit)
        if not result.ok:
            print(
                f"ok=False stage=scan current_title={result.current_title or '-'} "
                f"detail={result.detail or '-'}"
            )
            return 1

        unread_items = visible_unread_items(result.conversations)
        if not unread_items:
            print(f"ok=True stage=no_unread_visible current_title={result.current_title or '-'} detail=-")
            return 0

        current_unread = next((item for item in unread_items if item.is_current_candidate), None)
        if current_unread is not None:
            print(
                f"ok=True stage=current_unread target={current_unread.title or '-'} "
                f"unread={current_unread.unread_hint or '-'} current_title={result.current_title or '-'} "
                f"rect={current_unread.rect} detail=-"
            )
            if args.read:
                print_structured_messages(reader.read_structured_messages(limit=40))
            return 0

        target = unread_items[0]
        switch_result = QQNavigator().switch_to_conversation(target.title, wait_seconds=args.wait)
        print(
            f"ok={switch_result.ok} stage={switch_result.stage} target={target.title or '-'} "
            f"unread={target.unread_hint or '-'} before_title={switch_result.before_title or '-'} "
            f"after_title={switch_result.after_title or '-'} matched_title={switch_result.matched_title or '-'} "
            f"matched_rect={switch_result.matched_rect or '-'} method={switch_result.method or '-'} "
            f"detail={switch_result.detail or '-'}"
        )
        if args.read and switch_result.ok:
            print_structured_messages(reader.read_structured_messages(limit=40))
        return 0 if switch_result.ok else 1

    if args.command == "switch":
        result = QQNavigator().switch_to_conversation(
            args.title,
            wait_seconds=args.wait,
            activate=not args.no_activate,
            use_uia=not args.no_uia,
            uia_only=args.uia,
        )
        print(
            f"ok={result.ok} stage={result.stage} target={result.target or '-'} "
            f"before_title={result.before_title or '-'} after_title={result.after_title or '-'} "
            f"matched_title={result.matched_title or '-'} matched_rect={result.matched_rect or '-'} "
            f"method={result.method or '-'} detail={result.detail or '-'}"
        )
        return 0 if result.ok else 1

    if args.command == "draft":
        result = QQSender().prepare_reply_draft(
            args.text,
            activate=not args.no_activate,
            use_uia=args.uia,
            uia_only=args.uia,
        )
        print(f"ok={result.ok} stage={result.stage} method={result.method or '-'} detail={result.detail or '-'}")
        return 0 if result.ok else 1

    return 2


def print_candidate_summary(detector: QQDetector, chat_root: object, limit: int) -> None:
    titles = detector.find_title_candidates(chat_root, limit=limit)
    message_areas = detector.find_message_area_candidates(chat_root)[:limit]
    input_areas = detector.find_input_area_candidates(chat_root)[:limit]

    print("title candidates:")
    for score, name, item in titles:
        print(f"  score={score} depth={item.depth} reason={item.reason} name={name} {detector.describe_control(item.control)}")
    print("message area candidates:")
    for item in message_areas:
        print(f"  score={item.score} depth={item.depth} reason={item.reason} {detector.describe_control(item.control)}")
    print("input area candidates:")
    for item in input_areas:
        print(f"  score={item.score} depth={item.depth} reason={item.reason} {detector.describe_control(item.control)}")


def print_conversation_item(item: object) -> None:
    current = " current=1" if getattr(item, "is_current_candidate", False) else ""
    unread = f" unread={getattr(item, 'unread_hint', '')}" if getattr(item, "unread_hint", "") else ""
    print(
        f"{getattr(item, 'index', 0):03d}{current}{unread} rect={getattr(item, 'rect', '-') or '-'} "
        f"title={trim(getattr(item, 'title', ''), 80) or '-'} time={getattr(item, 'time_text', '') or '-'} "
        f"preview={trim(getattr(item, 'preview', ''), 180) or '-'}"
    )


def visible_unread_items(items: list[object]) -> list[object]:
    return [item for item in items if getattr(item, "unread_hint", "")]


def print_structured_messages(result: object) -> None:
    print(
        f"messages_ok={getattr(result, 'ok', False)} source={getattr(result, 'source', '') or '-'} "
        f"title={getattr(result, 'title', '') or '-'} detail={getattr(result, 'detail', '') or '-'}"
    )
    for index, item in enumerate(getattr(result, "messages", []), start=1):
        print(
            f"message_{index:03d} direction={item.direction} sender={item.sender or '-'} "
            f"time={item.time_text or '-'} confidence={item.confidence:.2f} "
            f"raw_count={item.raw_count} rect={item.rect} text={trim(item.text, 220)}"
        )


def print_visible_message_item(item: QQVisibleMessage) -> None:
    if item.content_type == "text":
        raw_count = item.raw_metadata.get("raw_count", 0)
        print(
            f"{item.index:03d} content_type=text direction={item.direction or '-'} "
            f"sender={item.sender or '-'} time={item.time_text or '-'} "
            f"confidence={item.confidence:.2f} raw_count={raw_count} "
            f"rect={item.rect or '-'} text={trim(item.text, 220)}"
        )
        return
    image_count = item.raw_metadata.get("image_count", 0)
    print(
        f"{item.index:03d} content_type={item.content_type or '-'} direction={item.direction or '-'} "
        f"platform_msg_id={item.platform_msg_id or '-'} confidence={item.confidence:.2f} "
        f"rect={item.rect or '-'} media_rect={item.media_rect or '-'} "
        f"images={image_count} file={trim(item.file_name, 120) or '-'} "
        f"size={item.file_size or '-'} text={trim(item.text, 220) or '-'} "
        f"evidence={item.evidence_ref or '-'}"
    )


def merged_structured_items(text_messages: list[object], media_messages: list[object]) -> list[dict[str, object]]:
    media_rects = [parse_rect(getattr(item, "rect", "")) for item in media_messages]
    media_rects = [rect for rect in media_rects if rect is not None]
    items: list[dict[str, object]] = []
    for message in text_messages:
        rect = parse_rect(getattr(message, "rect", ""))
        if rect is not None and any(rects_overlap(rect, media_rect) for media_rect in media_rects):
            continue
        items.append(
            {
                "content_type": "text",
                "direction": getattr(message, "direction", ""),
                "sender": getattr(message, "sender", ""),
                "time_text": getattr(message, "time_text", ""),
                "text": getattr(message, "text", ""),
                "rect": getattr(message, "rect", ""),
                "confidence": getattr(message, "confidence", 0.0),
                "raw_count": getattr(message, "raw_count", 0),
            }
        )
    for message in media_messages:
        items.append(
            {
                "content_type": getattr(message, "content_type", ""),
                "direction": getattr(message, "direction", ""),
                "sender": "",
                "time_text": "",
                "text": getattr(message, "text", ""),
                "rect": getattr(message, "rect", ""),
                "media_rect": getattr(message, "media_rect", ""),
                "confidence": getattr(message, "confidence", 0.0),
                "platform_msg_id": getattr(message, "platform_msg_id", ""),
                "file_name": getattr(message, "file_name", ""),
                "file_size": getattr(message, "file_size", ""),
                "image_count": getattr(message, "image_count", 0),
                "content_image_path": getattr(message, "content_image_path", ""),
                "evidence_ref": getattr(message, "evidence_ref", ""),
            }
        )
    return sorted(items, key=combined_message_sort_key)


def print_combined_message_item(index: int, item: dict[str, object]) -> None:
    content_type = str(item.get("content_type") or "-")
    if content_type == "text":
        print(
            f"{index:03d} content_type=text direction={item.get('direction') or '-'} "
            f"sender={item.get('sender') or '-'} time={item.get('time_text') or '-'} "
            f"confidence={float(item.get('confidence') or 0):.2f} raw_count={item.get('raw_count') or 0} "
            f"rect={item.get('rect') or '-'} text={trim(item.get('text'), 220)}"
        )
        return
    print(
        f"{index:03d} content_type={content_type} direction={item.get('direction') or '-'} "
        f"platform_msg_id={item.get('platform_msg_id') or '-'} confidence={float(item.get('confidence') or 0):.2f} "
        f"rect={item.get('rect') or '-'} media_rect={item.get('media_rect') or '-'} "
        f"images={item.get('image_count') or 0} file={trim(item.get('file_name'), 120) or '-'} "
        f"size={item.get('file_size') or '-'} text={trim(item.get('text'), 220) or '-'} "
        f"evidence={item.get('evidence_ref') or '-'}"
    )


def combined_message_json(index: int, item: dict[str, object]) -> dict[str, object]:
    content_type = str(item.get("content_type") or "")
    payload: dict[str, object] = {
        "index": index,
        "content_type": content_type,
        "direction": item.get("direction") or "",
        "sender": item.get("sender") or "",
        "time_text": item.get("time_text") or "",
        "text": item.get("text") or "",
        "rect": item.get("rect") or "",
        "confidence": float(item.get("confidence") or 0),
    }
    if content_type != "text":
        payload.update(
            {
                "platform_msg_id": item.get("platform_msg_id") or "",
                "file_name": item.get("file_name") or "",
                "file_size": item.get("file_size") or "",
                "media_rect": item.get("media_rect") or "",
                "image_count": int(item.get("image_count") or 0),
                "content_image_path": item.get("content_image_path") or "",
                "evidence_ref": item.get("evidence_ref") or "",
            }
        )
    else:
        payload["raw_count"] = int(item.get("raw_count") or 0)
    return payload


def structured_text_message_json(index: int, item: object) -> dict[str, object]:
    return {
        "index": index,
        "content_type": "text",
        "direction": getattr(item, "direction", "") or "",
        "sender": getattr(item, "sender", "") or "",
        "time_text": getattr(item, "time_text", "") or "",
        "text": getattr(item, "text", "") or "",
        "rect": getattr(item, "rect", "") or "",
        "confidence": float(getattr(item, "confidence", 0) or 0),
        "raw_count": int(getattr(item, "raw_count", 0) or 0),
    }


def combined_message_sort_key(item: dict[str, object]) -> tuple[int, int, int, str]:
    rect = parse_rect(str(item.get("rect") or ""))
    if rect is None:
        return (10**9, 10**9, 10**9, str(item.get("text") or ""))
    left, top, _right, _bottom = rect
    return (top // 8, top, left, str(item.get("text") or ""))


def rects_overlap(
    left: tuple[int, int, int, int],
    right: tuple[int, int, int, int],
    *,
    margin: int = 2,
) -> bool:
    return not (
        left[2] < right[0] - margin
        or left[0] > right[2] + margin
        or left[3] < right[1] - margin
        or left[1] > right[3] + margin
    )


def print_media_layout_item(item: object) -> None:
    print(
        f"{getattr(item, 'index', 0):03d} score={getattr(item, 'score', 0)} "
        f"reason={getattr(item, 'reason', '-') or '-'} depth={getattr(item, 'depth', 0)} "
        f"path={getattr(item, 'path', '-') or '-'} type={getattr(item, 'control_type', '') or '-'} "
        f"class={getattr(item, 'class_name', '') or '-'} aid={getattr(item, 'automation_id', '') or '-'} "
        f"rect={getattr(item, 'rect', '') or '-'} children={getattr(item, 'child_count', 0)} "
        f"patterns={getattr(item, 'patterns', '') or '-'} name={trim(getattr(item, 'name', ''), 100) or '-'} "
        f"parent_type={getattr(item, 'parent_type', '') or '-'} parent_rect={getattr(item, 'parent_rect', '') or '-'} "
        f"nearby={trim(getattr(item, 'nearby_text', ''), 180) or '-'}"
    )


def print_media_message_item(item: object) -> None:
    metadata = getattr(item, "metadata", {}) or {}
    raw_texts = metadata.get("raw_texts", []) if isinstance(metadata, dict) else []
    print(
        f"{getattr(item, 'index', 0):03d} type={getattr(item, 'content_type', '') or '-'} "
        f"direction={getattr(item, 'direction', '') or '-'} confidence={getattr(item, 'confidence', 0):.2f} "
        f"platform_msg_id={getattr(item, 'platform_msg_id', '') or '-'} "
        f"rect={getattr(item, 'rect', '') or '-'} media_rect={getattr(item, 'media_rect', '') or '-'} "
        f"images={getattr(item, 'image_count', 0)} file={trim(getattr(item, 'file_name', ''), 120) or '-'} "
        f"size={getattr(item, 'file_size', '') or '-'} text={trim(getattr(item, 'text', ''), 160) or '-'} "
        f"evidence={getattr(item, 'evidence_ref', '') or '-'} "
        f"raw_texts={trim(' | '.join(str(value) for value in raw_texts), 220) or '-'}"
    )


def collect_tree_rows(root: object, max_depth: int, max_nodes: int) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    stack: list[tuple[int, object, int | None, str]] = [(0, root, None, "0")]
    while stack and len(rows) < max_nodes:
        depth, control, parent_index, path = stack.pop()
        row_index = len(rows)
        row = summarize_control(depth, control).as_dict()
        row["index"] = row_index
        row["parent_index"] = parent_index
        row["path"] = path
        rows.append(row)
        if depth >= max_depth:
            continue
        try:
            children = list(control.GetChildren())
        except Exception:
            continue
        for child_offset, child in reversed(list(enumerate(children))):
            stack.append((depth + 1, child, row_index, f"{path}/{child_offset}"))
    return rows


def has_visible_rect(value: str) -> bool:
    rect = parse_rect(value)
    if rect is None:
        return False
    left, top, right, bottom = rect
    return right > left and bottom > top and (left, top, right, bottom) != (0, 0, 0, 0)


def parse_rect(value: str) -> tuple[int, int, int, int] | None:
    try:
        left, top, right, bottom = [int(part) for part in value.strip("()").split(",")]
    except ValueError:
        return None
    return left, top, right, bottom


def visual_sort_key(item: object) -> tuple[int, int, int, str]:
    rect = parse_rect(getattr(item, "rect", ""))
    if rect is None:
        return (10**9, 10**9, 10**9, getattr(item, "text", ""))
    left, top, _right, bottom = rect
    # Small y differences in wrapped/inline text belong to the same visual band.
    row = top // 8
    return (row, top, left, getattr(item, "text", ""))


def looks_like_message_content(value: str) -> bool:
    text = value.strip()
    if not text:
        return False
    if text in {"消息列表", "表情", "true", "false"}:
        return False
    if text.lower().startswith(("http://", "https://")):
        return False
    if text.isdigit():
        return False
    import re

    if re.fullmatch(r"\d{1,4}([/:.-]\d{1,2}){1,2}(\s+\d{1,2}:\d{2}(:\d{2})?)?", text):
        return False
    if re.fullmatch(r"(星期|周)[一二三四五六日天](\s+\d{1,2}:\d{2})?", text):
        return False
    if re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", text):
        return False
    return True


if __name__ == "__main__":
    raise SystemExit(main())
