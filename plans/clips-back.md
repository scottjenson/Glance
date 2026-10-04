# Plan: clips back into documents (phase 1 built; phase 2 later)

User's goal, 2026-10-01: "move information around", reversibly. A clip
should be a lightweight clipboard object: select text, drag it out, it
becomes a little window; drag that window's text back into a document and
it is pasted. Built clips:
[docs/clips.md](../docs/clips.md).

## Phase 1: built 2026-10-03
The clip app and dragging clips back in: see [docs/clips.md](../docs/clips.md).

## Phase 2: rich text (images built 2026-10-03, see docs/clips.md)
Ask the source for text/html (Firefox offers it), RTF, image/png; store
.html/.png/.txt; offer the same formats when dragged back out; Qt shows
simple HTML. Main work: cleaning web HTML down to bold/italic/links/
lists/headings; images inside web HTML are often remote links.

Images: built ([docs/clips.md](../docs/clips.md), "Image clips"). Left
out: Meta+C with a clipboard image (dropped by the user, 2026-10-04),
https-only drags (download).

## Open questions
- Clips after logout: the files stay in ~/Clips but the windows aren't
  restored at login (kept for later, 2026-10-04).
- Alt+Tab shows clips with the icon only.
- Parked windows' invisible full-size frames can catch drops meant for
  other parked windows (KWin's drag and drop goes by frames; the general
  input-transform limit, see the backlog's long term).
