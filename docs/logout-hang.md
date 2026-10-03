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

Update 2026-10-03 14:07: another one, same stack, first logout after a
VM reboot, in a session with Mac→VM clipboard use (no clips). Separate
from the plasma-keyboard login crash (see CLAUDE.md, fixed by rebooting
the VM).

## Root cause (found 2026-10-03)
A deadlock in KDE's kguiaddons 6.30 (src/systemclipboard/
waylandclipboard.cpp, source unpacked at ~/src/kguiaddons-6.30.0):
`WaylandClipboard::mimeData()` locks the global `s_clipboardLock` and
unlocks it only via `QTimer::singleShot(0, ...)`. If Klipper reads the
clipboard while plasmashell is quitting, the event loop never runs that
unlock; `~WaylandClipboard` then wakes `ClipboardThread` and waits for
it, but the thread blocks on the leaked lock (`tryLock`). Matches both
stacks exactly. Trigger: the clipboard owner exits during logout, so the
clipboard changes as plasmashell quits. In the 14:07 hang, systemd
stopped the VMware user agent (`vmtoolsd -n vmusr`, owner of the clipboard
after a Mac copy) at 14:06:42.93 and plasmashell at 14:06:42.94. Not
Glance code; made likelier by our Mac→VM copying and by clip apps owning
the clipboard. Options: `TimeoutStopSec=5` drop-in for
plasma-plasmashell.service, Klipper off, report to KDE (kguiaddons).
