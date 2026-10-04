// Parked windows (see parked.h).
#include "parked.h"

#include "geometry.h"

#include <core/output.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/windowitem.h>
#include <window.h>
#include <workspace.h>

#include <QPointer>

#include <algorithm>
#include <cmath>

using namespace KWin;

namespace glance
{

bool isClip(Window *window)
{
    return window->desktopFileName() == QLatin1String(clipAppId);
}

bool isClipInParking(Window *window, qreal shownWidth, const QSizeF &original)
{
    return isClip(window) && shownWidth / original.width() < parkingScale(original) + 0.02;
}

void releaseKdeState(Window *window)
{
    if (window->maximizeMode() != MaximizeRestore) {
        window->maximize(MaximizeRestore);
    }
    if (window->quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
        window->setQuickTileModeAtCurrentPosition(QuickTileFlag::None);
    }
}

bool manageable(Window *window)
{
    return !window->isDeleted() && window->isNormalWindow() && !window->isFullScreen() && window->isMovable()
        && window->isResizable() && !window->isMinimized() && window->isShown() && !window->skipSwitcher()
        && window->isOnCurrentDesktop() && window->isOnCurrentActivity() && window->windowItem();
}

// It is drawn at `item` + translation + scale * (item-local point).
void retarget(WindowPaintData &data, Window *window, const QRectF &from, const QRectF &to)
{
    const qreal k = to.width() / from.width();
    const QPointF item = window->windowItem()->position();
    data.setXScale(data.xScale() * k);
    data.setYScale(data.yScale() * k);
    data.setXTranslation(to.x() - item.x() + k * (item.x() + data.xTranslation() - from.x()));
    data.setYTranslation(to.y() - item.y() + k * (item.y() + data.yTranslation() - from.y()));
}

// --- Which windows ---

bool ParkedWindows::isParked(Window *window) const
{
    return m_parked.contains(window);
}

bool ParkedWindows::isParkedNotRestoring(Window *window) const
{
    auto it = m_parked.find(window);
    return it != m_parked.end() && !it->second.restoring;
}

bool ParkedWindows::empty() const
{
    return m_parked.empty();
}

ParkedWindows::Parked *ParkedWindows::find(Window *window)
{
    auto it = m_parked.find(window);
    return it != m_parked.end() ? &it->second : nullptr;
}

const ParkedWindows::Parked *ParkedWindows::find(Window *window) const
{
    auto it = m_parked.find(window);
    return it != m_parked.end() ? &it->second : nullptr;
}

ParkedWindows::Parked &ParkedWindows::at(Window *window)
{
    return m_parked.at(window);
}

const ParkedWindows::Parked &ParkedWindows::at(Window *window) const
{
    return m_parked.at(window);
}

const std::map<Window *, ParkedWindows::Parked> &ParkedWindows::all() const
{
    return m_parked;
}

void ParkedWindows::set(Window *window, const Parked &parked)
{
    m_parked[window] = parked;
}

void ParkedWindows::erase(Window *window)
{
    m_parked.erase(window);
}

// --- Places ---

Place ParkedWindows::placeOf(Window *window) const
{
    const RectF screen = window->output()->geometryF();
    if (const Parked *parked = find(window); parked && !parked->restoring) {
        return parkedPlace(parked->shown, parked->original, screen);
    }
    return placeOfFrame(window->moveResizeGeometry(), screen);
}

QRectF ParkedWindows::placeRect(Window *window, Place place, const QSizeF &size, qreal centerY, qreal stash) const
{
    return glance::placeRect(place, size, centerY, window->output()->geometryF(),
                             workspace()->clientArea(MaximizeArea, window), stash);
}

void ParkedWindows::moveTo(Window *window, Place place, qreal scale)
{
    releaseKdeState(window);
    // Where it is drawn now: the animation starts there.
    const QRectF from = currentlyDrawn(window);
    const auto closeRanks = leaving(window);

    // Its full (unparked) size, and the vertical center it keeps.
    const Parked *parked = find(window);
    const bool isParked = parked && !parked->restoring;
    const RectF current = window->moveResizeGeometry();
    const QSizeF size = isParked ? parked->original : QSizeF(current.width(), current.height());
    const qreal centerY = isParked ? parked->shown.center().y() : current.y() + current.height() / 2;
    commitPlace(window, place, size, centerY, from, scale);
    closeRanks();
}

void ParkedWindows::fillHalf(Window *window, Place half)
{
    releaseKdeState(window);
    const QRectF from = currentlyDrawn(window);
    const auto closeRanks = leaving(window);
    const RectF screen = window->output()->geometryF();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const qreal zoneWidth = screen.width() * zoneFraction;
    const qreal x = screen.x() + (half == Place::HalfLeft ? zoneWidth : 2 * zoneWidth);
    resizeAnimated(window, RectF(x, area.y(), zoneWidth, area.height()), from);
    closeRanks();
}

void ParkedWindows::commitPlace(Window *window, Place place, const QSizeF &size, qreal centerY, const QRectF &from,
                                qreal stash)
{
    const QRectF rect = placeRect(window, place, size, centerY, stash);
    switch (place) {
    case Place::HalfLeft:
    case Place::HalfRight:
    case Place::Full:
        resizeAnimated(window, RectF(rect.x(), rect.y(), rect.width(), rect.height()), from);
        break;
    case Place::StashLeft:
    case Place::StashRight:
    case Place::ParkingLeft:
    case Place::ParkingRight:
        park(window, rect, size);
        animate(window, from);
        arrange(window);
        break;
    case Place::Free:
        break;
    }
}

void ParkedWindows::resizeAnimated(Window *window, const RectF &target, const QRectF &from)
{
    m_parked[window] = Parked{.shown = QRectF(target.x(), target.y(), target.width(), target.height()),
                              .original = QSizeF(target.width(), target.height()),
                              .restoring = true};
    window->moveResize(target);
    animate(window, from);
}

void ParkedWindows::park(Window *window, const QRectF &shown, const QSizeF &original)
{
    m_parked[window] = Parked{.shown = shown, .original = original};
    const RectF frame = window->frameGeometry();
    const QSizeF layout = layoutSize(window, shown.size(), original);
    if (isClipInParking(window, shown.width(), original)) {
        // Drawn at 1/2: half as tall as its new layout (columns go by
        // `shown`).
        m_parked[window].shown.setHeight(layout.height() / 2);
    }
    if (layout != QSizeF(frame.width(), frame.height())) {
        qInfo("glance: %s: original %.0fx%.0f, app minimum %.0fx%.0f, shown %.0fx%.0f -> resize to %.0fx%.0f",
              qPrintable(window->caption()), original.width(), original.height(),
              window->minSize().width(), window->minSize().height(),
              shown.width(), shown.height(), layout.width(), layout.height());
        window->moveResize(RectF(shown.topLeft(), layout));
    }
    applyParked(window);
}

QSizeF ParkedWindows::layoutSize(Window *window, const QSizeF &shown, const QSizeF &original)
{
    if (isClipInParking(window, shown.width(), original)) {
        return QSizeF(2 * std::round(shown.width()), 2 * std::round(shown.height()));
    }
    return glance::layoutSize(shown, original, window->clientSizeToFrameSize(window->minSize()));
}

qreal ParkedWindows::fittingScale(LogicalOutput *output, const std::vector<Window *> &windows) const
{
    std::vector<qreal> heights;
    for (Window *window : windows) {
        const Parked *parked = find(window);
        heights.push_back(parked && !parked->restoring ? parked->original.height() : window->moveResizeGeometry().height());
    }
    return glance::fittingScale(heights, workspace()->clientArea(MaximizeArea, output).height());
}

// --- Drawing ---

qreal ParkedWindows::progress(const Parked &parked)
{
    const auto elapsed = std::chrono::steady_clock::now() - parked.start;
    return std::clamp(std::chrono::duration<qreal>(elapsed) / animationTime, 0.0, 1.0);
}

QRectF ParkedWindows::displayRect(const Parked &parked)
{
    const QRectF &target = parked.preview ? *parked.preview : parked.shown;
    if (!parked.animating) {
        return target;
    }
    const qreal t = progress(parked);
    return lerpRect(parked.from, target, 1.0 - std::pow(1.0 - t, 3));
}

qreal ParkedWindows::scaleOf(Window *window) const
{
    return displayRect(m_parked.at(window)).width() / window->frameGeometry().width();
}

QRectF ParkedWindows::drawnRect(Window *window) const
{
    const auto frame = window->frameGeometry();
    return QRectF(displayRect(m_parked.at(window)).topLeft(), QSizeF(frame.width(), frame.height()) * scaleOf(window));
}

QRectF ParkedWindows::currentlyDrawn(Window *window) const
{
    if (isParked(window)) {
        return drawnRect(window);
    }
    const RectF frame = window->frameGeometry();
    return QRectF(frame.x(), frame.y(), frame.width(), frame.height());
}

void ParkedWindows::applyParked(Window *window)
{
    auto it = m_parked.find(window);
    if (it == m_parked.end() || !window->windowItem()) {
        return;
    }
    const Parked &parked = it->second;
    const RectF frame = window->frameGeometry();
    if (parked.restoring && !parked.animating && std::abs(frame.width() - parked.original.width()) < 0.5
        && std::abs(frame.height() - parked.original.height()) < 0.5) {
        const QPointF topLeft = parked.shown.topLeft();
        m_parked.erase(it);
        setDrawTransform(window, QTransform());
        if (frame.topLeft() != topLeft) {
            window->move(topLeft);
        }
        return;
    }

    const qreal scale = scaleOf(window);
    const QPointF offset = displayRect(parked).topLeft() - frame.topLeft();
    QTransform transform;
    transform.translate(offset.x(), offset.y());
    transform.scale(scale, scale);
    setDrawTransform(window, transform);
}

void ParkedWindows::animate(Window *window, const QRectF &from)
{
    auto it = m_parked.find(window);
    if (it == m_parked.end()) {
        return;
    }
    it->second.from = from;
    it->second.start = std::chrono::steady_clock::now();
    it->second.animating = true;
    applyParked(window);
    effects->addRepaintFull();
}

void ParkedWindows::advance()
{
    std::vector<Window *> animating;
    for (auto &[window, parked] : m_parked) {
        if (parked.animating) {
            if (progress(parked) >= 1.0) {
                parked.animating = false;
            }
            animating.push_back(window);
        }
    }
    for (Window *window : animating) {
        applyParked(window);
    }
}

bool ParkedWindows::anyAnimating() const
{
    return std::ranges::any_of(m_parked, [](const auto &entry) {
        return entry.second.animating;
    });
}

void ParkedWindows::setDrawTransform(Window *window, const QTransform &transform)
{
    window->windowItem()->setTransform(transform);
    Q_EMIT transformChanged(window);
}

Window *ParkedWindows::pick(const QPointF &pos, Window *ignore) const
{
    const auto &stacking = workspace()->stackingOrder();
    for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
        Window *window = *it;
        if (window == ignore || window->isDeleted() || !window->isOnCurrentActivity() || !window->isOnCurrentDesktop()
            || window->isMinimized() || window->isHidden() || window->isHiddenByShowDesktop()
            || !window->readyForPainting()) {
            continue;
        }
        if (isParked(window)) {
            if (drawnRect(window).contains(pos)) {
                return window;
            }
            continue;
        }
        if (window->hitTest(pos)) {
            return window;
        }
    }
    return nullptr;
}

