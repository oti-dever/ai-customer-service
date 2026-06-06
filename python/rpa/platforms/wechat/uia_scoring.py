from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class ControlCandidate:
    control: Any
    depth: int
    score: int
    reason: str


def walk_controls(root: Any, *, max_depth: int = 12, max_nodes: int = 2500) -> list[tuple[int, Any]]:
    found: list[tuple[int, Any]] = []
    queue: list[tuple[int, Any]] = [(0, root)]
    visited = 0

    while queue and visited < max_nodes:
        depth, control = queue.pop(0)
        visited += 1
        found.append((depth, control))
        if depth >= max_depth:
            continue
        try:
            children = control.GetChildren()
        except Exception:
            continue
        for child in children:
            queue.append((depth + 1, child))

    return found


def safe_prop(control: Any, name: str) -> str:
    try:
        value = getattr(control, name, "")
        return "" if value is None else str(value)
    except Exception:
        return ""


def safe_rect_tuple(control: Any) -> tuple[int, int, int, int] | None:
    if control is None:
        return None
    try:
        rect = control.BoundingRectangle
        return int(rect.left), int(rect.top), int(rect.right), int(rect.bottom)
    except Exception:
        pass
    try:
        left = int(float(getattr(control, "left")))
        top = int(float(getattr(control, "top")))
        right = int(float(getattr(control, "right")))
        bottom = int(float(getattr(control, "bottom")))
    except Exception:
        return None
    if right <= left or bottom <= top:
        return None
    return left, top, right, bottom


def safe_rect_repr(control: Any) -> str:
    rect = safe_rect_tuple(control)
    return "-" if rect is None else f"({rect[0]},{rect[1]},{rect[2]},{rect[3]})"


def control_exists(control: Any) -> bool:
    try:
        return bool(control and control.Exists(0, 0))
    except Exception:
        return False


def normalize_text(value: Any) -> str:
    return " ".join(str(value or "").split()).strip()


def find_descendant_by_automation_id(root: Any, automation_id: str, *, max_depth: int = 14, max_nodes: int = 2500) -> Any | None:
    for _depth, control in walk_controls(root, max_depth=max_depth, max_nodes=max_nodes):
        if safe_prop(control, "AutomationId") == automation_id:
            return control
    return None


def find_session_list_candidates(window_control: Any) -> list[ControlCandidate]:
    candidates: list[ControlCandidate] = []
    for depth, control in walk_controls(window_control, max_depth=10, max_nodes=1200):
        score, reason = _score_session_list_candidate(control)
        if score > 0:
            candidates.append(ControlCandidate(control=control, depth=depth, score=score, reason=reason))
    return sorted(candidates, key=lambda item: item.score, reverse=True)


def find_session_candidate(window_control: Any, contact_name: str, *, exact: bool = False) -> Any | None:
    target = normalize_text(contact_name)
    if not target:
        return None
    session_candidates = find_session_list_candidates(window_control)
    roots = [item.control for item in session_candidates if item.score >= 60]
    if not roots:
        roots = [window_control]

    scored: list[tuple[tuple[int, int, int, int], Any]] = []
    for root in roots:
        for _depth, control in walk_controls(root, max_depth=8, max_nodes=1600):
            name = normalize_text(safe_prop(control, "Name"))
            automation_id = normalize_text(safe_prop(control, "AutomationId"))
            class_name = safe_prop(control, "ClassName")
            display_name = _session_display_name(name, automation_id)
            if not _matches_session(display_name, automation_id, target, exact=exact):
                continue
            rect = safe_rect_tuple(control)
            top = rect[1] if rect is not None else 0
            exact_name = 0 if display_name == target else 1
            exact_aid = 0 if automation_id == "session_item_" + target else 1
            class_rank = 0 if class_name == "mmui::ChatSessionCell" else 1
            scored.append(((class_rank, exact_aid, exact_name, top), control))
    if not scored:
        return None
    scored.sort(key=lambda item: item[0])
    return scored[0][1]


def find_message_list_candidates(window_control: Any) -> list[ControlCandidate]:
    candidates: list[ControlCandidate] = []
    for depth, control in walk_controls(window_control, max_depth=12, max_nodes=1800):
        score, reason = _score_message_list_candidate(control)
        if score > 0:
            candidates.append(ControlCandidate(control=control, depth=depth, score=score, reason=reason))
    return sorted(candidates, key=lambda item: item.score, reverse=True)


def find_input_candidates(window_control: Any) -> list[ControlCandidate]:
    message_list = find_descendant_by_automation_id(window_control, "chat_message_list")
    message_rect = safe_rect_tuple(message_list) if message_list is not None else None
    candidates: list[ControlCandidate] = []
    for depth, control in walk_controls(window_control, max_depth=14, max_nodes=2500):
        score, reason = _score_input_candidate(control, message_rect)
        if score > 0:
            candidates.append(ControlCandidate(control=control, depth=depth, score=score, reason=reason))
    return sorted(candidates, key=lambda item: item.score, reverse=True)


