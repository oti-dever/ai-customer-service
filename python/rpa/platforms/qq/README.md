# QQ PC UIA Probe

This package is a PoC for inspecting an existing QQ PC client window. It is now
registered in `service.rpa_bridge` as an adapter for read-only inspection,
visible unread scheduling, conversation switching, draft fill, and confirmed
text sending.

Run from the repository `python/` directory:

```powershell
python -B -m rpa.platforms.qq.probe windows
python -B -m rpa.platforms.qq.probe summary
python -B -m rpa.platforms.qq.probe tree --output ../logs/qq_uia_tree_deep_with_parent.jsonl
python -B -m rpa.platforms.qq.analyze_tree ../logs/qq_uia_tree_deep_with_parent.jsonl --limit 120
python -B -m rpa.platforms.qq.probe messages --limit 120 --visible-only --visual-order
python -B -m rpa.platforms.qq.probe messages --structured --limit 40
python -B -m rpa.platforms.qq.probe media-layout --limit 120 --output ../logs/qq_media_layout.jsonl
python -B -m rpa.platforms.qq.probe media-messages --limit 40
python -B -m rpa.platforms.qq.probe media-messages --limit 40 --jsonl
python -B -m rpa.platforms.qq.probe media-messages --limit 40 --evidence
python -B -m rpa.platforms.qq.probe media-menu --index 1
python -B -m rpa.platforms.qq.probe conversations --limit 20
python -B -m rpa.platforms.qq.probe unread --limit 20
python -B -m rpa.platforms.qq.probe next-unread --limit 20
python -B -m rpa.platforms.qq.probe next-unread --limit 20 --read
python -B -m rpa.platforms.qq.probe switch --title "慕己"
python -B -m rpa.platforms.qq.probe switch --title "慕己" --uia
python -B -m rpa.platforms.qq.probe switch --title "慕己" --no-uia
python -B -m rpa.platforms.qq.probe switch --title "慕己" --no-activate
python -B -m rpa.platforms.qq.probe draft --text "test draft"
python -B -m rpa.platforms.qq.probe draft --text "test draft" --uia
python -B -m rpa.platforms.qq.probe draft --text "test draft" --no-uia
python -B -m rpa.platforms.qq.probe draft --text "test draft" --no-activate
```

## Current PoC Scope

- Find visible top-level windows owned by `QQ.exe`.
- Score the current QQ chat window and key UIA regions.
- Resolve the current conversation title.
- Dump the UIA tree with `index`, `parent_index`, and `path` metadata.
- Read raw visible text candidates from the current message area.
- Group currently visible chat text into structured messages.
- Probe candidate media controls in the current message area.
- Read structured visible media-message candidates.
- Expose a stable `QQVisibleMessage` model for visible text/media messages.
- Map visible QQ conversations and messages into the normalized RPA bridge event
  shape through `QQSidecarAdapter`.
- Scan visible conversation-list items.
- List visible unread conversation candidates.
- Select the next visible unread conversation with UIA-first switching.
- Switch to a visible conversation by title with post-click title verification.
- Fill the current input area as a draft only.
- Send confirmed text replies through the same safe foreground path.

The `draft` command uses foreground-safe clipboard paste as the main path:
restore QQ, set it as foreground, verify the foreground hwnd, then click the
compose area and paste. `ValuePattern.SetValue` is kept as a diagnostic-only
background attempt through `draft --uia`; current tests show it fails on the QQ
compose area. Draft fill does not press Enter or click the send button.

## Observed QQ Layout

Recent QQ PC builds expose the main window as a `Chrome_WidgetWin_1` window owned
by `QQ.exe`. In the tested layout:

- Current title is a top header button near the right message pane, for example
  `慕己`.
- The right message pane is `WindowControl name=消息列表`.
- The message list root often has `AutomationId=ml-root`.
- The compose text band is the anonymous group directly below the toolbar.
- The left conversation list is a separate `WindowControl name=会话列表`.

This distinction matters: conversation-list previews can contain sender names and
message snippets, but they are not the full current chat history. The structured
message reader should consume only the current `消息列表` subtree.

## Message Reading Modes

`messages` has several modes:

