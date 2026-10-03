# Open issue: plasmashell hangs at logout (2026-10-01)

abrt-applet reports a "crash" after some logins: at logout plasmashell
doesn't exit, systemd kills it after 40 s (SIGABRT, core dump). Stack:
`WaylandClipboard::~WaylandClipboard` (kguiaddons, Klipper) waiting in
`QThread::wait`. KWin doesn't crash. All 21 logouts before 2026-09-30
14:30 were clean; 4 of 9 hung after clips (drag/primary-selection
reads, KWrite starts) were introduced; correlation with clip use per
session isn't exact, and KDE has known clipboard-thread issues
(kguiaddons MR 55). First step taken: clip KWrites run in their own
app scope (they used to live in KWin's service cgroup).

Check with
`journalctl --user -o short-iso | grep -E "Stopping plasma-plasmashell|stop-sigterm"`
(a "timed out" line right after a stop = hang). If it persists: compare
logouts from sessions without any clips.

Update 2026-10-03: after the own-scope fix, 1 hang in 27 logouts
(2026-10-01 12:30 to 2026-10-03 09:13), the same stack
(`WaylandClipboard::~WaylandClipboard` → `QThread::wait`, the
`ClipboardThread` stuck in `QRecursiveMutex::tryLock`), right after the
session where clip drag-in/out (glance-clip) was first tested. Still KDE's
bug, likely made more frequent by clipboard/selection activity. Costs a
~40 s logout and a crash notice; nothing is lost. If it gets frequent:
turn off Klipper (the clipboard history owns that code), or shorten
plasmashell's stop timeout (a systemd drop-in, `TimeoutStopSec=5`).
