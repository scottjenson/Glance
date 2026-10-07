// What Glance changes in KDE while loaded, restored on unload and never
// saved to the user's settings: KWin actions for keys Glance takes over,
// quick tiling by dragging, Meta+drag's mouse command, and the Meta tap
// that opens KDE's launcher (docs/keyboard.md, docs/meta-drag.md).
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
