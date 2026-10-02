# Plan: clips back into documents (not now)

User's goal, 2026-10-01: "move information around", reversibly. Today
getting a clip's text back means click, select all, copy, paste. Built clips:
[docs/clips.md](../docs/clips.md).

Likely answer: our own small clip app (Qt): text shown large without menus,
the window body is a drag source (title bar still moves it), one file per
clip as now, images later, could reformat when parked; Glance would launch
it instead of KWrite.

Rich text and images (discussed 2026-10-01): ask the source for text/html
(Firefox offers it), RTF, image/png; store .html/.png/.txt; offer the same
formats when dragged back out; Qt shows simple HTML. Main work: cleaning web
HTML down to bold/italic/links/lists/headings; images inside web HTML are
often remote links.

Cheap stopgap offered: an action on a clip window (e.g. clicking a parked
clip) copies its file with `wl-copy`. CopyQ was considered (items drag out,
but it's one list window, not a window per clip).
