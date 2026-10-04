// KDE integration (see kde.h).
#include "kde.h"

#include "tuning.h"

#include <input_event.h>
#include <workspace.h>

#include <QAction>
#include <QPluginLoader>

using namespace KWin;

namespace glance
{

KdeIntegration::KdeIntegration()
{
    disableKdeShortcuts();

    m_savedTiling = options->electricBorderTiling();
    options->setElectricBorderTiling(false);
    // Keep it off if the settings are reloaded.
    connect(options, &Options::electricBorderTilingChanged, this, []() {
        if (options->electricBorderTiling()) {
            options->setElectricBorderTiling(false);
        }
    });

    // Meta+drag activates the window, as a title-bar drag does: KDE's
    // default for it is "Move", which leaves another window active.
    m_savedCommandAll1 = options->commandAll1();
    activatingMetaDrag();
    connect(options, &Options::commandAll1Changed, this, &KdeIntegration::activatingMetaDrag);
}

KdeIntegration::~KdeIntegration()
{
    disconnect(options, nullptr, this, nullptr);
    options->setElectricBorderTiling(m_savedTiling);
    options->setCommandAll1(m_savedCommandAll1);
    for (const QPointer<QAction> &action : m_disabledActions) {
        if (action) {
            action->setEnabled(true);
        }
    }
}

// Meta pressed alone and released opens KDE's launcher. Glance uses Meta a
// lot (drag, wheel, double-click), so only a real tap does: a press held
// longer than metaTapMax (the user meant something else and let go)
// doesn't, nor one shorter than metaTapMin (no hand is that fast: VMware
// Fusion sends such taps when it held Command back).
void KdeIntegration::metaKey(const KeyboardKeyEvent *event)
{
    if (event->state == KeyboardKeyState::Pressed) {
        m_metaDown = event->timestamp;
    } else if (event->state == KeyboardKeyState::Released) {
        const auto held = event->timestamp - m_metaDown;
        if (held < metaTapMin || held > metaTapMax) {
            cancelMetaTap();
        }
    }
}

// Call off the launcher for the current Meta press. KWin doesn't export
// its own call for it (GlobalShortcutsManager::cancelModiferOnlySequence),
// but the slot it uses is on KWin's kglobalaccel plugin, a static Qt
// plugin, whose instance Qt hands out.
void KdeIntegration::cancelMetaTap()
{
    if (QObject *accel = globalAccel()) {
        QMetaObject::invokeMethod(accel, "cancelModiferOnlySequence");
    }
}

QObject *KdeIntegration::globalAccel()
{
    if (!m_globalAccel) {
        for (const QStaticPlugin &plugin : QPluginLoader::staticPlugins()) {
            if (plugin.metaData().value(QLatin1String("IID")).toString().contains(QLatin1String("KGlobalAccelInterface"))) {
                m_globalAccel = plugin.instance();
                break;
            }
        }
    }
    return m_globalAccel;
}

// Our Meta+arrows replace KDE's quick tiling on the same keys, and our
// Meta+Alt+arrows its "Switch to Window" ones. Rather than hiding the
// keys from KDE's shortcut system (which then opens the launcher when
// Meta is released), disable KWin's actions for them: the shortcut still
// matches, and a disabled action does nothing. Only while the effect is
// loaded; nothing is saved to the user's settings.
// Our Alt+Tab replaces KDE's window switcher (all its "Walk Through
// Windows" actions, also those for the current app's windows).
void KdeIntegration::disableKdeShortcuts()
{
    for (const char *name : {"Window Quick Tile Left", "Window Quick Tile Right",
                             "Window Quick Tile Top", "Window Quick Tile Bottom",
                             "Switch Window Left", "Switch Window Right",
                             "Switch Window Up", "Switch Window Down"}) {
        disableAction(workspace(), name);
    }
    // The switcher's actions belong to KWin's TabBox object, whose class
    // header kwin-devel doesn't install. It derives from QObject alone,
    // so its pointer is the QObject's.
    if (QObject *tabBox = reinterpret_cast<QObject *>(workspace()->tabbox())) {
        for (const char *name : {"Walk Through Windows", "Walk Through Windows (Reverse)",
                                 "Walk Through Windows Alternative", "Walk Through Windows Alternative (Reverse)",
                                 "Walk Through Windows of Current Application",
                                 "Walk Through Windows of Current Application (Reverse)",
                                 "Walk Through Windows of Current Application Alternative",
                                 "Walk Through Windows of Current Application Alternative (Reverse)"}) {
            disableAction(tabBox, name);
        }
    }
}

void KdeIntegration::disableAction(QObject *owner, const char *name)
{
    QAction *action = owner->findChild<QAction *>(QString::fromLatin1(name));
    if (!action) {
        qWarning("glance: KWin action \"%s\" not found", name);
        return;
    }
    if (action->isEnabled()) {
        action->setEnabled(false);
        m_disabledActions.push_back(action);
    }
}

// Meta+drag ("Move" or "Unrestricted move") also activates and raises.
// Also when the settings are reloaded.
void KdeIntegration::activatingMetaDrag()
{
    if (options->commandAll1() == Options::MouseMove) {
        options->setCommandAll1(Options::MouseActivateRaiseAndMove);
    } else if (options->commandAll1() == Options::MouseUnrestrictedMove) {
        options->setCommandAll1(Options::MouseActivateRaiseAndUnrestrictedMove);
    }
}

} // namespace glance
