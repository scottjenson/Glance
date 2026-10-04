# Plan: clips back into documents (phase 1 built; phase 2 later)

User's goal, 2026-10-01: "move information around", reversibly. A clip
should be a lightweight clipboard object: select text, drag it out, it
becomes a little window; drag that window's text back into a document and
it is pasted. Built clips:
[docs/clips.md](../docs/clips.md).

## Phase 1: built 2026-10-03
The clip app and dragging clips back in: see [docs/clips.md](../docs/clips.md).

## Phase 2: rich text and images (discussed 2026-10-01, later)
Ask the source for text/html (Firefox offers it), RTF, image/png; store
.html/.png/.txt; offer the same formats when dragged back out; Qt shows
simple HTML. Main work: cleaning web HTML down to bold/italic/links/
lists/headings; images inside web HTML are often remote links.

Images (talked through 2026-10-03): Firefox's "Copy Image" puts the real
pixels on the clipboard (checked with `wl-paste --list-types`): image/png,
jpeg, webp, tiff, bmp and more, at the best resolution it loaded (1536x650
for an image shown at 816x345), plus text/html with an `<img src=https…>`;
no text/uri-list. So Meta+C can fall back to a clipboard image without
downloading anything. Drags likely offer the same plus text/uri-list
(unchecked: log the drag's mime types when building it); `clipMimeType`
must then prefer image/png over bailing on uri-list. Ideas: store
~/Clips/<time>.png; the clip window takes the image's shape (images make
far better parking icons than text); dragged back out it offers
image/png and the file as file:// (upload forms, Dolphin); Spectacle
screenshots dragged out are another source. https-only drags (download)
left out at first.

## Open questions
- Clips after logout: the files stay in ~/Clips but the windows aren't
  restored at login.
- Parked windows' invisible full-size frames can catch drops meant for
  other parked windows (KWin's drag and drop goes by frames; the general
  input-transform limit, see the backlog's long term).
