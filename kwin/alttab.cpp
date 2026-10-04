// Alt+Tab (see alttab.h).
#include "alttab.h"

#include "geometry.h"

#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <input.h>
#include <input_event.h>
#include <keyboard_input.h>
#include <opengl/glutils.h>
#include <wayland/seat.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

AltTab::AltTab(ParkedWindows &parking)
    : m_parking(parking)
{
    // Until windows get used, the front one counts as the most recent.
    const auto &stacking = workspace()->stackingOrder();
    m_recent.assign(stacking.rbegin(), stacking.rend());
    noteActivated(workspace()->activeWindow());
    for (Window *window : workspace()->windows()) {
        add(window);
    }
    connect(workspace(), &Workspace::windowAdded, this, &AltTab::add);
    connect(workspace(), &Workspace::windowActivated, this, &AltTab::noteActivated);

    m_hold.setSingleShot(true);
    m_hold.setInterval(holdDelay);
    connect(&m_hold, &QTimer::timeout, this, [this]() {
        if (m_switch) {
            openMap();
        }
    });
    // For checking the map without a keyboard (e.g. in a headless KWin
    // with a screenshot): GLANCE_TEST_MAP=1 opens it 3 s after loading,
    // GLANCE_TEST_MAP=N after N s (slow-starting apps).
    if (qEnvironmentVariableIsSet("GLANCE_TEST_MAP")) {
        const int seconds = std::max(3, qEnvironmentVariableIntValue("GLANCE_TEST_MAP"));
        QTimer::singleShot(seconds * 1000, this, [this]() {
            startSwitch(Qt::AltModifier);
            step(1); // the hold timer then opens the map
        });
    }
}

AltTab::~AltTab()
{
    if (m_switch) {
        input()->keyboard()->update(); // give the keyboard back
    }
    // A texture can only be deleted with its GL context current.
    if (m_label && !effects->makeOpenGLContextCurrent()) {
        (void)m_label.release();
    }
    m_label.reset();
}

bool AltTab::switching() const
{
    return m_switch.has_value();
}

bool AltTab::mapShown() const
{
    return m_map.has_value();
}

// --- Hunt and return ---

void AltTab::add(Window *window)
{
    if (!std::ranges::contains(m_recent, window)) {
        m_recent.push_back(window); // new, not used yet
    }
    connect(window, &Window::closed, this, [this, window]() {
        std::erase(m_recent, window);
    });
}

void AltTab::noteActivated(Window *window)
{
    if (window) {
        std::erase(m_recent, window);
        m_recent.insert(m_recent.begin(), window);
    }
}

bool AltTab::startsSwitch(const KeyboardKeyEvent *event) const
{
    const Qt::KeyboardModifiers modifiers = event->modifiers & ~Qt::ShiftModifier;
    return event->state == KeyboardKeyState::Pressed && (event->key == Qt::Key_Tab || event->key == Qt::Key_Backtab)
        && (modifiers == Qt::AltModifier || modifiers == Qt::MetaModifier) && !workspace()->moveResizeWindow();
}

// Tab / Shift+Tab (Backtab) step through the windows, Esc cancels,
// releasing the modifier chooses. Other keys do nothing (no window has the
// keyboard meanwhile, see startSwitch). Under Alt, Tab and Esc are ours.
// Under Meta they are passed on, as for Meta+arrows (see Glance::onKey):
// KDE's own switcher actions on them are disabled.
bool AltTab::key(KeyboardKeyEvent *event)
{
    if (!m_switch) {
        startSwitch(event->modifiers & Qt::AltModifier ? Qt::AltModifier : Qt::MetaModifier);
    }
    if (!(event->modifiers & m_switch->modifier)) {
        finishSwitch(true);
        return false; // the modifier's release goes on
    }
    const bool tab = event->key == Qt::Key_Tab || event->key == Qt::Key_Backtab;
    const bool escape = event->key == Qt::Key_Escape;
    const bool swallow = (tab || escape) && m_switch->modifier == Qt::AltModifier;
    if (swallow && event->state == KeyboardKeyState::Pressed) {
        input()->keyboard()->addFilteredKey(event->nativeScanCode); // its release isn't sent either
    }
    if (event->state != KeyboardKeyState::Released) {
        if (tab) {
            step(event->key == Qt::Key_Backtab || (event->modifiers & Qt::ShiftModifier) ? -1 : 1);
        } else if (escape) {
            finishSwitch(false);
        }
    }
    return swallow;
}

