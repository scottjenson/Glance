# Plan: windows reopen where they were (remembered places)

Talked through with the user; not built yet.

## Goal
Like the Mac: an app's windows reopen where they were when it was last
closed, whenever the app relaunches (not only after a login). A few side
apps (music player, chat, notes) placed carefully in the stashes and
parking should come back there after a quit and relaunch. On Wayland apps
can't place their own windows, so if Glance doesn't do it, nothing does
(except apps using KWin's xdg-session-management).

## What is remembered
Every place: main, stash and parking (a parked music player is a real
place, not a "put away" state). Per app, a few window slots, each with:
- the **place**: its region and where in it, relative to the region (e.g.
  left stash, top at 30% of its height, scale 0.4), not in pixels, so a
  changed scale or resolution still works. In parking only the side and
  the order in the column;
- its **full size** (Glance's `original`), so the size the app reopens at
  doesn't matter;
- its **last title**, for matching.

A slot is updated whenever its window moves or closes; a window that
matched no slot gets a new one when it closes. At most ~10 per app (least
recently used dropped). Saved to `~/.local/state/glance/places.json`,
debounced after changes, not only at close (also covers KWin crashing).

## Recognizing a window
Wayland gives windows no lasting ID; Glance sees the app
(`desktopFileName`, or `resourceClass` for X11) and the title (`caption`,
`captionChanged`). In order:
1. Same app.
2. The app has one slot: that one, whatever the title (a music player's
   title is the current song).
3. Several slots: the same title; then a looser match (without the app
   name suffix and unsaved markers like `*` or `●`).
4. Still none: by order (the first unmatched window gets the most
   recently used unmatched slot).
5. No slot left: the window opens normally.

Expected: one-window apps very reliable; documents (title = file name)
reliable; browsers good (they reopen the same tabs); terminals and file
managers fair (a miss swaps two spots). Dialogs, popups, untitled and
non-normal windows open normally. A miss is harmless.

## Opening
A new window of an app with slots is kept invisible (not drawn) until its
title settles (`captionChanged`; some apps open as "Kate", then become
"notes.txt — Kate") or a short timeout (~300-500 ms, to tune), then goes
straight to its place: resized to its full size and parked or placed,
with no visible jump. Parking columns re-form around it. If the place no
longer fits (much smaller screen), it opens normally.

## Open questions
- Keep the logout step (`fullSizeForLogout`,
  [docs/dragging-and-parking.md](../docs/dragging-and-parking.md))? With
  remembered places Glance keeps the full size itself. Recommendation:
  keep it as a safety net, so apps come back at full size if Glance isn't
  running at the next login.
- How long to wait for the title.
- Reopening clip windows at login in their places (later, separate step).
- Apps using xdg-session-management get their position restored by KWin
  too; Glance's place then wins. Check which apps on the VM use it.
- Most recently used order for step 4 vs the order windows were opened
  last time: start with the simpler one.

## Testing
Headless: Konsole windows with fixed titles (`konsole -p tabtitle=...`)
placed by KWin scripts, closed and reopened; check places in the log and
screenshots ([docs/testing.md](../docs/testing.md)). The user: real apps
across quit/relaunch and logout.