bool ParkedWindows::isIcon(Window *window) const
{
    const Parked *parked = find(window);
    return parked && !parked->restoring
        && (parked->shown.width() / parked->original.width() < iconBelow || isParkingArea(areaOf(window)));
}

bool ParkedWindows::switchable(Window *window) const
{
    if (window->isDeleted() || !window->wantsTabFocus() || window->skipSwitcher() || window->isMinimized()
        || !window->isShown() || window->isHiddenByShowDesktop() || !window->readyForPainting()
        || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
        return false;
    }
    const RectF screen = window->output()->geometryF();
    return currentlyDrawn(window).intersects(QRectF(screen.x(), screen.y(), screen.width(), screen.height()));
}

// --- Making room ---

int ParkedWindows::areaOf(Window *window) const
{
    const Parked &parked = m_parked.at(window);
    switch (parkedPlace(parked.shown, parked.original, window->output()->geometryF())) {
    case Place::ParkingLeft:
        return 0;
    case Place::StashLeft:
        return 1;
    case Place::ParkingRight:
        return 2;
    default:
        return 3;
    }
}

bool ParkedWindows::isParkingArea(int area)
{
    return area % 2 == 0;
}

void ParkedWindows::arrangeArea(int area, LogicalOutput *output, Window *arriving)
{
    const RectF bounds = workspace()->clientArea(MaximizeArea, output);
    struct Item
    {
        Window *window;
        qreal key;
    };
    std::vector<Item> items;
    qreal total = 0;
    for (const auto &[window, parked] : m_parked) {
        if (parked.restoring || window->output() != output || areaOf(window) != area) {
            continue;
        }
        qreal key = parked.shown.center().y();
        if (window == arriving) {
            key += parked.shown.height() / 2;
        }
        items.push_back(Item{window, key});
        total += parked.shown.height() + (items.size() > 1 ? arrangeGap : 0);
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.key < b.key;
    });

    qreal top = std::max(bounds.y(), bounds.y() + (bounds.height() - total) / 2);
    for (const Item &item : items) {
        Parked &parked = m_parked.at(item.window);
        if (std::abs(parked.shown.y() - top) > 0.5) {
            const QRectF from = displayRect(parked);
            parked.shown.moveTop(top);
            animate(item.window, from);
        }
        top += parked.shown.height() + arrangeGap;
    }
}

void ParkedWindows::arrange(Window *window)
{
    if (isParkedNotRestoring(window) && isParkingArea(areaOf(window))) {
        arrangeArea(areaOf(window), window->output(), window);
    }
}

std::function<void()> ParkedWindows::leaving(Window *window)
{
    if (!isParkedNotRestoring(window) || !isParkingArea(areaOf(window))) {
        return [] {};
    }
    const int area = areaOf(window);
    QPointer<LogicalOutput> output = window->output();
    return [this, area, output] {
        if (output) {
            arrangeArea(area, output, nullptr);
        }
    };
}

// --- Lifetime ---

void ParkedWindows::closed(Window *window)
{
    const auto closeRanks = leaving(window);
    m_parked.erase(window);
    closeRanks();
}

void ParkedWindows::restoreAll()
{
    for (auto &[window, parked] : m_parked) {
        if (window->windowItem()) {
            window->windowItem()->setTransform(QTransform());
        }
        window->moveResize(RectF(parked.shown.topLeft(), parked.original));
    }
}

} // namespace glance