void AltTab::startSwitch(Qt::KeyboardModifier modifier)
{
    Switch s;
    s.modifier = modifier;
    for (Window *window : m_recent) {
        if (m_parking.switchable(window)) {
            s.windows.push_back(window);
        }
    }
    // The first Tab goes to the second window, the one used before the
    // active one; if no switchable window is active, to the first.
    s.index = !s.windows.empty() && s.windows.front() == workspace()->activeWindow() ? 0 : -1;
    m_switch = std::move(s);
    // Like KDE's own switcher: no window has the keyboard meanwhile, so the
    // app doesn't get the keys, nor a lone Alt press and release (Firefox
    // would show its menu bar).
    waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
    m_hold.start();
    qInfo("glance: switch started (%d windows)", int(m_switch->windows.size()));
}

void AltTab::step(int direction)
{
    Switch &s = *m_switch;
    const int n = int(s.windows.size());
    for (int tries = 0; tries < n; ++tries) {
        s.index = s.index < 0 ? (direction > 0 ? 0 : n - 1) : (s.index + direction + n) % n;
        if (s.windows[s.index]) {
            break; // skips windows closed meanwhile
        }
    }
    Q_EMIT bounce(mapSelected()); // the ring (and bounce) go to the selection
    if (m_map) {
        effects->addRepaintFull(); // the selection is undimmed, the label moves
    }
}

// The pointer moved over `window` in the map: it becomes the selection.
// The ring follows without a bounce (that is for keyboard moves).
void AltTab::select(Window *window)
{
    const auto it = std::ranges::find_if(m_switch->windows, [window](const QPointer<Window> &w) {
        return w == window;
    });
    if (it == m_switch->windows.end() || window == mapSelected()) {
        return;
    }
    m_switch->index = int(it - m_switch->windows.begin());
    Q_EMIT ringChanged();
    effects->addRepaintFull(); // the selection is undimmed, the label moves
}

bool AltTab::motion(const QPointF &position)
{
    if (!m_switch && !m_map) {
        return false;
    }
    if (m_switch && m_map && !m_map->closing) {
        if (Window *window = mapWindowAt(position)) {
            select(window);
        }
    }
    return true;
}

// A left click on a window in the map chooses it, as releasing the modifier
// would (like KDE's own switcher, the window gets the keyboard back while
// the modifier is still held). Clicks elsewhere do nothing.
bool AltTab::button(const PointerButtonEvent *event)
{
    if (event->state == PointerButtonState::Released) {
        return m_buttons.erase(event->nativeButton) > 0;
    }
    if (!m_switch && !m_map) {
        return false;
    }
    m_buttons.insert(event->nativeButton);
    if (event->button == Qt::LeftButton && m_switch && m_map && !m_map->closing) {
        if (Window *window = mapWindowAt(event->position)) {
            select(window);
            qInfo("glance: chosen by click");
            finishSwitch(true);
        }
    }
    return true;
}

// The selected window: the switch's, or the chosen one while the map
// closes.
Window *AltTab::mapSelected() const
{
    if (m_switch) {
        return m_switch->index >= 0 ? m_switch->windows[m_switch->index].data() : nullptr;
    }
    return m_map ? m_map->chosen.data() : nullptr;
}

Window *AltTab::highlighted() const
{
    if (m_switch) {
        return mapSelected();
    }
    return m_chosen ? m_chosen.data() : workspace()->activeWindow();
}

// End the switch: activate the selected window (`accept`), or leave things
// as they were.
void AltTab::finishSwitch(bool accept)
{
    QPointer<Window> chosen = accept ? mapSelected() : nullptr;
    const bool mapped = m_map && !m_map->closing;
    m_switch.reset();
    m_chosen = chosen; // keeps the ring until it is active
    m_hold.stop();
    if (m_map) {
        closeMap(chosen);
    }
    if (chosen) {
        qInfo("glance: window chosen: %s", qPrintable(chosen->caption()));
    } else {
        qInfo("glance: switch cancelled");
    }
    // Once the key event that ended the switch has gone through (with no
    // keyboard focus, so the app doesn't see the modifier's release), give
    // the keyboard back. The chosen window has the ring already; after the
    // map it bounces again as it gets focus. Cancelled, the ring goes back
    // to the active window.
    QTimer::singleShot(0, this, [this, chosen, mapped]() {
        m_chosen = nullptr;
        if (chosen && !chosen->isDeleted() && chosen != workspace()->activeWindow()) {
            workspace()->activateWindow(chosen);
        }
        if (chosen && mapped) {
            Q_EMIT bounce(chosen);
        } else {
            Q_EMIT ringChanged();
        }
        input()->keyboard()->update();
    });
}

