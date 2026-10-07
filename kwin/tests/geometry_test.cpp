// Unit tests for Glance's geometry (geometry.h). No KWin needed:
// `ctest --test-dir kwin/build`, or run kwin/build/bin/geometry_test.
//
// The screen is this VM's (4004x1630 logical), with a panel at the bottom;
// each edge zone is a quarter of it (1001 px).
#include "geometry.h"

#include <QTest>
#include <QtMath>

using namespace glance;

namespace
{
const QRectF screen(0, 0, 4004, 1630);
const QRectF area(0, 0, 4004, 1580);
const qreal zone = 1001;

bool near(qreal a, qreal b, qreal tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

const Place sidePlaces[] = {Place::ParkingLeft, Place::StashLeft, Place::StashRight, Place::ParkingRight};
const Place mainPlaces[] = {Place::HalfLeft, Place::HalfRight, Place::Full};
const QSizeF sizes[] = {QSizeF(1600, 1000), QSizeF(3000, 1500), QSizeF(500, 300), QSizeF(2002, 1580)};
}

class GeometryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- Shrinking at the edges ---

    void edgeScaleIsOnTheCurve()
    {
        // The drawn edge, d = c - w * s from the screen edge, must be where
        // the curve puts it: s = minScale + (1 - minScale) * d / zone.
        for (const qreal c : {0.0, 100.0, 500.0, 900.0, 1500.0}) {
            for (const qreal w : {0.0, 50.0, 400.0, 1200.0}) {
                const qreal s = edgeScale(c, w, zone);
                const qreal d = c - w * s;
                QVERIFY2(near(s, minScale + (1 - minScale) * d / zone), qPrintable(QStringLiteral("c=%1 w=%2").arg(c).arg(w)));
            }
        }
    }

    void edgeScaleEnds()
    {
        // Cursor at the screen edge, nothing beyond it: minScale.
        QVERIFY(near(edgeScale(0, 0, zone), minScale));
        // The window's edge exactly at the zone's inner border: full size.
        QVERIFY(near(edgeScale(1401, 400, zone), 1.0));
        // Well inside main: more than 1 (callers cap it at 1).
        QVERIFY(edgeScale(2000, 500, zone) > 1.0);
    }

    void shiftOntoScreen_data()
    {
        QTest::addColumn<qreal>("x");
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("shift");
        QTest::newRow("inside") << 100.0 << 500.0 << 0.0;
        QTest::newRow("off left") << -50.0 << 500.0 << 50.0;
        QTest::newRow("off right") << 3800.0 << 500.0 << -296.0;
        QTest::newRow("wider than the screen keeps its left edge") << -100.0 << 5000.0 << 100.0;
    }
    void shiftOntoScreen()
    {
        QFETCH(qreal, x);
        QFETCH(qreal, width);
        QFETCH(qreal, shift);
        QCOMPARE(glance::shiftOntoScreen(x, width, screen), shift);
    }

    void parkingScaleLimits()
    {
        QCOMPARE(parkingScale(QSizeF(2000, 1000)), minScale); // 0.09 would be too small
        QVERIFY(near(parkingScale(QSizeF(1000, 800)), parkingMinSize / 1000)); // kept at parkingMinSize
        QVERIFY(near(parkingScale(QSizeF(211, 451)), parkingMinSize / 451)); // by the longer side
        QCOMPARE(parkingScale(QSizeF(150, 100)), 1.0); // never larger than full size
    }

    void heldScale_data()
    {
        QTest::addColumn<qreal>("rule");
        QTest::addColumn<qreal>("expected");
        // Grabbed when the rule gave 0.4, held at 0.6, parking at 0.15.
        QTest::newRow("starts at the held size") << 0.4 << 0.6;
        QTest::newRow("full size stays full size") << 1.0 << 1.0;
        QTest::newRow("parking size stays parking size") << 0.15 << 0.15;
        QTest::newRow("shrinks at once moving outward") << 0.3 << 0.15 + 0.45 * 0.6;
        QTest::newRow("grows moving inward") << 0.7 << 0.8;
    }
    void heldScale()
    {
        QFETCH(qreal, rule);
        QFETCH(qreal, expected);
        QVERIFY(qFuzzyCompare(glance::heldScale(rule, 0.4, 0.6, 0.15), expected));
    }

