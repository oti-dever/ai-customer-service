from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")


@dataclass(frozen=True)
class TreeNode:
    index: int
    parent_index: int | None
    path: str
    depth: int
    control_type: str
    class_name: str
    automation_id: str
    name: str
    rect: tuple[int, int, int, int] | None
    child_count: int


@dataclass(frozen=True)
class ConversationListItem:
    node: TreeNode
    title: str
    time_text: str
    preview: str


@dataclass(frozen=True)
class ChatMessageItem:
    node: TreeNode
    direction: str
    sender: str
    time_text: str
    text: str


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze a dumped QQ UIA JSONL tree")
    parser.add_argument("path", help="path to qq_uia_tree.jsonl")
    parser.add_argument("--limit", type=int, default=80)
    args = parser.parse_args()

    nodes = load_nodes(Path(args.path))
    if not nodes:
        print("no nodes")
        return 1

    print(f"nodes={len(nodes)}")
    has_parent_meta = any(node.parent_index is not None or node.path for node in nodes)
    print(f"parent_metadata={'yes' if has_parent_meta else 'no'}")
    print("named controls:")
    for node in [item for item in nodes if item.name][: args.limit]:
        print(format_node(node))

    print("message-like controls:")
    for node in [item for item in nodes if is_message_like(item)][: args.limit]:
        print(format_node(node))

    print("input-like controls:")
    for node in [item for item in nodes if is_input_like(item)][: args.limit]:
        print(format_node(node))

    conversation_items = conversation_list_items(nodes)
    if conversation_items:
        print("conversation list items:")
        for item in conversation_items[: args.limit]:
            print(
                f"  #{item.node.index:03d} title={item.title or '-'} "
                f"time={item.time_text or '-'} preview={item.preview or '-'}"
            )

    message_root = best_message_root(nodes)
    print("inferred layout:")
    if message_root:
        print(f"  message_root: {format_node(message_root)}")
        title = best_title(nodes, message_root)
        compose = best_compose_area(nodes, message_root)
        if title:
            print(f"  title: {format_node(title)}")
        if compose:
            print(f"  compose_area: {format_node(compose)}")
        print("chat text controls:")
        for node in chat_text_controls(nodes, message_root)[: args.limit]:
            print(f"  {format_node(node)}")
        chat_items = chat_message_items(nodes, message_root)
        if chat_items:
            print("chat message groups:")
            for item in chat_items[: args.limit]:
                print(
                    f"  #{item.node.index:03d} direction={item.direction} sender={item.sender or '-'} "
                    f"time={item.time_text or '-'} text={item.text or '-'}"
                )
    else:
        print("  message_root: not found")

    return 0


def load_nodes(path: Path) -> list[TreeNode]:
    nodes: list[TreeNode] = []
    with path.open("r", encoding="utf-8") as file:
        for index, line in enumerate(file):
            line = line.strip()
            if not line:
                continue
            raw = json.loads(line)
            nodes.append(
                TreeNode(
                    index=index,
                    parent_index=_optional_int(raw.get("parent_index")),
                    path=str(raw.get("path") or ""),
                    depth=int(raw.get("depth") or 0),
                    control_type=str(raw.get("control_type") or ""),
                    class_name=str(raw.get("class_name") or ""),
                    automation_id=str(raw.get("automation_id") or ""),
                    name=str(raw.get("name") or ""),
                    rect=parse_rect(raw.get("rect")),
                    child_count=int(raw.get("child_count") or 0),
                )
            )
    return nodes


def parse_rect(value: Any) -> tuple[int, int, int, int] | None:
    match = re.fullmatch(r"\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)", str(value or ""))
    if not match:
        return None
    return tuple(int(part) for part in match.groups())  # type: ignore[return-value]


