// Clips: text or an image dropped on the desktop, or clipped with Meta+C,
// becomes a glance-clip window (kwin/clip) instead of Plasma's sticky-note
// widget; dragging a clip's body drags its data back out into apps, drawn
// as if the note moved (docs/clips.md).
#pragma once

#include "parked.h"

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QSocketNotifier>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>

namespace KWin
{
class AbstractDataSource;
struct PointerButtonEvent;
class ScreenPrePaintData;
class WindowPrePaintData;
class WindowPaintData;
}

namespace glance
{

class Clips : public QObject
{
    Q_OBJECT

public:
    // `lastPress`: where the last pointer button press was (kept by the
    // effect), where a clip drag grabs the clip.
    Clips(ParkedWindows &parking, const QPointF &lastPress);
    ~Clips() override;

    // A clip is being dragged (and drawn under the pointer).
    bool dragging() const;
    // Pointer motion during a clip drag: the clip follows (KWin's drag
    // and drop goes on).
    void follow(const QPointF &pos);
    // A left-button release: a text or image drag released over the
    // desktop (or the parking band) becomes a clip, a dragged clip moves
    // there. Returns whether the release was taken.
    bool drop(KWin::PointerButtonEvent *event);

    // A window appeared: the clip app's, put where the clip goes.
    void placeClip(Window *window);
    // A window's frame changed: a clip in parking takes the height its
    // text needs.
    void frameChanged(Window *window);
    void closed(Window *window);

    // Painting a dragged clip under the pointer. paintWindow returns
    // whether a clip drag is on: then every window is painted with a
    // finite region (see Glance::paintWindow).
    void prePaintScreen(KWin::ScreenPrePaintData &data);
    void prePaintWindow(Window *window, KWin::WindowPrePaintData &data);
    bool paintWindow(Window *window, KWin::WindowPaintData &data);

private:
    // Text or an image being turned into a clip (see startClip): the type
    // asked for, the data read so far, where the clip goes, and for a drop
    // the held-back release.
    struct ClipRead
    {
        int fd = -1;
        std::unique_ptr<QSocketNotifier> notifier = nullptr;
        QString mimeType = {};
        QByteArray text = {};
        // More than clipMaxBytes came: given up on.
        bool tooLarge = false;
        QPointF position;
        std::optional<Place> place = std::nullopt;
        bool fromDrag = false;
        quint32 nativeButton = 0;
        std::chrono::microseconds timestamp = {};
    };
    // A clip being dragged (see dragStarted): its window, where it was
    // grabbed (fraction of its drawn size), its full size, its shape, where
    // it is drawn now, and whether it was dropped on an app that took it
    // (with Shift: copied).
    struct ClipDrag
    {
        QPointer<Window> window;
        QPointF grab;
        QSizeF original;
        qreal aspect = 1;
        QRectF ghost;
        bool dropped = false;
        bool copy = false;
    };

    void clipSelection();
    static QString clipMimeType(const QStringList &types, bool images);
    bool startClip(KWin::AbstractDataSource *source, const QString &mimeType, ClipRead clip);
    void readClip();
    void finishClip();
    void openClip(const QString &path, const QPointF &position, std::optional<Place> place);
    static QString droppedImageFile(const QByteArray &uriList);
    static QString clipApp();
    static Window *clipWindowOf(KWin::AbstractDataSource *source);

    void dragStarted();
    QRectF ghostRect(const QPointF &cursor) const;
    void placeDroppedClip(const QPointF &pos);
    void dragEnded();
    void slideBack();
    static std::optional<Place> parkingSide(const QPointF &pos);

    ParkedWindows &m_parking;
    const QPointF &m_lastPress;
    std::unique_ptr<ClipRead> m_clip;
    std::optional<ClipDrag> m_drag;
    // The clip app started for the last clip, where its window goes, and
    // whether it goes to parking (see placeClip).
    qint64 m_clipPid = 0;
    QPointF m_clipPosition;
    std::optional<Place> m_clipPlace;
};

} // namespace glance
