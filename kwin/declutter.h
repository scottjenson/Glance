// Declutter (Meta+double-click, docs/declutter.md): on a window, it takes
// the half of main nearest to it at full height, and every other window in
// main goes to the stashes, split so both end up holding about as many
// (keeping their left-to-right order); on the desktop, everything in main
// goes to the sides. Each stash then shows all its windows at one scale,
// as large as fits the screen height, so its column lines up. The same
// Meta+double-click again undoes it.
#pragma once

#include "parked.h"

#include <core/rect.h>
#include <effect/globals.h>

#include <QObject>
#include <QPointF>
#include <QPointer>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

namespace KWin
{
struct PointerButtonEvent;
}

namespace glance
{

class Declutter : public QObject
{
    Q_OBJECT

public:
    explicit Declutter(ParkedWindows &parking);

    // Every pointer button event: a Meta+double-click declutters or undoes.
    // Returns whether the event was taken.
    bool button(KWin::PointerButtonEvent *event);

private:
    using Parked = ParkedWindows::Parked;
    // How windows were before the last declutter, for undoing it: the
    // target (null for the desktop), the half it went to, and each window's
    // state then.
    struct Saved
    {
        QPointer<Window> window;
        std::optional<Parked> parked; // parked then, else free:
        KWin::RectF frame;
        KWin::MaximizeMode maximize = KWin::MaximizeRestore;
    };
    struct Layout
    {
        bool desktop;
        QPointer<Window> target;
        Place half = Place::Free;
        std::vector<Saved> saved;
    };

    void toggle(Window *target, const QPointF &pos);
    void undo();

    ParkedWindows &m_parking;
    // The last Meta+left press (for double-clicks), and whether the release
    // of a double-click's second press is to be swallowed too.
    std::optional<std::pair<QPointF, std::chrono::microseconds>> m_metaPress;
    bool m_swallowRelease = false;
    std::optional<Layout> m_last;
};

} // namespace glance