    void layoutSize_data()
    {
        QTest::addColumn<QSizeF>("shown");
        QTest::addColumn<QSizeF>("original");
        QTest::addColumn<QSizeF>("appMin");
        QTest::addColumn<QSizeF>("expected");
        QTest::newRow("stash: full size, only drawn smaller") << QSizeF(400, 250) << QSizeF(1600, 1000) << QSizeF() << QSizeF(1600, 1000);
        QTest::newRow("parking: a parkingLayoutWidth square") << QSizeF(140, 140) << QSizeF(1600, 1000) << QSizeF() << QSizeF(600, 600);
        QTest::newRow("parking: the app's minimum") << QSizeF(140, 140) << QSizeF(1600, 1000) << QSizeF(800, 0) << QSizeF(800, 600);
        QTest::newRow("parking: no larger than the original's longer side") << QSizeF(140, 100) << QSizeF(350, 250) << QSizeF() << QSizeF(350, 350);
    }
    void layoutSize()
    {
        QFETCH(QSizeF, shown);
        QFETCH(QSizeF, original);
        QFETCH(QSizeF, appMin);
        QFETCH(QSizeF, expected);
        QCOMPARE(glance::layoutSize(shown, original, appMin), expected);
    }

    // --- Parking tiles ---

    void parkingTileRect()
    {
        // parkingTile on the longer side, in the window's shape, against
        // the screen edge, centered on centerY.
        const QRectF square = glance::parkingTileRect(Side::Left, QSizeF(600, 600), QSizeF(1600, 1000), 800, screen, area);
        QCOMPARE(square, QRectF(0, 800 - parkingTile / 2, parkingTile, parkingTile));
        const QRectF tall = glance::parkingTileRect(Side::Right, QSizeF(600, 800), QSizeF(1600, 1000), 800, screen, area);
        QCOMPARE(tall.height(), parkingTile);
        QCOMPARE(tall.width(), parkingTile * 0.75);
        QCOMPARE(tall.right(), 4004.0);
        // Kept in the area.
        QCOMPARE(glance::parkingTileRect(Side::Left, QSizeF(600, 600), QSizeF(1600, 1000), 1600, screen, area).bottom(), area.bottom());
        // About 10 fit along the edge.
        QVERIFY(10 * parkingTile + 9 * arrangeGap <= area.height());
        // A window smaller than a tile at parking size is no larger than
        // that (so it still counts as parked, not stashed).
        const QSizeF tiny(130, 100);
        const QRectF small = glance::parkingTileRect(Side::Left, tiny, tiny, 800, screen, area);
        QCOMPARE(glance::parkedPlace(small, tiny, screen), Place::ParkingLeft);
    }

    void tiltTransform()
    {
        const qreal eyeY = area.center().y();
        const qreal distance = tiltDistance * area.height();
        const QRectF tile(0, 200, 140, 140);
        // Flat: nothing changes.
        QVERIFY(glance::tiltTransform(tile, Side::Left, 0, eyeY, distance).isIdentity());
        for (const Side side : {Side::Left, Side::Right}) {
            const bool left = side == Side::Left;
            const QRectF rect = left ? tile : tile.translated(4004 - 140, 0);
            const QTransform t = glance::tiltTransform(rect, side, 1, eyeY, distance);
            // The outer edge stays where it is.
            for (const QPointF &corner : {left ? rect.topLeft() : rect.topRight(), left ? rect.bottomLeft() : rect.bottomRight()}) {
                QVERIFY(near(t.map(corner).x(), corner.x(), 1e-9) && near(t.map(corner).y(), corner.y(), 1e-9));
            }
            // The inner edge recedes: shorter, nearer the outer edge.
            const QPointF innerTop = t.map(left ? rect.topRight() : rect.topLeft());
            const QPointF innerBottom = t.map(left ? rect.bottomRight() : rect.bottomLeft());
            QVERIFY(innerBottom.y() - innerTop.y() < rect.height());
            QVERIFY(std::abs(innerTop.x() - rect.left() - (left ? 0 : rect.width())) < rect.width() * std::cos(qDegreesToRadians(tiltAngle)));
            // Input maps back exactly.
            const QPointF inside(rect.x() + 100, rect.y() + 30);
            const QPointF back = t.inverted().map(t.map(inside));
            QVERIFY(near(back.x(), inside.x(), 1e-6) && near(back.y(), inside.y(), 1e-6));
        }
    }

