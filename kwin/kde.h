// What Glance changes in KDE while it is loaded, all restored when it is
// unloaded and nothing saved to the user's settings (docs/keyboard.md,
// docs/meta-drag.md): KWin's actions for keys Glance takes over are
// disabled, quick tiling by dragging to the side is off (it uses the same
// edges), and Meta+drag also activates and raises the window. And KDE's
// launcher, which opens when Meta is pressed alone and released, opens
// only on a real tap (see metaKey).
#pragma once

#include <options.h>

#include <QObject>
#include <QPointer>

#include <chrono>
#include <vector>

class QAction;

namespace KWin
{
struct KeyboardKeyEvent;
}

namespace glance
{

class KdeIntegration : public QObject
{
    Q_OBJECT

public:
    KdeIntegration();
    ~KdeIntegration() override;

    // Every Meta key event (see the .cpp).
    void metaKey(const KWin::KeyboardKeyEvent *event);
    // Call off the launcher for the current Meta press.
    void cancelMetaTap();
    // KWin's kglobalaccel plugin, or null if it wasn't found.
    QObject *globalAccel();

private:
    void disableKdeShortcuts();
    void disableAction(QObject *owner, const char *name);
    void activatingMetaDrag();

    // KDE's shortcut actions we disabled, to re-enable on unload.
    std::vector<QPointer<QAction>> m_disabledActions;
    // Quick tiling setting to restore when unloaded.
    bool m_savedTiling = true;
    // Meta+left-drag's mouse command to restore when unloaded (see
    // activatingMetaDrag).
    KWin::Options::MouseCommand m_savedCommandAll1 = KWin::Options::MouseMove;
    // When Meta went down (see metaKey).
    std::chrono::microseconds m_metaDown{};
    // KWin's kglobalaccel plugin (see cancelMetaTap).
    QPointer<QObject> m_globalAccel;
};

} // namespace glance
