from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from playwright.sync_api import sync_playwright

from rpa.platforms.pdd_web.image_poc import (
    DEFAULT_MEDIA_DIR,
    build_fixture_html,
    capture_latest_customer_image,
)


COMMON_BROWSER_EXECUTABLES = [
    Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
    Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
]


def _write_fixture(kind: str, base_dir: Path) -> Path:
    fixture_root = base_dir / "fixtures"
    fixture_root.mkdir(parents=True, exist_ok=True)
    fixture_path = fixture_root / f"{kind}.html"
    fixture_path.write_text(build_fixture_html(kind), encoding="utf-8")
    return fixture_path


def _load_page(page, *, url: str, fixture: str, timeout_ms: int, output_dir: Path) -> str:
    if fixture:
        fixture_path = _write_fixture(fixture, output_dir)
        page.goto(fixture_path.as_uri(), wait_until="load", timeout=timeout_ms)
        if fixture == "blob":
            page.wait_for_selector('img[src^="blob:"]', timeout=timeout_ms)
        return fixture_path.as_uri()
    if not url:
        raise ValueError("--url or --fixture is required")
    page.goto(url, wait_until="domcontentloaded", timeout=timeout_ms)
    return url


def _launch_browser(playwright, *, headed: bool, executable_path: str = ""):
    launch_options = {"headless": not headed}
    if executable_path:
        return playwright.chromium.launch(executable_path=str(Path(executable_path).resolve()), **launch_options)
    try:
        return playwright.chromium.launch(**launch_options)
    except Exception as exc:
        missing_browser = "Executable doesn't exist" in str(exc) or "playwright install" in str(exc)
        if not missing_browser:
            raise
        for candidate in COMMON_BROWSER_EXECUTABLES:
            if candidate.exists():
                return playwright.chromium.launch(executable_path=str(candidate), **launch_options)
        raise


def _select_cdp_page(browser, *, url_match: str):
    pages = []
    for context in browser.contexts:
        pages.extend(context.pages)
    if not pages:
        raise RuntimeError("cdp_browser_has_no_pages")
    if url_match:
        for page in pages:
            if url_match in page.url:
                return page
    return pages[-1]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="PDD Web image capture PoC. Extracts the latest customer image from a page and saves it locally."
    )
    parser.add_argument("--url", default="", help="Existing page URL to inspect.")
    parser.add_argument(
        "--cdp-url",
        default="",
        help="Connect to an existing Chrome/Edge remote debugging endpoint, e.g. http://127.0.0.1:9222.",
    )
    parser.add_argument(
        "--url-match",
        default="mms.pinduoduo.com/chat-merchant",
        help="When using --cdp-url, select the first open page whose URL contains this text.",
    )
    parser.add_argument(
        "--fixture",
        default="data",
        choices=["", "data", "blob", "background", "screenshot"],
        help="Run against a generated local fixture instead of --url.",
    )
    parser.add_argument("--output-dir", default=str(DEFAULT_MEDIA_DIR), help="Directory for saved images.")
    parser.add_argument("--headed", action="store_true", help="Run browser headed for visual debugging.")
    parser.add_argument(
        "--browser-executable",
        default="",
        help="Optional Chrome/Edge executable path. If omitted, common local paths are tried when Playwright Chromium is missing.",
    )
    parser.add_argument("--save-all", action="store_true", help="Save all detected image candidates.")
    parser.add_argument("--timeout-ms", type=int, default=10000)
    parser.add_argument("--json-indent", type=int, default=2)
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    with sync_playwright() as playwright:
        browser = (
            playwright.chromium.connect_over_cdp(args.cdp_url)
            if args.cdp_url
            else _launch_browser(playwright, headed=args.headed, executable_path=args.browser_executable)
        )
        try:
            if args.cdp_url:
                page = _select_cdp_page(browser, url_match=args.url_match)
                loaded_url = page.url
            else:
                page = browser.new_page(viewport={"width": 900, "height": 700}, device_scale_factor=1)
                loaded_url = _load_page(
                    page,
                    url=args.url,
                    fixture=args.fixture,
                    timeout_ms=args.timeout_ms,
                    output_dir=output_dir,
                )
            result = capture_latest_customer_image(
                page,
                output_dir=output_dir,
                save_all_candidates=args.save_all,
            )
            result["loaded_url"] = loaded_url
            result["output_dir"] = str(output_dir)
        finally:
            browser.close()

    print(json.dumps(result, ensure_ascii=False, indent=args.json_indent), flush=True)
    return 0 if result.get("status") == "success" else 2


if __name__ == "__main__":
    raise SystemExit(main())