// --- The map ---

bool AltTab::inMap(Window *window) const
{
    return window->isDesktop() || m_map->windows.contains(window);
}

void AltTab::openMap()
{
    const RectF screen = workspace()->activeOutput()->geometryF();
    m_map = Map{};
    m_map->center = QPointF(screen.x() + screen.width() / 2, screen.y() + screen.height() / 2);
    for (const QPointer<Window> &window : m_switch->windows) {
        if (window) {
            m_map->windows.insert(window);
        }
    }
    m_map->opened = std::chrono::steady_clock::now();
    m_map->spread = spreadPiles(QRectF(screen.x(), screen.y(), screen.width(), screen.height()));
    effects->addRepaintFull();
    qInfo("glance: map shown (%d windows, %d spread)", int(m_map->windows.size()), int(m_map->spread.size()));
}

// Start zooming the map back to full size from wherever it is now.
void AltTab::closeMap(Window *chosen)
{
    m_map->openAtClose = mapProgress();
    m_map->closing = true;
    m_map->closed = std::chrono::steady_clock::now();
    m_map->chosen = chosen;
    effects->addRepaintFull();
}

// How far open the map is (0 to 1, eased): opening or closing takes
// mapTime.
qreal AltTab::mapProgress() const
{
    const auto ease = [](qreal t) {
        return 1.0 - std::pow(1.0 - std::clamp(t, 0.0, 1.0), 3);
    };
    const auto now = std::chrono::steady_clock::now();
    if (m_map->closing) {
        return m_map->openAtClose * (1.0 - ease(std::chrono::duration<qreal>(now - m_map->closed) / mapTime));
    }
    return ease(std::chrono::duration<qreal>(now - m_map->opened) / mapTime);
}

// Where the map draws something drawn at `rect`: scaled by mapScale toward
// the screen's centre.
QRectF AltTab::toMap(const QRectF &rect) const
{
    const QPointF c = m_map->center;
    return QRectF(c.x() + (rect.x() - c.x()) * mapScale, c.y() + (rect.y() - c.y()) * mapScale,
                  rect.width() * mapScale, rect.height() * mapScale);
}

// Where a window drawn at `from` is drawn in this frame of the map: on the
// straight way to its place there (shrinking and spreading at once).
QRectF AltTab::mapped(Window *window, const QRectF &from) const
{
    auto it = m_map->spread.find(window);
    return lerpRect(from, it != m_map->spread.end() ? it->second : toMap(from), m_mapOpen);
}

// The window the map draws at `position` in this frame, or null. The label
// counts as part of the selected window (it is drawn over its neighbours).
Window *AltTab::mapWindowAt(const QPointF &position) const
{
    Window *selected = mapSelected();
    if (selected && m_label && m_labelWindow == selected && m_labelRect.contains(position)) {
        return selected;
    }
    const auto &stacking = workspace()->stackingOrder();
    for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
        if (!m_map->windows.contains(*it)) {
            continue;
        }
        const QRectF drawn = m_parking.currentlyDrawn(*it);
        if (drawn.width() > 0 && mapped(*it, drawn).contains(position)) {
            return *it;
        }
    }
    return nullptr;
}

qreal AltTab::mapZoom(Window *window) const
{
    if (!m_map || !inMap(window)) {
        return 1.0;
    }
    const QRectF drawn = m_parking.currentlyDrawn(window);
    return drawn.width() > 0 ? mapped(window, drawn).width() / drawn.width() : 1.0;
}

// Piles in the map (see glance::spreadPiles), spread so every window can be
// counted and pointed at. Parked windows stay out: their columns and slight
// stash overlaps don't count. Returns where they go (global, at map scale).
std::map<Window *, QRectF> AltTab::spreadPiles(const QRectF &screen) const
{
    // Free windows in the map, front first, where the map draws them.
    std::vector<std::pair<Window *, QRectF>> items;
    const auto &stacking = workspace()->stackingOrder();
    for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
        if (m_map->windows.contains(*it) && !m_parking.isParked(*it)) {
            items.emplace_back(*it, toMap(m_parking.currentlyDrawn(*it)));
        }
    }
    std::vector<QRectF> rects;
    for (const auto &item : items) {
        rects.push_back(item.second);
    }
    const auto places = glance::spreadPiles(rects, screen);
    std::map<Window *, QRectF> spread;
    for (size_t i = 0; i < items.size(); ++i) {
        if (places[i]) {
            spread[items[i].first] = *places[i];
        }
    }
    return spread;
}

// --- Painting ---

