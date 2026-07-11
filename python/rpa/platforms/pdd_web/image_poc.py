from __future__ import annotations

import base64
import hashlib
import mimetypes
import re
import time
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[4]
DEFAULT_OUTPUT_DIR = REPO_ROOT / ".codex_tmp" / "pdd_web_image_poc"
DEFAULT_MEDIA_DIR = REPO_ROOT / "python" / "rpa" / "_media" / "pdd_web" / "image"


PDD_IMAGE_EXTRACTOR_JS = r"""
() => {
  const MESSAGE_SELECTORS = [
    "li[id^='middlePanel_List_']",
    ".msg-list li.onemsg",
    "div[id*='cs-common-message-list-item']",
    "[class*='message-list'] [class*='item']",
    "[class*='MessageList'] [class*='item']",
    "[class*='LayoutDefaultWrapper']"
  ];
  const FALLBACK_MESSAGE_SELECTORS = [".msg-list li", "[data-pdd-message]"];

  function textOf(node) {
    return (node && (node.innerText || node.textContent) || "").trim();
  }

  function classNameOf(node) {
    const value = node && node.className;
    if (!value) return "";
    return typeof value === "string" ? value : String(value.baseVal || value);
  }

  function firstNonEmptyNodes(selectors, root = document) {
    for (const selector of selectors) {
      let nodes = [];
      try {
        nodes = Array.from(root.querySelectorAll(selector));
      } catch (_error) {
        nodes = [];
      }
      if (nodes.length) return nodes;
    }
    return [];
  }

  function isLikelyMessageNode(node) {
    if (!node) return false;
    if (node.matches("li[id^='middlePanel_List_'],div[id*='cs-common-message-list-item'],[class*='LayoutDefaultWrapper'],[data-pdd-message]")) {
      return true;
    }
    return Boolean(node.querySelector("img,[class*='image-msg'],[class*='ImageMessage'],[style*='background-image']"));
  }

  function isAgentMessageNode(node) {
    const className = classNameOf(node).toLowerCase();
    if (/(^|\s)(right|self|mine|me|send|sent|seller|service|agent)(\s|$)|is[-_]?mine/.test(className)) {
      return true;
    }
    if (node.querySelector(".kwaishop-cs-LayoutDefaultWrapper__isMine,.cs-item,[class*='isMine']")) {
      return true;
    }
    if (node.getAttribute("data-sender-role") === "agent") {
      return true;
    }
    return false;
  }

  function backgroundUrl(node) {
    const value = node ? getComputedStyle(node).backgroundImage || "" : "";
    const match = value.match(/url\((['"]?)(.*?)\1\)/i);
    return match ? match[2] : "";
  }

  function sourceKind(url) {
    if (!url) return "none";
    if (url.startsWith("data:")) return "data_url";
    if (url.startsWith("blob:")) return "blob_url";
    if (/^https?:\/\//i.test(url)) return "remote_url";
    return "other_url";
  }

  function rectOf(node) {
    const rect = node.getBoundingClientRect();
    return {
      x: rect.x,
      y: rect.y,
      width: rect.width,
      height: rect.height
    };
  }

  function imageTargets(messageNode) {
    const targets = [];
    const explicit = Array.from(messageNode.querySelectorAll([
      ".image-msg img",
      "[class*='image-msg'] img",
      "[class*='ImageMessage'] img",
      "img"
    ].join(",")));
    for (const image of explicit) {
      targets.push({
        element: image,
        asset_url: image.currentSrc || image.src || image.getAttribute("data-src") || image.getAttribute("data-original") || ""
      });
    }
    const backgroundNodes = Array.from(messageNode.querySelectorAll("[style*='background-image'],[class*='image-msg'],[class*='ImageMessage']"));
    for (const node of backgroundNodes) {
      if (explicit.includes(node)) continue;
      const url = backgroundUrl(node);
      if (url || !targets.length) {
        targets.push({ element: node, asset_url: url });
      }
    }
    return targets;
  }

  let nodes = firstNonEmptyNodes(MESSAGE_SELECTORS);
  if (!nodes.length) {
    nodes = firstNonEmptyNodes(FALLBACK_MESSAGE_SELECTORS).filter(isLikelyMessageNode);
  }
  nodes = nodes.filter(isLikelyMessageNode);

  const candidates = [];
  nodes.forEach((messageNode, messageIndex) => {
    const senderRole = isAgentMessageNode(messageNode) ? "agent" : "customer";
    const targets = imageTargets(messageNode);
    targets.forEach((target, imageIndex) => {
      const element = target.element;
      const candidateId = `pdd-image-poc-${messageIndex}-${imageIndex}-${Date.now()}`;
      element.setAttribute("data-yy-pdd-image-poc-id", candidateId);
      candidates.push({
        candidate_id: candidateId,
        message_index: messageIndex,
        image_index: imageIndex,
        sender_role: senderRole,
        content_type: "image",
        content: target.asset_url ? "[image]" : textOf(messageNode),
        asset_url: target.asset_url,
        source_kind: sourceKind(target.asset_url),
        message_text: textOf(messageNode).replace(/\s+/g, " ").slice(0, 500),
        message_id: messageNode.id || "",
        message_class_name: classNameOf(messageNode).slice(0, 240),
        element_class_name: classNameOf(element).slice(0, 240),
        rect: rectOf(element)
      });
    });
  });
  return candidates;
}
"""


