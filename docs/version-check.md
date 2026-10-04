# Version check: a dialog when KWin didn't load Glance

Built and checked by the user 2026-10-04. User's goal: after a Plasma update, a message on the
screen that is impossible to miss, instead of Glance silently doing
nothing.

## Why it is needed
KWin already refuses an effect built for another KWin version: a
plugin's IID carries the exact version (`EffectPluginFactory_iid` =
"org.kde.kwin.EffectPluginFactory" + "6.7.5"), and
`PluginEffectLoader::factory` (src/effect/effectloader.cpp) skips a
plugin whose IID differs, before any of its code runs. So a Plasma update
can't make Glance crash KWin, but it can't speak up either: KWin logs it
at debug level only, and Glance still shows in Desktop Effects.

## At login: glance-check
`kwin/check/glance-check.in`, made into `kwin/build/bin/glance-check` by
CMake (with the KWin version it was built for and the source folder), run
at login by an autostart entry (`org.glance.Check.desktop`: installed to
/etc/xdg/autostart by `cmake --install`; `use-in-session.sh on` writes
one with the same name to ~/.config/autostart, pointing at the build
folder, which replaces the installed one).
- Quiet when Glance is turned off in Desktop Effects (kwinrc
  `[Plugins] glanceEnabled=false`), not installed (KWin's
  `listOfEffects` D-Bus property lists every effect plugin it finds,
  also refused ones), or loaded (`isEffectLoaded`).
- Otherwise a `kdialog --error` dialog (notify-send, critical, if kdialog
  is missing): with a different KWin version, "Plasma was updated" and
  the exact commands to rebuild (`sudo cmake --install` only when it runs
  from an installed copy); with the same version, where KWin's log is.
- `glance-check --test` shows the update message, for checking the
  dialog. Headless check (2026-10-04): unloading Glance over D-Bus in a
  virtual KWin, then running it, shows the same-version dialog.

## At build time: kwin-private
`kwin/kwin-private` holds KWin headers copied from one KWin version
(`GLANCE_KWIN_PRIVATE_VERSION` in kwin/CMakeLists.txt, 6.7.5). Built
against another Plasma release (6.8 after 6.7) the build stops with a
message pointing to GitHub for a newer Glance; a bug-fix release (6.7.6)
only warns. CMake re-reads KWin's version on every build (KWin's
ConfigVersion file is a configure dependency), so a kwin-devel update is
noticed by a plain `cmake --build`. After updating the copies, set
`GLANCE_KWIN_PRIVATE_VERSION`.
