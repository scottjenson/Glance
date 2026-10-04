// Alt+Tab (and Meta+Tab; replaces KDE's window switcher): hunt and return
// (docs/alt-tab.md). Windows in the order they were last used; a quick
// Alt+Tab goes back to the previous one, so two windows toggle with a tap.
// Holding Alt shows the map: the whole desktop drawn at mapScale in the
// middle of the screen, same layout, dimmed except the selected window,
// overlapping windows spread into rows above and below their pile; a label
// at the bottom of the selected window names it (icon and title). Tab /
// Shift+Tab move the selection, releasing Alt focuses it where it is, Esc
// cancels. Nothing moves: only the drawing changes.
//
// The focus ring (kept by the effect) follows the selection: see
// highlighted, mapZoom and the signals.
#pragma once

#include "parked.h"

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTimer>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QImage;

namespace KWin
{
class GLTexture;
struct KeyboardKeyEvent;
class RenderTarget;
class RenderViewport;
class ScreenPrePaintData;
class WindowPaintData;
class WindowPrePaintData;
}

namespace glance
{

class AltTab : public QObject
{
    Q_OBJECT

public:
    explicit AltTab(ParkedWindows &parking);
    ~AltTab() override;

    // A switch is on.
    bool switching() const;
    // The map is up (or closing).
    bool mapShown() const;
    // Alt+Tab or Meta+Tab (also with Shift): starts a switch.
    bool startsSwitch(const KWin::KeyboardKeyEvent *event) const;
    // Every key while a switch is on, and the one that starts it. Returns
    // whether to swallow it.
    bool key(KWin::KeyboardKeyEvent *event);

    // The window that gets the focus ring: the active one, or during a
    // switch the selected one (then the chosen one until it is active).
    Window *highlighted() const;
    // How much larger than where it is drawn the map draws `window` in
    // this frame (1 when it isn't in the map).
    qreal mapZoom(Window *window) const;

    // Painting the map and the label. paintWindow returns whether the map
    // is up: then every window is painted with a finite region (see
    // Glance::paintWindow).
    void prePaintScreen(KWin::ScreenPrePaintData &data);
    // While the map opens or closes, every frame is needed; fully open,
    // only when something changes.
    void postPaintScreen();
    void prePaintWindow(Window *window, KWin::WindowPrePaintData &data);
    bool paintWindow(Window *window, KWin::WindowPaintData &data);
    void paintScreen(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                     KWin::LogicalOutput *screen);

Q_SIGNALS:
    // The focus ring is to follow the highlighted window (see highlighted).
    void ringChanged();
    // The ring goes to `window` by keyboard: it bounces there (null: it
    // just follows).
    void bounce(KWin::Window *window);

private:
    // A switch in progress (see key): the windows in recency order when it
    // started, the selected one (-1 before the first Tab) and the modifier
    // it is held with (Alt or Meta).
    struct Switch
    {
        std::vector<QPointer<Window>> windows;
        int index = -1;
        Qt::KeyboardModifier modifier = Qt::AltModifier;
    };
    // The map while it is up or closing (see openMap): its centre, the
    // windows in it (others fade out), where piled windows spread to (see
    // spreadPiles), when it opened, and when it started closing and how far
    // open it was then. `chosen` stays undimmed while it closes.
    struct Map
    {
        QPointF center;
        std::set<Window *> windows = {};
        std::map<Window *, QRectF> spread = {};
        std::chrono::steady_clock::time_point opened = {};
        bool closing = false;
        std::chrono::steady_clock::time_point closed = {};
        qreal openAtClose = 0;
        QPointer<Window> chosen = nullptr;
    };

    void add(Window *window);
    void noteActivated(Window *window);
    void startSwitch(Qt::KeyboardModifier modifier);
    void step(int direction);
    Window *mapSelected() const;
    void finishSwitch(bool accept);

    bool inMap(Window *window) const;
    void openMap();
    void closeMap(Window *chosen);
    qreal mapProgress() const;
    QRectF toMap(const QRectF &rect) const;
    QRectF mapped(Window *window, const QRectF &from) const;
    std::map<Window *, QRectF> spreadPiles(const QRectF &screen) const;

    void paintLabel(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                    KWin::LogicalOutput *screen);
    static QImage labelImage(Window *window, qreal devicePixelRatio);

    ParkedWindows &m_parking;
    // Windows, most recently used first (see noteActivated; KWin's own
    // focus chain isn't exported to plugins).
    std::vector<Window *> m_recent;
    std::optional<Switch> m_switch;
    QPointer<Window> m_chosen;
    // Shows the map once Alt is held.
    QTimer m_hold;
    std::optional<Map> m_map;
    // How far open the map is in this frame (0 to 1).
    qreal m_mapOpen = 0;
    // The label's texture, its size on screen, and what it shows.
    std::unique_ptr<KWin::GLTexture> m_label;
    QSizeF m_labelSize;
    QPointer<Window> m_labelWindow;
    QString m_labelCaption;
    qreal m_labelScale = 0;
};

} // namespace glance