FETCH_AS_DATA_URL_JS = r"""
async (url) => {
  if (!url) {
    throw new Error("empty_url");
  }
  if (url.startsWith("data:")) {
    return url;
  }
  const response = await fetch(url, { credentials: "include", cache: "force-cache" });
  if (!response.ok) {
    throw new Error(`fetch_failed:${response.status}`);
  }
  const blob = await response.blob();
  return await new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result || ""));
    reader.onerror = () => reject(reader.error || new Error("file_reader_failed"));
    reader.readAsDataURL(blob);
  });
}
"""


def ensure_dir(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def extension_from_mime(mime_type: str) -> str:
    mime_type = (mime_type or "").split(";", 1)[0].strip().lower()
    if mime_type == "image/jpeg":
        return ".jpg"
    if mime_type == "image/png":
        return ".png"
    guessed = mimetypes.guess_extension(mime_type)
    return guessed or ".bin"


def save_data_url(data_url: str, output_dir: Path, *, prefix: str = "pdd_web_image") -> dict[str, Any]:
    match = re.match(r"^data:([^;,]+)?(?:;[^,]*)?;base64,(.*)$", data_url, re.DOTALL)
    if not match:
        raise ValueError("invalid_data_url")
    mime_type = match.group(1) or "application/octet-stream"
    raw = base64.b64decode(match.group(2), validate=False)
    digest = hashlib.sha1(raw).hexdigest()[:16]
    ext = extension_from_mime(mime_type)
    output_path = ensure_dir(output_dir) / f"{prefix}_{digest}{ext}"
    output_path.write_bytes(raw)
    return {
        "path": str(output_path),
        "mime_type": mime_type,
        "bytes": len(raw),
        "sha1": hashlib.sha1(raw).hexdigest(),
        "method": "fetch_data_url",
    }


def latest_customer_image_candidate(candidates: list[dict[str, Any]]) -> dict[str, Any] | None:
    for candidate in reversed(candidates):
        if str(candidate.get("sender_role") or "").lower() == "customer":
            return candidate
    return candidates[-1] if candidates else None


def _valid_clip(rect: dict[str, Any]) -> bool:
    try:
        return float(rect.get("width") or 0) > 1 and float(rect.get("height") or 0) > 1
    except (TypeError, ValueError):
        return False


def capture_latest_customer_image(
    page: Any,
    *,
    output_dir: Path = DEFAULT_MEDIA_DIR,
    save_all_candidates: bool = False,
) -> dict[str, Any]:
    candidates = page.evaluate(PDD_IMAGE_EXTRACTOR_JS)
    if not isinstance(candidates, list):
        candidates = []
    selected = latest_customer_image_candidate(candidates)
    result: dict[str, Any] = {
        "status": "not_found",
        "candidate_count": len(candidates),
        "selected": selected or {},
        "saved": [],
        "errors": [],
    }
    if not selected:
        return result

    selected_items = candidates if save_all_candidates else [selected]
    saved: list[dict[str, Any]] = []
    errors: list[str] = []
    for index, candidate in enumerate(selected_items):
        prefix = f"pdd_web_image_{int(time.time() * 1000)}_{index + 1}"
        asset_url = str(candidate.get("asset_url") or "")
        if asset_url:
            try:
                data_url = page.evaluate(FETCH_AS_DATA_URL_JS, asset_url)
                saved_item = save_data_url(str(data_url), output_dir, prefix=prefix)
                saved_item.update(
                    {
                        "candidate_id": candidate.get("candidate_id", ""),
                        "source_kind": candidate.get("source_kind", ""),
                        "asset_url": asset_url[:500],
                    }
                )
                saved.append(saved_item)
                continue
            except Exception as exc:  # noqa: BLE001 - PoC should preserve all failure detail.
                errors.append(f"fetch_failed:{candidate.get('candidate_id', '')}:{exc}")

        candidate_id = str(candidate.get("candidate_id") or "")
        rect = candidate.get("rect") if isinstance(candidate.get("rect"), dict) else {}
        if candidate_id and _valid_clip(rect):
            try:
                screenshot_path = ensure_dir(output_dir) / f"{prefix}_screenshot.png"
                locator = page.locator(f'[data-yy-pdd-image-poc-id="{candidate_id}"]').first
                locator.screenshot(path=str(screenshot_path))
                saved.append(
                    {
                        "path": str(screenshot_path),
                        "mime_type": "image/png",
                        "bytes": screenshot_path.stat().st_size,
                        "method": "element_screenshot",
                        "candidate_id": candidate_id,
                        "source_kind": candidate.get("source_kind", ""),
                        "asset_url": asset_url[:500],
                    }
                )
            except Exception as exc:  # noqa: BLE001
                errors.append(f"screenshot_failed:{candidate_id}:{exc}")

    result["saved"] = saved
    result["errors"] = errors
    result["status"] = "success" if saved else "failed"
    return result


TINY_RED_PNG_DATA_URL = (
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADUlEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
)


def build_fixture_html(kind: str) -> str:
    image_markup = {
        "data": f'<img alt="customer-image" src="{TINY_RED_PNG_DATA_URL}" />',
        "background": (
            f'<div class="image-msg background-image" '
            f'style="width:96px;height:64px;background-image:url({TINY_RED_PNG_DATA_URL});background-size:cover"></div>'
        ),
        "screenshot": (
            '<div class="image-msg screenshot-only" '
            'style="width:120px;height:72px;background:linear-gradient(135deg,#24a148,#f1c21b);'
            'border:2px solid #333"></div>'
        ),
        "blob": '<img id="blob-image" alt="customer-blob-image" />',
    }.get(kind)
    if image_markup is None:
        raise ValueError(f"unsupported_fixture:{kind}")

    blob_script = ""
    if kind == "blob":
        blob_script = f"""
        <script>
          (async () => {{
            const response = await fetch("{TINY_RED_PNG_DATA_URL}");
            const blob = await response.blob();
            document.getElementById("blob-image").src = URL.createObjectURL(blob);
          }})();
        </script>
        """

    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>PDD image PoC fixture</title>
  <style>
    body {{ font-family: sans-serif; padding: 24px; background: #f5f5f5; }}
    .msg-list {{ list-style: none; padding: 0; width: 520px; }}
    .onemsg {{ margin: 12px 0; padding: 10px; border-radius: 8px; background: white; }}
    .onemsg.right {{ margin-left: 120px; background: #e8f3ff; }}
    .image-msg img, .onemsg img {{ width: 96px; height: 64px; image-rendering: pixelated; }}
  </style>
</head>
<body>
  <ul class="msg-list">
    <li id="middlePanel_List_agent_1" class="onemsg right isMine" data-pdd-message="1">您好，有什么可以帮您？</li>
    <li id="middlePanel_List_customer_2" class="onemsg left" data-pdd-message="1">
      <div>客户发来图片</div>
      <div class="image-msg">{image_markup}</div>
    </li>
  </ul>
  {blob_script}
</body>
</html>
"""
