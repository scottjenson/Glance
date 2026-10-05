# Plan: windows reopen where they were (remembered places)

Talked through with the user 2026-10-04; not built yet.

## Goal
Like the Mac: an app's windows reopen where they were when it was last
closed, whenever the app relaunches (not only after a login). User's
case: a few apps you use on the side (music player, chat, notes) placed
carefully in the stashes and parking, your main work in the middle; quit
and relaunch them and they come back to those places. Re-placing them
every time would be frustrating.

On Wayland apps can't place their own windows, so if Glance doesn't do
it, nothing does (except the few apps using KWin's xdg-session-management,
which KWin restores itself).

## What is remembered
Everything: main, stash and parking. Parked windows are meant to be used
in place (that is Glance's point), so a parked music player is a real
place, not a "put away" state (user, 2026-10-04).

Per app, a few window "slots", each with:
- the window's **place**: its region (main, stash left/right, parking
  left/right) and where in it, stored relative to the region (e.g. left
  stash, top at 30% of its height, scale 0.4), not in pixels, so a
  changed scale or resolution still works (the user has changed scale
  twice). In parking only the side and the order in the column (columns
  re-form anyway);
- its **full size** (Glance's `original`), so the size the app reopens
  at doesn't matter (an app quit while parked saves its parked size; see
  below);
- its **last title**, for matching.

A slot is updated whenever its window moves or closes; a window that
matched no slot gets a new one when it closes. A cap per app (say 10)
drops the least recently used.

Saved to a small file (e.g. `~/.local/state/glance/places.json`), written
shortly after changes (debounced), not only at close: that also covers
KWin crashing or restarting (code review, "Parked state lives only in
memory").

## Recognizing a window
Wayland gives windows no lasting ID, but Glance sees each window's app
(`desktopFileName`, the Wayland app id; `resourceClass` for X11 apps) and
its title (`caption`, `captionChanged`). In order of trust:
1. Same app.
2. The app has one slot: that one, whatever the title (a music player's
   title is the current song).
3. Several slots: the same title; then a looser match (without the app
   name suffix and unsaved markers like `*` or `●`).
4. Still none: by order (the first unmatched window gets the most
   recently used unmatched slot).
5. No slot left: the window opens normally (KWin's placement).

| Kind | Examples | Matched by | Expected |
|---|---|---|---|
| One-window apps | music, chat, mail, System Settings | app | very reliable |
| Documents | Kate, Okular, LibreOffice, image viewers | title = file name | reliable |
| Browser windows | Firefox, Chrome | title = active tab, then order | good: browsers reopen their own windows with the same tabs |
| Terminals, file managers | Konsole, Dolphin | title = folder/command, then order | fair: new terminals all start as "~ : bash"; a miss swaps two spots |

Let go (open normally): dialogs, popups, untitled windows, and anything
that isn't a normal top-level window. A miss is harmless: a window in
another remembered spot, or placed by KWin as today.

## Opening
- A new window of an app with slots is kept invisible for a moment (an
  effect can skip drawing it) until its title settles (`captionChanged`;
  some apps open as "Kate", then become "notes.txt — Kate") or a short
  timeout passes (~300-500 ms, to tune), then goes straight to its place:
  resized to its remembered full size and parked or placed there, with
  no visible jump. Parking columns re-form around it as when a window is
  dropped there; a stash allows overlap.
- If the remembered place no longer fits (screen much smaller), the
  window opens normally.

## The logout step (built 2026-10-04)
Today, when a logout starts, Glance gives parked apps their full size so
they save it (Firefox reopened at its 500 px parked size). It can't be
done at every app close: an app that quits by itself (Ctrl+Q, File →
Quit) saves its size and closes its window before Glance hears of it.
With remembered places Glance keeps the full size itself, so the logout
step isn't needed for that any more. **Decision for the user: keep it or
remove it.** Recommendation: keep it (about 15 lines) as a safety net:
if Glance isn't running at the next login (turned off, or skipped after
a Plasma update), apps come back at full size instead of stuck small.

## Open questions
- Keep the logout step (above).
- The invisible wait: how long before giving up on the title.
- Clips: their text is already saved in ~/Clips; reopening clip windows
  at login in their places could close the "clips aren't restored"
  item ([clips-back.md](clips-back.md)). Later, separate step.
- Apps using KWin's xdg-session-management get their position restored
  by KWin too; Glance's place then wins. Check which apps on the VM use
  it.
- "Most recently used" order for step 4 vs the order windows were
  opened last time: start with the simpler one and see.

## Testing
Headless: Konsole windows with fixed titles (`konsole -p tabtitle=...`)
opened, placed by KWin scripts (minimize parks), closed and reopened in
the virtual session; then check places in the log and screenshots. The
user: real apps (music player, documents, Firefox) across quit/relaunch
and logout.