    // A column is one plane: the edges of all its tiles (above and below
    // the eye) run to one vanishing point, so the gaps between tiles stay
    // gaps (nearly parallel), not Vs.
    void tiltedColumnIsOnePlane()
    {
        const qreal eyeY = area.center().y();
        const qreal distance = tiltDistance * area.height();
        std::optional<QPointF> vanishing;
        qreal previousInnerBottom = -1;
        for (qreal top = 100; top < 1500; top += parkingTile + arrangeGap) {
            const QRectF rect(0, top, parkingTile, parkingTile);
            const QTransform t = glance::tiltTransform(rect, Side::Left, 1, eyeY, distance);
            const QPointF innerTop = t.map(rect.topRight());
            const QPointF innerBottom = t.map(rect.bottomRight());
            // The gap above this tile is still open at the inner side.
            QVERIFY(innerTop.y() > previousInnerBottom);
            previousInnerBottom = innerBottom.y();
            // Where the top edge's line reaches the eye's level.
            const QPointF outer = rect.topLeft();
            if (std::abs(outer.y() - eyeY) < 1) {
                continue;
            }
            const qreal x = outer.x() + (innerTop.x() - outer.x()) * (eyeY - outer.y()) / (innerTop.y() - outer.y());
            if (!vanishing) {
                vanishing = QPointF(x, eyeY);
            }
            QVERIFY(near(x, vanishing->x(), 1e-6));
        }
    }

    // --- Places ---

    void placeOrderSteps()
    {
        QCOMPARE(placeIndex(Place::ParkingLeft), 0);
        QCOMPARE(placeIndex(Place::ParkingRight), 5);
        QCOMPARE(placeOrder[placeIndex(Place::HalfLeft) + 1], Place::HalfRight);
    }

    void placeOfFrame_data()
    {
        QTest::addColumn<QRectF>("frame");
        QTest::addColumn<Place>("place");
        QTest::newRow("all of main") << QRectF(zone, 100, 2 * zone, 800) << Place::Full;
        QTest::newRow("left half") << QRectF(zone, 100, zone, 800) << Place::HalfLeft;
        QTest::newRow("right half") << QRectF(2 * zone, 100, zone, 800) << Place::HalfRight;
        QTest::newRow("left half, a bit moved") << QRectF(zone + 49, 100, zone, 800) << Place::HalfLeft;
        QTest::newRow("small window") << QRectF(100, 100, 500, 500) << Place::Free;
        QTest::newRow("between the halves") << QRectF(1.5 * zone, 100, zone, 800) << Place::Free;
    }
    void placeOfFrame()
    {
        QFETCH(QRectF, frame);
        QFETCH(Place, place);
        QCOMPARE(glance::placeOfFrame(frame, screen), place);
    }

    // Meta+Left/Right in main: centered in a half, always in the arrow's
    // direction, never resizing; past the last stop, nothing (on to the
    // stash).
    void nextMainStop()
    {
        const qreal leftStop = zone + zone / 2 - 250; // a 500-px window
        const qreal rightStop = 2 * zone + zone / 2 - 250;
        const QRectF small(1500, 100, 500, 400);
        QCOMPARE(glance::nextMainStop(Side::Left, small, screen), Side::Left);
        QCOMPARE(glance::nextMainStop(Side::Right, small, screen), Side::Right);
        const QRectF atLeft(leftStop, 100, 500, 400);
        QCOMPARE(glance::nextMainStop(Side::Left, atLeft, screen), std::nullopt);
        QCOMPARE(glance::nextMainStop(Side::Right, atLeft, screen), Side::Right);
        const QRectF atRight(rightStop, 100, 500, 400);
        QCOMPARE(glance::nextMainStop(Side::Left, atRight, screen), Side::Left);
        QCOMPARE(glance::nextMainStop(Side::Right, atRight, screen), std::nullopt);
        // In the left stash zone: Meta+Right brings it into the left half,
        // Meta+Left goes on into the stash.
        const QRectF inStash(300, 100, 500, 400);
        QCOMPARE(glance::nextMainStop(Side::Right, inStash, screen), Side::Left);
        QCOMPARE(glance::nextMainStop(Side::Left, inStash, screen), std::nullopt);
        // As wide as main: one stop, straight to a stash.
        const QRectF full(zone, 100, 2 * zone, 800);
        QCOMPARE(glance::nextMainStop(Side::Left, full, screen), std::nullopt);
        QCOMPARE(glance::nextMainStop(Side::Right, full, screen), std::nullopt);
    }