void AltTab::prePaintScreen(ScreenPrePaintData &data)
{
    if (!m_map) {
        return;
    }
    m_mapOpen = mapProgress();
    if (m_map->closing && m_mapOpen <= 0.0) {
        m_map.reset();
        effects->addRepaintFull();
    } else {
        data.mask |= Effect::PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }
    Q_EMIT ringChanged(); // its width follows the map's scale
}

void AltTab::postPaintScreen()
{
    if (m_map && (m_map->closing || m_mapOpen < 1.0)) {
        effects->addRepaintFull();
    }
}

void AltTab::prePaintWindow(Window *window, WindowPrePaintData &data)
{
    if (m_map) {
        data.setTransformed();
        if (!inMap(window)) {
            data.setTranslucent(); // fades out
        }
    }
}

// With the map up, every window in it is drawn where the map has it (see
// mapped), dimmed unless selected; the others (panels, notifications) fade
// out.
bool AltTab::paintWindow(Window *window, WindowPaintData &data)
{
    if (!m_map) {
        return false;
    }
    if (inMap(window) && window->windowItem() && m_parking.currentlyDrawn(window).width() > 0) {
        const QRectF from = m_parking.currentlyDrawn(window);
        retarget(data, window, from, mapped(window, from));
        if (window != mapSelected()) {
            data.multiplyBrightness(1.0 - (1.0 - mapDim) * m_mapOpen);
        }
    } else {
        data.multiplyOpacity(1.0 - m_mapOpen);
    }
    return true;
}

// The label, over everything.
void AltTab::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, LogicalOutput *screen)
{
    if (m_map && m_mapOpen > 0.0 && effects->isOpenGLCompositing()) {
        paintLabel(renderTarget, viewport, screen);
    }
}

// --- The label ---

// The selected window's icon and title on one line, on a rounded
// translucent card, centred on the window where the map draws it, its
// bottom on the window's bottom edge: always in the same spot, inside the
// window (wider than the window if need be). Fades with the map. Redrawn
// when the selection (or its title) changes.
void AltTab::paintLabel(const RenderTarget &renderTarget, const RenderViewport &viewport, LogicalOutput *screen)
{
    Window *window = mapSelected();
    if (!window) {
        return;
    }
    const qreal scale = viewport.scale();
    if (!m_label || m_labelWindow != window || m_labelCaption != window->caption() || m_labelScale != scale) {
        const QImage image = labelImage(window, scale);
        m_label = GLTexture::upload(image);
        if (!m_label) {
            return;
        }
        m_label->setFilter(GL_LINEAR);
        m_labelSize = QSizeF(image.size()) / scale;
        m_labelWindow = window;
        m_labelCaption = window->caption();
        m_labelScale = scale;
    }
    const RectF area = screen->geometryF();
    const QRectF drawn = inMap(window) ? mapped(window, m_parking.currentlyDrawn(window)) : m_parking.currentlyDrawn(window);
    const QSizeF size = m_labelSize;
    const qreal x = std::clamp(drawn.center().x() - size.width() / 2, area.left(), area.right() - size.width());
    const QPointF topLeft(x, drawn.bottom() - size.height());
    m_labelRect = QRectF(topLeft, size);
    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(std::round(topLeft.x() * scale), std::round(topLeft.y() * scale));
    const qreal opacity = m_mapOpen;

    GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::Modulate
                                                             | ShaderTrait::TransformColorspace);
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
    shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(opacity, opacity, opacity, opacity));
    shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied
    m_label->render(QSizeF(m_label->size()));
    glDisable(GL_BLEND);
    ShaderManager::instance()->popShader();
}

QImage AltTab::labelImage(Window *window, qreal devicePixelRatio)
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(labelTextSize);
    const QFontMetricsF metrics(font);
    const QString title = metrics.elidedText(window->caption(), Qt::ElideRight, labelMaxWidth);
    const qreal textWidth = metrics.horizontalAdvance(title);
    const QSizeF size(2 * labelPadding + labelIconSize + labelGap + textWidth,
                      2 * labelPadding + std::max(labelIconSize, metrics.height()));

    QImage image((size * devicePixelRatio).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 20, 20, 200));
    painter.drawRoundedRect(QRectF(QPointF(0, 0), size), labelRadius, labelRadius);
    QIcon icon = window->icon();
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }
    icon.paint(&painter, QRectF(labelPadding, (size.height() - labelIconSize) / 2, labelIconSize, labelIconSize).toRect());
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(labelPadding + labelIconSize + labelGap, 0, textWidth + 1, size.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, title);
    return image;
}

} // namespace glance