def find_send_button_candidates(window_control: Any) -> list[ControlCandidate]:
    input_field = find_descendant_by_automation_id(window_control, "chat_input_field")
    input_rect = safe_rect_tuple(input_field) if input_field is not None else None
    candidates: list[ControlCandidate] = []
    for depth, control in walk_controls(window_control, max_depth=14, max_nodes=2500):
        score, reason = _score_send_button_candidate(control, input_rect)
        if score > 0:
            candidates.append(ControlCandidate(control=control, depth=depth, score=score, reason=reason))
    return sorted(candidates, key=lambda item: item.score, reverse=True)


def _score_session_list_candidate(control: Any) -> tuple[int, str]:
    automation_id = safe_prop(control, "AutomationId")
    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    class_name = safe_prop(control, "ClassName")
    try:
        children = control.GetChildren()
    except Exception:
        return 0, ""

    child_names = [normalize_text(safe_prop(child, "Name")) for child in children[:40]]
    child_names = [name for name in child_names if name]

    score = 0
    reasons: list[str] = []
    if automation_id == "session_list":
        score += 100
        reasons.append("automationId")
    if "List" in control_type or "Table" in control_type:
        score += 25
        reasons.append("list-type")
    if "RecyclerListView" in class_name or "TableView" in class_name or "ListView" in class_name:
        score += 25
        reasons.append("list-class")
    if child_names:
        score += min(len(child_names), 20)
        reasons.append(f"named-children={len(child_names)}")
    if any("session_item_" in safe_prop(child, "AutomationId") for child in children[:80]):
        score += 70
        reasons.append("session-items")
    return score, ",".join(reasons)


def _session_display_name(name: str, automation_id: str) -> str:
    if automation_id.startswith("session_item_"):
        return normalize_text(automation_id[len("session_item_") :])
    lines = [normalize_text(line) for line in str(name or "").splitlines()]
    lines = [line for line in lines if line]
    return lines[0] if lines else normalize_text(name)


def _matches_session(display_name: str, automation_id: str, target: str, *, exact: bool) -> bool:
    if not display_name and not automation_id:
        return False
    expected_aid = "session_item_" + target
    if exact:
        return display_name == target or automation_id == expected_aid
    return target in display_name or display_name in target or expected_aid in automation_id or target in automation_id


def _score_message_list_candidate(control: Any) -> tuple[int, str]:
    automation_id = safe_prop(control, "AutomationId")
    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    class_name = safe_prop(control, "ClassName")
    try:
        children = control.GetChildren()
    except Exception:
        return 0, ""

    child_names = [normalize_text(safe_prop(child, "Name")) for child in children[:40]]
    score = 0
    reasons: list[str] = []
    if automation_id == "chat_message_list":
        score += 100
        reasons.append("automationId")
    if "List" in control_type:
        score += 20
        reasons.append("list-type")
    if "RecyclerListView" in class_name or "ListView" in class_name:
        score += 30
        reasons.append("list-class")
    if any("chat_bubble_item_view" in safe_prop(child, "AutomationId") for child in children[:80]):
        score += 40
        reasons.append("bubble-children")
    if any("ChatTextItemView" in safe_prop(child, "ClassName") for child in children[:80]):
        score += 35
        reasons.append("text-children")
    if any(child_names):
        score += min(len([name for name in child_names if name]), 20)
        reasons.append("named-children")
    return score, ",".join(reasons)


def _score_input_candidate(control: Any, message_rect: tuple[int, int, int, int] | None) -> tuple[int, str]:
    automation_id = safe_prop(control, "AutomationId")
    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    class_name = safe_prop(control, "ClassName")
    name = normalize_text(safe_prop(control, "Name"))
    rect = safe_rect_tuple(control)

    score = 0
    reasons: list[str] = []
    if automation_id == "chat_input_field":
        score += 120
        reasons.append("automationId")
    if "Edit" in control_type or "Document" in control_type:
        score += 25
        reasons.append("editable-type")
    if "Input" in class_name or "Edit" in class_name or "TextEdit" in class_name:
        score += 20
        reasons.append("input-class")
    if name.lower() in {"input", "message", "chat input"}:
        score += 10
        reasons.append("input-name")
    if rect and message_rect:
        if rect[1] >= message_rect[3] - 20:
            score += 35
            reasons.append("below-message-list")
        if rect[0] <= message_rect[2] and rect[2] >= message_rect[0]:
            score += 15
            reasons.append("right-pane-x")
    return score, ",".join(reasons)


def _score_send_button_candidate(control: Any, input_rect: tuple[int, int, int, int] | None) -> tuple[int, str]:
    name = normalize_text(safe_prop(control, "Name"))
    automation_id = safe_prop(control, "AutomationId")
    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    class_name = safe_prop(control, "ClassName")
    rect = safe_rect_tuple(control)

    lower_name = name.lower()
    score = 0
    reasons: list[str] = []
    if "send" in lower_name or name.startswith("\u53d1") or name.startswith("\u53d1\u9001"):
        score += 120
        reasons.append("send-name")
    if "Button" in control_type:
        score += 25
        reasons.append("button-type")
    if "Button" in class_name or "XButton" in class_name or "XTextView" in class_name:
        score += 15
        reasons.append("button-class")
    if "send" in automation_id.lower():
        score += 20
        reasons.append("send-id")
    if rect and input_rect:
        if rect[1] >= input_rect[1] - 20:
            score += 15
            reasons.append("near-input-y")
        if rect[0] >= input_rect[0]:
            score += 10
            reasons.append("right-of-input")
    return score, ",".join(reasons)