    void mainStopRect()
    {
        const QSizeF size(500, 400);
        const QRectF left = glance::mainStopRect(Side::Left, size, 800, screen, area);
        QCOMPARE(left, QRectF(zone + zone / 2 - 250, 600, 500, 400));
        QCOMPARE(glance::mainStopRect(Side::Right, size, 800, screen, area).center().x(), 2.5 * zone);
        QCOMPARE(glance::nextMainStop(Side::Left, left, screen), std::nullopt);
        // Wider than a half: against main's edge, inside main.
        const QSizeF wide(1500, 800);
        QCOMPARE(glance::mainStopRect(Side::Left, wide, 800, screen, area).left(), zone);
        QCOMPARE(glance::mainStopRect(Side::Right, wide, 800, screen, area).right(), 3 * zone);
    }

    void parkedPlace()
    {
        const QSizeF original(1600, 1000);
        const QSizeF tiny = original * parkingScale(original);
        const QSizeF half = original * 0.5;
        QCOMPARE(glance::parkedPlace(QRectF(QPointF(0, 100), tiny), original, screen), Place::ParkingLeft);
        QCOMPARE(glance::parkedPlace(QRectF(QPointF(4004 - tiny.width(), 100), tiny), original, screen), Place::ParkingRight);
        QCOMPARE(glance::parkedPlace(QRectF(QPointF(100, 100), half), original, screen), Place::StashLeft);
        QCOMPARE(glance::parkedPlace(QRectF(QPointF(3200, 100), half), original, screen), Place::StashRight);
    }

    // What placeRect gives is recognised again as the same place: the
    // keyboard ladder depends on it.
    void placeRectRoundTrip()
    {
        for (const QSizeF &size : sizes) {
            for (const Place place : mainPlaces) {
                const QRectF rect = placeRect(place, size, 800, screen, area);
                QCOMPARE(glance::placeOfFrame(rect, screen), place);
            }
            for (const Place place : sidePlaces) {
                const QRectF rect = placeRect(place, size, 800, screen, area);
                QCOMPARE(glance::parkedPlace(rect, size, screen), place);
            }
        }
    }

    void placeRectPositions()
    {
        const QSizeF size(1600, 1000);
        // Parking against the screen edges, stashes centered in their zone.
        QCOMPARE(placeRect(Place::ParkingLeft, size, 800, screen, area).left(), 0.0);
        QCOMPARE(placeRect(Place::ParkingRight, size, 800, screen, area).right(), 4004.0);
        QCOMPARE(placeRect(Place::StashLeft, size, 800, screen, area).center().x(), zone / 2);
        QCOMPARE(placeRect(Place::StashRight, size, 800, screen, area).center().x(), 4004 - zone / 2);
        // Halves and all of main: exactly their part of main.
        QCOMPARE(placeRect(Place::HalfLeft, size, 800, screen, area).left(), zone);
        QCOMPARE(placeRect(Place::HalfRight, size, 800, screen, area).width(), zone);
        QCOMPARE(placeRect(Place::Full, size, 800, screen, area).width(), 2 * zone);
        QVERIFY(placeRect(Place::Free, size, 800, screen, area).isNull());
    }

    void placeRectStaysInTheArea()
    {
        for (const QSizeF &size : sizes) {
            for (const Place place : {Place::ParkingLeft, Place::StashLeft, Place::HalfLeft, Place::Full}) {
                for (const qreal centerY : {0.0, 800.0, 1630.0}) {
                    const QRectF rect = placeRect(place, size, centerY, screen, area);
                    QVERIFY(rect.top() >= area.top());
                    QVERIFY(rect.bottom() <= area.bottom() + 1e-6);
                }
            }
        }
        // Keeps its vertical center where there is room.
        QCOMPARE(placeRect(Place::StashLeft, QSizeF(1600, 1000), 800, screen, area).center().y(), 800.0);
    }