```powershell
python -B -m rpa.platforms.qq.probe messages --limit 120
python -B -m rpa.platforms.qq.probe messages --limit 120 --visible-only
python -B -m rpa.platforms.qq.probe messages --limit 120 --visible-only --visual-order
python -B -m rpa.platforms.qq.probe messages --structured --limit 40
python -B -m rpa.platforms.qq.probe messages --structured --include-media --limit 40
python -B -m rpa.platforms.qq.probe messages --structured --include-media --jsonl --limit 40
python -B -m rpa.platforms.qq.probe messages --structured --include-media --evidence --jsonl --limit 40
```

- Raw mode keeps UIA tree order for debugging.
- `--visible-only` removes zero-rectangle hidden/cache text.
- `--visual-order` sorts raw candidates by screen position.
- `--structured` groups visible candidates into message rows.
- `--structured --include-media` merges visible text and media rows into one
  top-to-bottom output. Media rows include `content_type`, `platform_msg_id`,
  `file_name`, `file_size`, and `media_rect` when available.
- `--jsonl` emits one JSON object per structured message. With
  `--include-media`, the output is pure JSONL without the human-readable header
  line so it can be consumed by scripts.
- `--evidence` collects local artifacts for media rows. It requires
  `--structured --include-media` and writes copied files or PNG evidence under
  `python/rpa/_media/qq` by default.

Structured output currently includes:

```text
direction=incoming sender=宋剑 time=2026/06/13 09:21 text=@王刚 下周一上午...
direction=incoming sender=李名华 time=2026/06/13 10:53 text=收到
direction=incoming sender=y time=2026/06/13 17:06 text=收到
direction=outgoing sender=王刚 time=2026/06/13 17:15 text=收到
content_type=image direction=outgoing text=[图片]
content_type=video direction=outgoing file=Screenrecording_20260623_1320 03.mp4
content_type=file direction=outgoing file=答辩记录卡.docx
```

The grouping is intentionally lightweight. It uses visible rectangles, timestamp
nodes, sender-like short prefixes, and simple badge filtering such as `管理员`.

## Visible Message Model

The stable read API for future adapter work is:

```python
from rpa.platforms.qq.messages import read_visible_messages

result = read_visible_messages(include_media=True, capture_evidence=False)
```

It returns `QQVisibleMessageResult` with `QQVisibleMessage` items. Each item uses
the same field shape for text and media messages:

- `platform`
- `platform_msg_id`
- `conversation_title`
- `content_type`
- `direction`
- `sender`
- `time_text`
- `text`
- `file_name`
- `file_size`
- `content_image_path`
- `evidence_ref`
- `rect`
- `media_rect`
- `confidence`
- `raw_metadata`

Probe commands such as `messages --structured --include-media --jsonl` consume
this model layer. The QQ adapter also calls this API directly instead of parsing
probe output.

## RPA Bridge Adapter

QQ is registered as platform `qq` in `service.rpa_bridge`.

Supported adapter commands:

- `connect`
- `disconnect`
- `health_check`
- `fetch_visible_conversations`
- `fetch_visible_messages`
- `scan_unread_and_fetch`
- `switch_conversation`
- `prepare_reply_draft`
- `send_message`

`fetch_visible_messages` calls `read_visible_messages()` directly. It emits
normalized `message_observed` events for text, image, video, and file rows.
Media rows include stable `platform_msg_id` values. When QQ does not expose a
usable row automation id, the reader generates a fallback hash from the
conversation title, visible media fields, media rectangle size, and visible
sequence.
Media rows can include original copied file paths or PNG evidence paths.
Adapter calls and the development observer default to
`qq.media_capture_evidence=true`, so unread polling can also collect local media
artifacts.

`scan_unread_and_fetch` only considers unread hints in the current visible
conversation list. It returns the current unread conversation without switching
when possible; otherwise it switches the first visible unread item using the
UIA-first path and then reads visible messages.

`prepare_reply_draft` can optionally switch to a visible conversation first. Its
main input path remains foreground-safe clipboard paste.

`send_message` currently supports text only and requires
`confirm_token=manual_confirmed_by_agent`. It switches to the requested visible
conversation when needed, fills the draft through foreground-safe clipboard
paste, clicks the QQ send button after verifying the QQ window is foreground,
and returns `sent` when the send-button click succeeds. Pass `verify=true` only
for diagnostic visible-message read-back verification. Media sending is
intentionally not implemented yet.

