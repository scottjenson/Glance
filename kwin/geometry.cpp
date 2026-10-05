// Glance's geometry (see geometry.h).
#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>

namespace glance
{

qreal edgeScale(qreal cursorToScreenEdge, qreal cursorToWindowEdge, qreal zoneWidth)
{
    const qreal k = (1.0 - minScale) / zoneWidth;
    return (minScale + k * cursorToScreenEdge) / (1.0 + k * cursorToWindowEdge);
}

qreal heldScale(qreal rule, qreal from, qreal hold, qreal floor)
{
    if (from >= 1.0 - 1e-3 || from <= floor + 1e-3) {
        return rule; // nothing to remap
    }
    if (rule >= from) {
        return hold + (1.0 - hold) * (rule - from) / (1.0 - from);
    }
    return floor + (hold - floor) * (rule - floor) / (from - floor);
}

qreal shiftOntoScreen(qreal x, qreal width, const QRectF &screen)
{
    const qreal minShift = screen.x() - x;
    const qreal maxShift = (screen.x() + screen.width()) - (x + width);
    if (minShift > maxShift) {
        return minShift;
    }
    return std::clamp(0.0, minShift, maxShift);
}

qreal parkingScale(const QSizeF &original)
{
    return std::clamp(parkingMinSize / std::max(original.width(), original.height()), minScale, 1.0);
}

bool atParkingSize(qreal shownWidth, const QSizeF &original)
{
    return shownWidth / original.width() < parkingScale(original) + 0.02;
}

QSizeF layoutSize(const QSizeF &shown, const QSizeF &original, const QSizeF &appMin)
{
    if (!atParkingSize(shown.width(), original)) {
        return original;
    }
    const qreal k = std::max({parkingLayoutWidth / original.width(), appMin.width() / original.width(),
                              appMin.height() / original.height()});
    return k >= 1.0 ? original : (original * k).toSize();
}

int placeIndex(Place place)
{
    return int(std::find(std::begin(placeOrder), std::end(placeOrder), place) - std::begin(placeOrder));
}

Place placeOfFrame(const QRectF &frame, const QRectF &screen)
{
    const qreal zoneWidth = screen.width() * zoneFraction;
    auto share = [&](qreal x) {
        const qreal inter = std::min(frame.right(), x + zoneWidth) - std::max(frame.left(), x);
        const qreal uni = std::max(frame.right(), x + zoneWidth) - std::min(frame.left(), x);
        return std::max(0.0, inter) / uni;
    };
    const qreal mainShare = std::max(0.0, std::min(frame.right(), screen.x() + 3 * zoneWidth) - std::max(frame.left(), screen.x() + zoneWidth))
        / (std::max(frame.right(), screen.x() + 3 * zoneWidth) - std::min(frame.left(), screen.x() + zoneWidth));
    if (mainShare >= halfMatch) {
        return Place::Full;
    }
    if (share(screen.x() + zoneWidth) >= halfMatch) {
        return Place::HalfLeft;
    }
    if (share(screen.x() + 2 * zoneWidth) >= halfMatch) {
        return Place::HalfRight;
    }
    return Place::Free;
}

// Where a window of width `width` goes in `half` of main: centered in
// it, kept inside main (so a window wider than a half is against main's
// edge; one wider than main is centered in it).
static qreal mainStopX(Side half, qreal width, const QRectF &screen)
{
    const qreal zoneWidth = screen.width() * zoneFraction;
    const qreal left = screen.x() + zoneWidth;
    const qreal right = screen.x() + 3 * zoneWidth;
    if (width >= right - left) {
        return (left + right - width) / 2;
    }
    const qreal center = half == Side::Left ? left + zoneWidth / 2 : right - zoneWidth / 2;
    return std::clamp(center - width / 2, left, right - width);
}

std::optional<Side> nextMainStop(Side direction, const QRectF &frame, const QRectF &screen)
{
    // A pixel of slack: frames are whole pixels, the stops may not be.
    constexpr qreal slack = 1.0;
    std::optional<std::pair<qreal, Side>> next;
    for (const Side half : {Side::Left, Side::Right}) {
        const qreal x = mainStopX(half, frame.width(), screen);
        const bool ahead = direction == Side::Left ? x < frame.x() - slack : x > frame.x() + slack;
        const bool nearer = !next || (direction == Side::Left ? x > next->first : x < next->first);
        if (ahead && nearer) {
            next = std::pair{x, half};
        }
    }
    if (!next) {
        return std::nullopt;
    }
    return next->second;
}

QRectF mainStopRect(Side half, const QSizeF &size, qreal centerY, const QRectF &screen, const QRectF &area)
{
    const qreal height = std::min(size.height(), area.height());
    const qreal y = std::clamp(centerY - height / 2, area.y(), std::max(area.y(), area.y() + area.height() - height));
    return QRectF(mainStopX(half, size.width(), screen), y, size.width(), height);
}

Place parkedPlace(const QRectF &shown, const QSizeF &original, const QRectF &screen)
{
    const bool left = shown.center().x() < screen.x() + screen.width() / 2;
    if (atParkingSize(shown.width(), original)) {
        return left ? Place::ParkingLeft : Place::ParkingRight;
    }
    return left ? Place::StashLeft : Place::StashRight;
}

QRectF placeRect(Place place, const QSizeF &size, qreal centerY, const QRectF &screen, const QRectF &area, qreal stash)
{
    const qreal zoneWidth = screen.width() * zoneFraction;
    auto topFor = [&](qreal height) {
        return std::clamp(centerY - height / 2, area.y(), std::max(area.y(), area.y() + area.height() - height));
    };
    switch (place) {
    case Place::HalfLeft:
    case Place::HalfRight: {
        const qreal height = std::min(size.height(), area.height());
        const qreal x = screen.x() + (place == Place::HalfLeft ? zoneWidth : 2 * zoneWidth);
        return QRectF(QPointF(x, topFor(height)), QSizeF(zoneWidth, height));
    }
    case Place::Full: {
        const qreal height = std::min(size.height(), area.height());
        return QRectF(QPointF(screen.x() + zoneWidth, topFor(height)), QSizeF(2 * zoneWidth, height));
    }
    case Place::StashLeft:
    case Place::StashRight:
    case Place::ParkingLeft:
    case Place::ParkingRight: {
        // In a stash at most stashMaxWidth of the zone wide (wide windows,
        // e.g. from all of main, shrink more), but still above parking size.
        // At least stashMinSize, below parkBelow so it stays parked.
        const qreal stashFloor = std::max(parkingScale(size) + 0.03,
                                          std::min(stashMinSize / std::max(size.width(), size.height()), parkBelow - 0.01));
        const qreal scale = place == Place::StashLeft || place == Place::StashRight
            ? std::max(stashFloor, std::min(stash, zoneWidth * stashMaxWidth / size.width()))
            : parkingScale(size);
        const QSizeF drawn = size * scale;
        // A stash column is centered in its zone (whatever the windows'
        // widths, it lines up, and both sides keep some room); parking
        // is against the screen edge.
        const bool left = place == Place::StashLeft || place == Place::ParkingLeft;
        qreal x;
        if (place == Place::StashLeft || place == Place::StashRight) {
            const qreal center = left ? screen.x() + zoneWidth / 2 : screen.x() + screen.width() - zoneWidth / 2;
            x = center - drawn.width() / 2;
        } else {
            x = left ? screen.x() : screen.x() + screen.width() - drawn.width();
        }
        return QRectF(QPointF(x, topFor(drawn.height())), drawn);
    }
    case Place::Free:
        break;
    }
    return QRectF();
}

qreal fittingScale(const std::vector<qreal> &heights, qreal areaHeight)
{
    const qreal total = std::accumulate(heights.begin(), heights.end(), 0.0);
    const qreal room = areaHeight - arrangeGap * (int(heights.size()) - 1);
    return std::clamp(room / total, minScale + 0.03, stashScale);
}

QRectF lerpRect(const QRectF &a, const QRectF &b, qreal e)
{
    return QRectF(a.x() + (b.x() - a.x()) * e, a.y() + (b.y() - a.y()) * e,
                  a.width() + (b.width() - a.width()) * e, a.height() + (b.height() - a.height()) * e);
}

QPointF scaledTopLeft(const QPointF &topLeft, const QPointF &pos, const QSizeF &from, const QSizeF &to)
{
    return QPointF(pos.x() - (pos.x() - topLeft.x()) * to.width() / from.width(),
                   pos.y() - (pos.y() - topLeft.y()) * to.height() / from.height());
}

QRectF keptIn(const QRectF &box, const QRectF &area)
{
    const qreal x = std::clamp(box.x(), area.x(), std::max(area.x(), area.x() + area.width() - box.width()));
    const qreal y = std::clamp(box.y(), area.y(), std::max(area.y(), area.y() + area.height() - box.height()));
    return QRectF(QPointF(x, y), box.size());
}

std::vector<std::optional<QRectF>> spreadPiles(const std::vector<QRectF> &windows, const QRectF &screen)
{
    const auto area = [](const QRectF &r) {
        return r.width() * r.height();
    };
    // Join overlapping windows into piles (union-find).
    const int n = int(windows.size());
    std::vector<int> root(n);
    std::iota(root.begin(), root.end(), 0);
    const auto find = [&root](int i) {
        while (root[i] != i) {
            i = root[i] = root[root[i]];
        }
        return i;
    };
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const QRectF &a = windows[i];
            const QRectF &b = windows[j];
            const QRectF overlap = a & b;
            if (!overlap.isEmpty() && area(overlap) > pileOverlap * std::min(area(a), area(b))) {
                root[find(j)] = find(i);
            }
        }
    }
    std::map<int, std::vector<int>> piles; // members front first
    for (int i = 0; i < n; ++i) {
        piles[find(i)].push_back(i);
    }

    std::vector<std::optional<QRectF>> spread(n);
    // Lay out a row of windows between `left` and `right`, in the band
    // from `top` to `bottom`: centered, against the pile.
    const auto row = [&](const std::vector<int> &members, qreal left, qreal right, qreal top, qreal bottom,
                         bool above) {
        if (members.empty()) {
            return;
        }
        qreal width = 0;
        qreal height = 0;
        for (int m : members) {
            width += windows[m].width();
            height = std::max(height, windows[m].height());
        }
        const qreal gaps = spreadGap * (members.size() - 1);
        const qreal k = std::clamp(std::min((bottom - top) / height, (right - left - gaps) / width), spreadMinScale, 1.0);
        qreal x = (left + right) / 2 - (width * k + gaps) / 2;
        for (int m : members) {
            const QSizeF size = windows[m].size() * k;
            spread[m] = QRectF(QPointF(x, above ? bottom - size.height() : top), size);
            x += size.width() + spreadGap;
        }
    };
    for (const auto &[pile, members] : piles) {
        if (members.size() < 2) {
            continue;
        }
        QRectF bounds = windows[members.front()];
        for (int m : members) {
            bounds |= windows[m];
        }
        std::vector<int> above;
        std::vector<int> below;
        for (size_t i = 1; i < members.size(); ++i) {
            (i % 2 ? above : below).push_back(members[i]);
        }
        row(above, bounds.left(), bounds.right(), screen.top() + spreadGap, bounds.top() - spreadGap, true);
        row(below, bounds.left(), bounds.right(), bounds.bottom() + spreadGap, screen.bottom() - spreadGap, false);
    }
    return spread;
}

} // namespace glance