    void stashScaleLimits()
    {
        // A wide window takes at most stashMaxWidth of the zone...
        QVERIFY(placeRect(Place::StashLeft, QSizeF(3000, 1500), 800, screen, area).width() <= zone * stashMaxWidth + 1e-6);
        // ... a normal one stashScale.
        QCOMPARE(placeRect(Place::StashLeft, QSizeF(1000, 800), 800, screen, area).width(), 1000 * stashScale);
        // A small one at least stashMinSize on its longer side...
        QCOMPARE(placeRect(Place::StashLeft, QSizeF(321, 108), 800, screen, area).width(), stashMinSize);
        QCOMPARE(placeRect(Place::StashLeft, QSizeF(211, 451), 800, screen, area).height(), stashMinSize);
        // ... or nearly its full size, if smaller (still a stash).
        const QSizeF tiny(200, 100);
        const QRectF stashed = placeRect(Place::StashLeft, tiny, 800, screen, area);
        QVERIFY(stashed.width() < 200 && stashed.width() > 190);
        QCOMPARE(glance::parkedPlace(stashed, tiny, screen), Place::StashLeft);
    }

    void fittingScale()
    {
        QCOMPARE(glance::fittingScale({1000}, 1580), stashScale); // room to spare
        QCOMPARE(glance::fittingScale({1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000}, 1580),
                 minScale + 0.03); // too many: still a stash
        QVERIFY(near(glance::fittingScale({1000, 1000, 1000, 1000}, 1580), (1580 - 3 * arrangeGap) / 4000));
    }

    // --- Rectangles ---

    void rectHelpers()
    {
        const QRectF a(0, 0, 100, 100);
        const QRectF b(100, 50, 300, 200);
        QCOMPARE(lerpRect(a, b, 0), a);
        QCOMPARE(lerpRect(a, b, 1), b);
        QCOMPARE(lerpRect(a, b, 0.5), QRectF(50, 25, 200, 150));

        // The point under `pos` stays there.
        QCOMPARE(scaledTopLeft(QPointF(0, 0), QPointF(100, 100), QSizeF(200, 200), QSizeF(100, 100)), QPointF(50, 50));

        QCOMPARE(keptIn(QRectF(100, 100, 200, 200), area), QRectF(100, 100, 200, 200));
        QCOMPARE(keptIn(QRectF(3900, 1500, 200, 200), area), QRectF(3804, 1380, 200, 200));
        QCOMPARE(keptIn(QRectF(-50, -50, 5000, 2000), area).topLeft(), area.topLeft());
    }

    // --- The Alt+Tab map ---

    void spreadPilesLeavesSeparateWindows()
    {
        const auto spread = spreadPiles({QRectF(100, 400, 400, 300), QRectF(1000, 400, 400, 300)}, screen);
        QVERIFY(!spread[0] && !spread[1]);
        // Overlapping by less than pileOverlap: not a pile.
        const auto slight = spreadPiles({QRectF(100, 400, 400, 300), QRectF(480, 400, 400, 300)}, screen);
        QVERIFY(!slight[0] && !slight[1]);
    }

    void spreadPilesSpreadsAPile()
    {
        const QRectF pile(1000, 600, 800, 400);
        const auto spread = spreadPiles({pile, pile, pile}, screen);
        QVERIFY(!spread[0]); // the front one stays
        QVERIFY(spread[1] && spread[2]);
        QVERIFY(spread[1]->bottom() <= pile.top() - spreadGap + 1e-6); // above
        QVERIFY(spread[2]->top() >= pile.bottom() + spreadGap - 1e-6); // below
        for (const int i : {1, 2}) {
            QVERIFY(screen.contains(*spread[i]));
            QVERIFY(spread[i]->left() >= pile.left() - 1e-6 && spread[i]->right() <= pile.right() + 1e-6);
        }
    }
};

QTEST_GUILESS_MAIN(GeometryTest)

#include "geometry_test.moc"