`connect` starts a lightweight QQ observer thread. The observer periodically
calls `scan_unread_and_fetch` and therefore only dispatches unread conversations
that are already visible in the QQ conversation list. `disconnect` stops the
observer. The C++ client registers platform `qq` through `QQRPAAdapter`, so the
aggregate UI can start/stop QQ listening through the same platform-listening
controls used by WeChat and Qianniu.

## Media Layout Probe

`media-layout` is a read-only diagnostic command for learning how QQ exposes
image, emoji, file, video, and voice messages through UIA. It does not classify
or fetch media yet.

Run it while the current QQ chat area contains visible multimedia messages:

```powershell
python -B -m rpa.platforms.qq.probe media-layout --limit 120
python -B -m rpa.platforms.qq.probe media-layout --limit 120 --output ../logs/qq_media_layout.jsonl
python -B -m rpa.platforms.qq.probe tree --output ../logs/qq_media_tree.jsonl
```

The output includes candidate control type, class, automation id, name, rect,
UIA path, parent summary, child count, available patterns, and nearby visible
text. The candidate scoring is intentionally broad so that unknown QQ media
layouts are not filtered out too early.

Useful test chats should include both incoming and outgoing samples when
possible:

- Image or screenshot messages.
- Custom emoji or stickers.
- File cards.
- Video cards.
- Voice messages.

After enough layouts are observed, the expected implementation path is:

1. Add conservative `content_type` detection for visible media bubbles.
2. Try foreground-safe context-menu copy for original files or clipboard
   bitmaps.
3. Fall back to bubble/card screenshots under `python/rpa/_media/qq/...`.

`media-messages` is the first structured read-only pass over the same visible
QQ message list:

```powershell
python -B -m rpa.platforms.qq.probe media-messages --limit 40
python -B -m rpa.platforms.qq.probe media-messages --limit 40 --jsonl
python -B -m rpa.platforms.qq.probe media-messages --limit 40 --evidence
```

It treats the direct children under `ml-root` as visible message rows and uses
the row `AutomationId` as a tentative `platform_msg_id`. Current detection is
conservative:

- `ImageControl name=图片` becomes `content_type=image`.
- Video-like filenames such as `.mp4` become `content_type=video`.
- Other visible filenames plus size text become `content_type=file`.

By default this command does not capture evidence, copy original files, open
context menus, or send messages.

Add `--evidence` to collect a local artifact for each visible media row.
Successful artifact extraction populates `content_image_path` and/or
`evidence_ref`.

Media message ids are normalized before evidence capture. QQ row automation ids
are kept when present; otherwise the reader creates a deterministic
`qq_media_<hash>` id so evidence paths, de-duplication, and bridge events remain
stable across the same visible layout.

When evidence capture is enabled, QQ first tries a foreground-safe
right-click copy PoC for the media rectangle. It only invokes a `复制`/`Copy`
menu item, then reads the clipboard. If the clipboard contains file paths, the
files are copied under `python/rpa/_media/qq/{content_type}/...`; if it contains
an image bitmap for image/emoji messages, the bitmap is saved as PNG. Local
tests have verified original artifact copy for visible image, video, and file
messages when QQ has the media available locally. If the copy path fails, the
reader falls back to the existing `PrintWindow` screenshot evidence. The PoC
intentionally does not click download, automate Save As dialogs, or open
folders.

Foreground activation is part of the copy contract. Before right-clicking, the
reader actively restores and raises the QQ window, including a short
topmost/not-topmost activation nudge. It only continues when the QQ top-level
hwnd is the foreground window; otherwise it skips the right-click and falls back
to screenshot evidence.

Use `media-menu` to inspect the QQ context-menu items exposed by the current
visible media row without invoking copy:

```powershell
python -B -m rpa.platforms.qq.probe media-menu --index 1
```

## Visible Unread Scheduling

Unread scheduling should stay limited to the currently visible conversation list
for this PoC. New QQ messages normally promote unread conversations into the
visible list, so scrolling the conversation list is not necessary for the first
working version.

Implemented commands:

```powershell
python -B -m rpa.platforms.qq.probe unread --limit 20
python -B -m rpa.platforms.qq.probe next-unread --limit 20
python -B -m rpa.platforms.qq.probe next-unread --limit 20 --read
```

Current behavior:

- `unread` lists visible conversation items where `unread_hint` is present.
- `next-unread` picks the first visible unread item in top-to-bottom list order.
- If the current conversation itself has `unread_hint`, it should be returned as
  current instead of switching away and back.
- Switching should use the existing UIA-first `switch` behavior.
- `next-unread --read` switches when needed, verifies the title, then calls
  structured current-message reading.

The first version should not scroll the list, process all unread conversations in
a loop, infer priority from preview text, or send replies. `unread_hint` remains a
best-effort badge signal, not a guaranteed unread count.

## Analyzer

`analyze_tree` is for offline dumps:

```powershell
python -B -m rpa.platforms.qq.analyze_tree ../logs/qq_uia_tree_deep_with_parent.jsonl --limit 120
```

It prints:

- Named controls.
- Message-like controls.
- Input-like controls.
- Conversation-list items with `title`, `time`, and `preview`.
- Inferred current layout: `message_root`, `title`, and `compose_area`.
- Current chat text controls and grouped chat messages.

If `parent_metadata=no`, the dump is an older format and cannot reliably separate
the left conversation list from the right message pane. Re-run `probe tree` with
the current code.

## Known Boundaries

- The reader only covers currently visible messages. It does not scroll history.
- `media-layout` only probes visible controls. It does not classify messages,
  capture evidence, copy files, or open context menus yet.
- `media-messages` only classifies visible media rows by default. With
  `--evidence`, it tries QQ right-click copy first for original image/video/file
  artifacts and falls back to visible PNG evidence. It still does not click
  download, automate Save As dialogs, or scroll history.
- `messages --structured --include-media` is also consumed by the QQ bridge
  adapter through the stable `QQVisibleMessage` model.
- Adapter `fetch_visible_messages`, manual unread fetch, and the development
  observer default to media artifact extraction. This means QQ observer handling
  may move the mouse, change the clipboard, and run more slowly than passive UIA
  polling.
- Conversation switching only targets currently visible conversation-list items.
- Unread scheduling should only consider currently visible conversation-list
  items. It should not scroll the list in this phase.
- Conversation switching defaults to UIA background switching first. If UIA
  patterns fail, it falls back to foreground-safe clicking: restore QQ, set it as
  foreground, verify the foreground hwnd, then click.
- `switch --no-activate` keeps the older direct coordinate click behavior and is
  intended only for diagnostics when click fallback is used.
- `switch --uia` only tries UIA background switching and does not fall back to
  clicking.
- `switch --no-uia` skips UIA patterns and uses the foreground click path.
- Draft fill defaults to foreground-safe clipboard paste. It restores QQ, sets
  it as foreground, verifies the foreground hwnd, then clicks the compose area.
- `draft --uia` only tries UIA `ValuePattern.SetValue` for diagnostics and does
  not fall back to clipboard paste.
- `draft --no-uia` is retained as a compatibility alias for the default
  foreground clipboard paste path.
- `draft --no-activate` keeps the older direct coordinate click behavior for the
  paste fallback and is intended only for diagnostics.
- Text sending uses the same foreground safety checks as draft fill. It does not
  use Enter because QQ send-key settings may vary by user.
- Text sending is considered successful when the QQ send-button click succeeds.
  Optional `verify=true` performs a slower visible-message read-back check for
  diagnostics.
- Unread detection is a weak hint based on visible badge text, not a guaranteed
  unread count.
- It is UIA-first. OCR is still a fallback candidate for future edge cases.
- Message grouping is heuristic and should stay conservative.
- Actual send is implemented for text only. Image, video, and file sending are
  still intentionally not implemented.

## Next Integration Plan

1. Validate the adapter commands against several QQ windows and conversation
   types.
2. Decide whether to add a lightweight observer loop, or keep QQ command-driven
   until unread scheduling is more stable.
3. Add original media-file extraction only after more layouts are observed.
4. Gate any real-send behavior behind a separate explicit implementation and
   manual confirmation flow.