def _optional_int(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def is_message_like(node: TreeNode) -> bool:
    text = " ".join([node.control_type, node.class_name, node.automation_id, node.name]).lower()
    return any(value in text for value in ["message", "msg", "聊天", "消息", "ml-root"])


def is_input_like(node: TreeNode) -> bool:
    text = " ".join([node.control_type, node.class_name, node.automation_id, node.name]).lower()
    return any(value in text for value in ["input", "edit", "textarea", "textbox", "输入", "编辑", "回复"])


def best_message_root(nodes: list[TreeNode]) -> TreeNode | None:
    candidates = [node for node in nodes if node.name == "消息列表" or node.automation_id == "ml-root"]
    candidates = [node for node in candidates if node.rect]
    if not candidates:
        return None
    return max(candidates, key=lambda node: rect_area(node.rect))


def best_title(nodes: list[TreeNode], message_root: TreeNode) -> TreeNode | None:
    if not message_root.rect:
        return None
    left, top, right, _bottom = message_root.rect
    candidates: list[tuple[int, TreeNode]] = []
    for node in nodes:
        if not node.rect or not looks_like_title(node.name):
            continue
        n_left, n_top, n_right, n_bottom = node.rect
        if n_top < top - 90 or n_bottom > top - 4:
            continue
        if n_left < left - 8 or n_left > left + 320:
            continue
        score = 100 - abs(n_left - left)
        if node.control_type in {"TextControl", "ButtonControl"}:
            score += 30
        candidates.append((score, node))
    return max(candidates, key=lambda item: item[0])[1] if candidates else None


def best_compose_area(nodes: list[TreeNode], message_root: TreeNode) -> TreeNode | None:
    if not message_root.rect:
        return None
    msg_left, _msg_top, msg_right, msg_bottom = message_root.rect
    candidates: list[tuple[int, TreeNode]] = []
    for node in nodes:
        if not node.rect:
            continue
        left, top, right, bottom = node.rect
        width = right - left
        height = bottom - top
        if left < msg_left - 8 or right > msg_right + 8:
            continue
        if top < msg_bottom or height < 40:
            continue
        if node.control_type in {"ButtonControl", "ToolBarControl"}:
            continue
        if node.name:
            continue
        gap = top - msg_bottom
        score = width + height
        if 24 <= gap <= 75 and 60 <= height <= 160:
            score += 400
        if top <= msg_bottom + 80:
            score += 80
        if gap < 18:
            score -= 160
        if height > 175:
            score -= 180
        if bottom > msg_bottom + 175:
            score -= 100
        if node.control_type in {"GroupControl", "PaneControl"}:
            score += 60
        candidates.append((score, node))
    return max(candidates, key=lambda item: item[0])[1] if candidates else None


def chat_text_controls(nodes: list[TreeNode], message_root: TreeNode) -> list[TreeNode]:
    if not message_root.rect:
        return []
    if any(node.parent_index is not None for node in nodes):
        descendants = descendant_indexes(nodes, message_root.index)
        candidates = [node for node in nodes if node.index in descendants]
    else:
        candidates = [node for node in nodes if rect_inside(node.rect, message_root.rect)]
    candidates = [
        node
        for node in candidates
        if node.control_type == "TextControl" and node.name and not looks_like_chrome_text(node.name)
    ]
    return sorted(candidates, key=lambda node: (node.rect[1] if node.rect else 0, node.rect[0] if node.rect else 0))


def conversation_list_items(nodes: list[TreeNode]) -> list[ConversationListItem]:
    root = next((node for node in nodes if node.name == "会话列表"), None)
    if root is None:
        return []
    children_by_parent = build_children_by_parent(nodes)
    items: list[ConversationListItem] = []
    for child_index in children_by_parent.get(root.index, []):
        child = nodes[child_index]
        text_nodes = sorted(
            [
                node
                for node in descendant_nodes(nodes, child.index, children_by_parent)
                if node.control_type == "TextControl" and node.name.strip()
            ],
            key=lambda node: (node.rect[1] if node.rect else 0, node.rect[0] if node.rect else 0),
        )
        if not text_nodes:
            continue
        time_nodes = [node for node in text_nodes if looks_like_time_or_date(node.name)]
        title_node = next((node for node in text_nodes if node not in time_nodes), text_nodes[0])
        preview_nodes = [node for node in text_nodes if node not in {title_node, *time_nodes}]
        items.append(
            ConversationListItem(
                node=child,
                title=clean_text(title_node.name),
                time_text=clean_text(time_nodes[0].name) if time_nodes else "",
                preview=" ".join(clean_text(node.name) for node in preview_nodes),
            )
        )
    return items


def chat_message_items(nodes: list[TreeNode], message_root: TreeNode) -> list[ChatMessageItem]:
    if not message_root.rect or not any(node.parent_index is not None for node in nodes):
        return []
    children_by_parent = build_children_by_parent(nodes)
    descendants = descendant_nodes(nodes, message_root.index, children_by_parent)
    message_nodes = [
        node
        for node in descendants
        if node.control_type == "GroupControl"
        and node.rect
        and re.fullmatch(r"\d{10,}", node.automation_id or "")
    ]
    items: list[ChatMessageItem] = []
    for node in sorted(message_nodes, key=lambda item: (item.rect[1] if item.rect else 0, item.index)):
        text_nodes = sorted(
            [
                child
                for child in descendant_nodes(nodes, node.index, children_by_parent)
                if child.control_type == "TextControl" and child.name.strip() and not looks_like_chrome_text(child.name)
            ],
            key=lambda item: (item.rect[1] if item.rect else 0, item.rect[0] if item.rect else 0),
        )
        if not text_nodes:
            continue
        time_nodes = [child for child in text_nodes if looks_like_time_or_date(child.name)]
        body_nodes = [child for child in text_nodes if child not in time_nodes]
        sender_nodes = [
            child
            for child in descendant_nodes(nodes, node.index, children_by_parent)
            if child.control_type == "GroupControl" and child.name.strip()
        ]
        text = " ".join(clean_text(child.name) for child in body_nodes)
        direction = infer_message_direction(body_nodes or text_nodes, message_root.rect)
        items.append(
            ChatMessageItem(
                node=node,
                direction=direction,
                sender=clean_text(sender_nodes[0].name) if sender_nodes else "",
                time_text=clean_text(time_nodes[0].name) if time_nodes else "",
                text=text,
            )
        )
    return items


def build_children_by_parent(nodes: list[TreeNode]) -> dict[int, list[int]]:
    children_by_parent: dict[int, list[int]] = {}
    for node in nodes:
        if node.parent_index is not None:
            children_by_parent.setdefault(node.parent_index, []).append(node.index)
    return children_by_parent


def descendant_nodes(
    nodes: list[TreeNode],
    root_index: int,
    children_by_parent: dict[int, list[int]] | None = None,
) -> list[TreeNode]:
    by_index = {node.index: node for node in nodes}
    children = children_by_parent or build_children_by_parent(nodes)
    found: list[TreeNode] = []
    stack = list(reversed(children.get(root_index, [])))
    while stack:
        index = stack.pop()
        node = by_index.get(index)
        if node is None:
            continue
        found.append(node)
        stack.extend(reversed(children.get(index, [])))
    return found


def descendant_indexes(nodes: list[TreeNode], root_index: int) -> set[int]:
    children_by_parent = build_children_by_parent(nodes)
    found: set[int] = set()
    stack = list(children_by_parent.get(root_index, []))
    while stack:
        index = stack.pop()
        if index in found:
            continue
        found.add(index)
        stack.extend(children_by_parent.get(index, []))
    return found


def rect_inside(
    inner: tuple[int, int, int, int] | None,
    outer: tuple[int, int, int, int],
) -> bool:
    if not inner:
        return False
    return inner[0] >= outer[0] and inner[1] >= outer[1] and inner[2] <= outer[2] and inner[3] <= outer[3]


def looks_like_chrome_text(value: str) -> bool:
    normalized = value.strip()
    if not normalized:
        return True
    return normalized in {"消息列表", "会话"}


def looks_like_time_or_date(value: str) -> bool:
    text = clean_text(value)
    return bool(
        re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", text)
        or re.fullmatch(r"\d{1,2}/\d{1,2}", text)
        or text in {"昨天", "今天", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日", "周一", "周二", "周三", "周四", "周五", "周六", "周日"}
    )


def infer_message_direction(text_nodes: list[TreeNode], message_rect: tuple[int, int, int, int]) -> str:
    centers = [rect_center_x(node.rect) for node in text_nodes if node.rect]
    if not centers:
        return "unknown"
    midpoint = (message_rect[0] + message_rect[2]) / 2.0
    return "outgoing" if sum(centers) / len(centers) > midpoint else "incoming"


def rect_center_x(rect: tuple[int, int, int, int] | None) -> float:
    if not rect:
        return 0.0
    return (rect[0] + rect[2]) / 2.0


def clean_text(value: str) -> str:
    return re.sub(r"\s+", " ", value.replace("\x7f", "")).strip()


def rect_area(rect: tuple[int, int, int, int] | None) -> int:
    if not rect:
        return 0
    return max(0, rect[2] - rect[0]) * max(0, rect[3] - rect[1])


def looks_like_title(value: str) -> bool:
    if not value or len(value) > 80:
        return False
    if re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", value):
        return False
    blocked = [
        "QQ",
        "切换",
        "天气",
        "头像",
        "在线状态",
        "个性签名",
        "点击",
        "窗口控制",
        "发起群聊",
        "更多",
        "消息",
        "联系人",
        "空间",
        "邮箱",
        "我的手机",
    ]
    return not any(item in value for item in blocked)


def format_node(node: TreeNode) -> str:
    parent = f" parent={node.parent_index}" if node.parent_index is not None else ""
    path = f" path={node.path}" if node.path else ""
    return (
        f"#{node.index:03d} depth={node.depth} type={node.control_type or '-'} "
        f"class={node.class_name or '-'} aid={node.automation_id or '-'} "
        f"name={node.name or '-'} rect={node.rect or '-'} children={node.child_count}{parent}{path}"
    )


if __name__ == "__main__":
    raise SystemExit(main())
