// Clips (see clips.h).
#include "clips.h"

#include "geometry.h"

#include <core/output.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <input.h>
#include <input_event.h>
#include <main.h>
#include <pointer_input.h>
#include <utils/filedescriptor.h>
#include <wayland/abstract_data_source.h>
#include <wayland/clientconnection.h>
#include <wayland/seat.h>
#include <wayland/surface.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <KGlobalAccel>

#include <QAction>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

using namespace KWin;

namespace glance
{

namespace
{

// Save a clip's data as a new file in ~/Clips, or copy the dropped image
// file there (closing the clip deletes its file). Returns its path, or
// empty. Runs on a worker thread: an image can be large, and KWin's main
// thread paints every frame and handles all input.
QString saveClip(const QByteArray &data, const QString &imageFile, const QString &suffix)
{
    const QDir dir(QDir::home().filePath(QLatin1String(clipsFolder)));
    if (!dir.mkpath(QStringLiteral("."))) {
        qWarning("glance: clip: can't create %s", qPrintable(dir.path()));
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    QString path = dir.filePath(stamp + QLatin1Char('.') + suffix);
    for (int i = 2; QFile::exists(path); ++i) {
        path = dir.filePath(QStringLiteral("%1 (%2).%3").arg(stamp).arg(i).arg(suffix));
    }
    if (!imageFile.isEmpty()) {
        if (!QFile::copy(imageFile, path)) {
            qWarning("glance: clip: can't copy %s to %s", qPrintable(imageFile), qPrintable(path));
            return {};
        }
        return path;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        qWarning("glance: clip: can't write %s", qPrintable(path));
        return {};
    }
    return path;
}

} // namespace

Clips::Clips(ParkedWindows &parking, const QPointF &lastPress)
    : m_parking(parking)
    , m_lastPress(lastPress)
{
    auto seat = waylandServer()->seat();
    connect(seat, &SeatInterface::dragStarted, this, &Clips::dragStarted);
    connect(seat, &SeatInterface::dragDropped, this, [this]() {
        if (m_drag) {
            m_drag->dropped = true;
            m_drag->copy = input()->keyboardModifiers() & Qt::ShiftModifier;
        }
    });
    connect(seat, &SeatInterface::dragEnded, this, &Clips::dragEnded);

    auto clipAction = new QAction(this);
    clipAction->setObjectName(QStringLiteral("Glance Clip Selection"));
    clipAction->setText(QStringLiteral("Glance: Clip the Selected Text"));
    KGlobalAccel::self()->setGlobalShortcut(clipAction, QKeySequence(Qt::META | Qt::Key_C));
    connect(clipAction, &QAction::triggered, this, &Clips::clipSelection);
}

Clips::~Clips()
{
    if (m_clip) {
        m_clip->notifier.reset();
        close(m_clip->fd);
    }
}

bool Clips::dragging() const
{
    return m_drag.has_value();
}

void Clips::follow(const QPointF &pos)
{
    if (!m_drag->dropped) {
        m_drag->ghost = ghostRect(pos);
        effects->addRepaintFull();
    }
}

// --- Text dropped on the desktop ---

// Rather than letting Plasma make a sticky-note widget of it, ask the
// dragging app for the data and hold the release back. Once it is in
// (finishClip), the drag is cancelled, so nothing is dropped anywhere, and
// the release passed on. Dragged files: a single image file becomes a clip,
// anything else is dropped on Plasma after all; links without image data
// are left to Plasma.
bool Clips::drop(PointerButtonEvent *event)
{
    auto seat = waylandServer()->seat();
    if (m_clip || event->button != Qt::LeftButton || !seat->isDragPointer() || !seat->dragSource()) {
        return false;
    }
    // (A dragged clip is drawn under the pointer, not where it was.)
    Window *under = m_parking.pick(event->position, m_drag ? m_drag->window.data() : nullptr);
    if (under && !under->isDesktop() && !(m_parking.isIcon(under) && parkingSide(event->position))) {
        return false;
    }
    const QStringList types = seat->dragSource()->mimeTypes();
    const QString mimeType = clipMimeType(types, true);
    if (!m_drag) {
        qInfo("glance: clip: drop offers %s", qPrintable(types.join(QLatin1Char(' '))));
    }
    if (mimeType.isEmpty()) {
        return false;
    }
    if (m_drag) {
        // A clip dropped where text would become a new clip: it moves
        // there instead (see placeDroppedClip). The drag is cancelled,
        // so Plasma makes no note of it.
        placeDroppedClip(event->position);
        seat->cancelDrag();
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerButton(event->nativeButton, event->state);
        seat->notifyPointerFrame();
        return true;
    }
    return startClip(seat->dragSource(), mimeType,
                     ClipRead{.position = event->position,
                              .place = parkingSide(event->position),
                              .fromDrag = true,
                              .nativeButton = event->nativeButton,
                              .timestamp = event->timestamp});
}

// Meta+C: clip the text selected in the active window (the primary
// selection, if that window's app owns it: the primary selection outlives
// the highlight and may belong to another app) into parking on the side
// nearer the window.
void Clips::clipSelection()
{
    Window *window = workspace()->activeWindow();
    AbstractDataSource *source = waylandServer()->seat()->primarySelection();
    if (m_clip || !window || !window->surface() || !source) {
        return;
    }
    if (source->client() != window->surface()->client()->client()) {
        qInfo("glance: clip: no text selected in %s", qPrintable(window->caption()));
        return;
    }
    const QString mimeType = clipMimeType(source->mimeTypes(), false);
    if (mimeType.isEmpty()) {
        return;
    }
    const QRectF drawn = m_parking.currentlyDrawn(window);
    const RectF screen = window->output()->geometryF();
    const bool left = drawn.center().x() < screen.x() + screen.width() / 2;
    startClip(source, mimeType,
              ClipRead{.position = drawn.center(), .place = left ? Place::ParkingLeft : Place::ParkingRight});
}

// What to ask for of `types` to make a clip, or empty: with `images`, image
// data first (an image dragged out of a browser comes with its link too),
// then a file list (finishClip takes it only if it is one image file); then
// plain text, unless it comes with a file list or link (text/uri-list).
QString Clips::clipMimeType(const QStringList &types, bool images)
{
    if (images) {
        if (types.contains(QStringLiteral("image/png"))) {
            return QStringLiteral("image/png");
        }
        const QList<QByteArray> readable = QImageReader::supportedMimeTypes();
        for (const QString &type : types) {
            if (type.startsWith(QLatin1String("image/")) && readable.contains(type.toLatin1())) {
                return type;
            }
        }
    }
    if (types.contains(QStringLiteral("text/uri-list"))) {
        return images ? QStringLiteral("text/uri-list") : QString();
    }
    for (const char *type : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"}) {
        if (types.contains(QLatin1String(type))) {
            return QLatin1String(type);
        }
    }
    return {};
}

// Ask `source` for its text or image; it arrives in readClip, and
// finishClip makes the clip. Returns whether it started.
bool Clips::startClip(AbstractDataSource *source, const QString &mimeType, ClipRead clip)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        return false;
    }
    // The app writes into fds[1] (our copy is closed once sent) until it
    // closes it; we read fds[0] as data arrives.
    source->requestData(mimeType, FileDescriptor(fds[1]));
    clip.fd = fds[0];
    clip.mimeType = mimeType;
    clip.notifier = std::make_unique<QSocketNotifier>(fds[0], QSocketNotifier::Read);
    m_clip = std::make_unique<ClipRead>(std::move(clip));
    connect(m_clip->notifier.get(), &QSocketNotifier::activated, this, &Clips::readClip);
    // Images take longer: the app may encode them first.
    const auto timeout = mimeType.startsWith(QLatin1String("image/")) ? clipImageTimeout : clipTimeout;
    QTimer::singleShot(timeout, this, [this, fd = fds[0]]() {
        if (m_clip && m_clip->fd == fd) {
            qWarning("glance: clip: the app took too long to hand over the data");
            finishClip();
        }
    });
    return true;
}

void Clips::readClip()
{
    char buffer[4096];
    while (m_clip) {
        const ssize_t n = read(m_clip->fd, buffer, sizeof(buffer));
        if (n > 0 && m_clip->text.size() + n > clipMaxBytes) {
            qWarning("glance: clip: more than %lld MB of %s, given up", qlonglong(clipMaxBytes >> 20),
                     qPrintable(m_clip->mimeType));
            m_clip->tooLarge = true;
            m_clip->text.clear();
            finishClip();
        } else if (n > 0) {
            m_clip->text.append(buffer, n);
        } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            return; // more to come
        } else {
            finishClip(); // end of data, or an error
        }
    }
}

// All data read (or given up): for a drop, end the drag and pass the
// release on; save the clip (see saveClip) and open it. A dragged file that
// isn't a single image is dropped where it was going after all (on
// Plasma).
void Clips::finishClip()
{
    const std::unique_ptr<ClipRead> clip = std::move(m_clip);
    // We may be inside the notifier's own signal: delete it later.
    clip->notifier->setEnabled(false);
    clip->notifier.release()->deleteLater();
    close(clip->fd);

    QString imageFile;
    if (clip->mimeType == QLatin1String("text/uri-list")) {
        imageFile = droppedImageFile(clip->text);
    }
    const bool image = clip->mimeType.startsWith(QLatin1String("image/"));
    const bool ours = image || !imageFile.isEmpty() || clip->mimeType != QLatin1String("text/uri-list");

    if (clip->fromDrag) {
        auto seat = waylandServer()->seat();
        if (ours) {
            seat->cancelDrag();
        }
        seat->setTimestamp(clip->timestamp);
        seat->notifyPointerButton(clip->nativeButton, PointerButtonState::Released);
        seat->notifyPointerFrame();
    }
    if (!ours || clip->tooLarge) {
        return;
    }

    // Only the image's header is read, not the whole image decoded.
    QBuffer imageData(&clip->text);
    if (image && (!imageData.open(QIODevice::ReadOnly) || !QImageReader(&imageData).canRead())) {
        qWarning("glance: clip: no image received (%s, %lld bytes)", qPrintable(clip->mimeType), qlonglong(clip->text.size()));
        return;
    }
    if (!image && imageFile.isEmpty() && clip->text.trimmed().isEmpty()) {
        qWarning("glance: clip: no text received");
        return;
    }
    QString suffix = QStringLiteral("txt");
    if (image) {
        suffix = QMimeDatabase().mimeTypeForName(clip->mimeType).preferredSuffix();
    } else if (!imageFile.isEmpty()) {
        suffix = QFileInfo(imageFile).suffix().toLower();
    }
    const QString what = imageFile.isEmpty() ? QStringLiteral("%1 bytes of %2").arg(clip->text.size()).arg(clip->mimeType) : imageFile;
    QtConcurrent::run(saveClip, clip->text, imageFile, suffix)
        .then(this, [this, what, position = clip->position, place = clip->place](const QString &path) {
            if (!path.isEmpty()) {
                qInfo("glance: clip: %s -> %s", qPrintable(what), qPrintable(path));
                openClip(path, position, place);
            }
        });
}

// Open a saved clip in glance-clip; its window goes to `position`, or to
// the parking column `place` (see placeClip).
void Clips::openClip(const QString &path, const QPointF &position, std::optional<Place> place)
{
    // KWin's own environment for the apps it starts, without the plugin
    // path that loads this effect from the build folder. Started in its own
    // systemd scope in app.slice, like apps Plasma starts: otherwise it
    // would belong to KWin's service, be stopped in an odd order at logout
    // and die with KWin. systemd-run --scope execs the app in its own
    // process, so the pid is the app's (see placeClip).
    QProcessEnvironment env = kwinApp()->processStartupEnvironment();
    env.remove(QStringLiteral("QT_PLUGIN_PATH"));
    const QString unit = QStringLiteral("app-%1-%2.scope").arg(QLatin1String(clipAppId)).arg(QDateTime::currentMSecsSinceEpoch());
    QProcess process;
    process.setProgram(QStringLiteral("systemd-run"));
    process.setArguments({QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--slice=app.slice"),
                          QStringLiteral("--unit=") + unit, QStringLiteral("--collect"), QStringLiteral("--quiet"),
                          QStringLiteral("--"), clipApp(), path});
    process.setProcessEnvironment(env);
    qint64 pid = 0;
    if (!process.startDetached(&pid)) {
        qWarning("glance: clip: can't start %s", qPrintable(clipApp()));
        return;
    }
    m_clipPid = pid;
    m_clipPosition = position;
    m_clipPlace = place;
}

// The local image file a dropped file list (text/uri-list: one URL per
// line, # comments) holds, if it holds just one, else empty.
QString Clips::droppedImageFile(const QByteArray &uriList)
{
    QStringList files;
    for (const QByteArray &line : uriList.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        const QUrl url(QString::fromUtf8(trimmed));
        if (!url.isLocalFile()) {
            return {};
        }
        files.append(url.toLocalFile());
    }
    if (files.size() != 1 || !QImageReader(files.first()).canRead()) {
        return {};
    }
    return files.first();
}

// The clip app: the one built with this plugin (bin/glance-clip in the
// build folder, three levels above the plugin's bin/kwin/effects/plugins),
// else the installed one, from PATH.
QString Clips::clipApp()
{
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&Clips::clipApp), &info) && info.dli_fname) {
        const QString besidePlugin = QFileInfo(QFile::decodeName(info.dli_fname)).dir().filePath(QStringLiteral("../../../glance-clip"));
        if (QFileInfo(besidePlugin).isExecutable()) {
            return QFileInfo(besidePlugin).canonicalFilePath();
        }
    }
    return QStringLiteral("glance-clip");
}

// A clip in parking resizes itself to the height its text needs (see
// resizeEvent in kwin/clip/main.cpp): take it (drawn at 1/2, see
// layoutSize) and re-form its column.
void Clips::frameChanged(Window *window)
{
    auto *it = m_parking.find(window);
    if (!it || it->restoring || !isClipInParking(window, it->shown.width(), it->original)) {
        return;
    }
    const RectF frame = window->frameGeometry();
    const qreal scale = it->shown.width() / frame.width();
    const qreal height = frame.height() * scale;
    if (std::abs(height - it->shown.height()) > 0.5) {
        it->shown.setHeight(height);
        m_parking.arrange(window);
    }
}

// The clip window a drag comes from, if any.
Window *Clips::clipWindowOf(AbstractDataSource *source)
{
    if (!source) {
        return nullptr;
    }
    for (Window *window : workspace()->windows()) {
        if (isClip(window) && window->surface() && window->surface()->client()->client() == source->client()) {
            return window;
        }
    }
    return nullptr;
}

// The clip's window appeared: put it where the text was dropped, as if it
// had been dragged there held at its center and dropped: full size in
// main, shrunk by the edge rule (see edgeScale) and parked if an edge went
// into an edge zone.
void Clips::placeClip(Window *window)
{
    if (!m_clipPid || window->pid() != m_clipPid || !window->isNormalWindow() || !window->windowItem()) {
        return;
    }
    m_clipPid = 0;
    const QPointF pos = m_clipPosition;
    const RectF screen = window->output()->geometryF();
    const RectF area = workspace()->clientArea(MaximizeArea, window);
    const RectF frame = window->moveResizeGeometry();
    const QSizeF size(frame.width(), frame.height());
    if (m_clipPlace) {
        // Dropped in the parking band: a parking icon in that column.
        m_parking.commitPlace(window, *m_clipPlace, size, pos.y(), m_parking.currentlyDrawn(window));
        workspace()->activateWindow(window);
        return;
    }
    const qreal zoneWidth = screen.width() * zoneFraction;
    const qreal scale = std::max(parkingScale(size), std::min({1.0,
                                                     edgeScale(pos.x() - screen.x(), size.width() / 2, zoneWidth),
                                                     edgeScale(screen.x() + screen.width() - pos.x(), size.width() / 2, zoneWidth)}));
    const QSizeF drawn = size * scale;
    const qreal left = pos.x() - drawn.width() / 2;
    const qreal x = left + shiftOntoScreen(left, drawn.width(), screen);
    const qreal y = std::clamp(pos.y() - drawn.height() / 2, area.y(), std::max(area.y(), area.y() + area.height() - drawn.height()));
    if (scale < parkBelow) {
        const QRectF from = m_parking.currentlyDrawn(window);
        m_parking.park(window, QRectF(QPointF(x, y), drawn), size);
        m_parking.animate(window, from);
        m_parking.arrange(window);
    } else {
        window->move(QPointF(x, y));
    }
    workspace()->activateWindow(window);
}

void Clips::closed(Window *window)
{
    if (m_drag && m_drag->window == window) {
        m_drag.reset(); // pasted: the clip is gone
        effects->addRepaintFull();
    }
}

// --- Dragging a clip ---

void Clips::dragStarted()
{
    Window *window = clipWindowOf(waylandServer()->seat()->dragSource());
    if (!window || !window->windowItem()) {
        return;
    }
    if (m_parking.isParked(window)) {
        // Hover previews notice (see HoverPreviews::current).
        m_parking.at(window).preview.reset();
    }
    const QRectF drawn = m_parking.currentlyDrawn(window);
    auto *it = m_parking.find(window);
    const RectF frame = window->frameGeometry();
    ClipDrag drag;
    drag.window = window;
    drag.grab = QPointF((m_lastPress.x() - drawn.x()) / drawn.width(), (m_lastPress.y() - drawn.y()) / drawn.height());
    drag.original = it ? it->original : QSizeF(frame.width(), frame.height());
    drag.aspect = drawn.height() / drawn.width();
    drag.ghost = drawn;
    m_drag = drag;
    m_drag->ghost = ghostRect(input()->pointer()->pos());
    workspace()->raiseWindow(window);
    effects->addRepaintFull();
    qInfo("glance: clip drag started");
}

// Where a dragged clip is drawn with the pointer at `cursor`: held at the
// spot it was grabbed, scaled by the edge rule like a moved window (full
// size in main, down to parking size at the edges), in the shape it had
// when the drag started.
QRectF Clips::ghostRect(const QPointF &cursor) const
{
    const ClipDrag &drag = *m_drag;
    LogicalOutput *output = workspace()->outputAt(cursor);
    const RectF screen = output ? output->geometryF() : RectF();
    const qreal zoneWidth = screen.width() * zoneFraction;
    const qreal width = drag.original.width();
    const qreal scale = std::clamp(std::min({edgeScale(cursor.x() - screen.x(), drag.grab.x() * width, zoneWidth),
                                             edgeScale(screen.x() + screen.width() - cursor.x(), (1 - drag.grab.x()) * width, zoneWidth)}),
                                   parkingScale(drag.original), 1.0);
    const QSizeF size(width * scale, width * scale * drag.aspect);
    const QPointF topLeft = cursor - QPointF(drag.grab.x() * size.width(), drag.grab.y() * size.height());
    // Kept on the screen, like a moved window.
    return QRectF(QPointF(topLeft.x() + shiftOntoScreen(topLeft.x(), size.width(), screen), topLeft.y()), size);
}

// A dragged clip dropped at `pos` on the desktop (or nothing, or the
// parking band). In the parking band it joins that parking column, against
// the screen edge (as text dropped there does, see parkingSide); elsewhere
// it stays where and as large as it is drawn, like a moved window dropped
// there (parked if shrunk).
void Clips::placeDroppedClip(const QPointF &pos)
{
    const ClipDrag drag = *m_drag;
    m_drag.reset();
    Window *window = drag.window;
    if (!window || !window->windowItem()) {
        return;
    }
    const QRectF from = drag.ghost;
    const auto closeRanks = m_parking.leaving(window);
    const qreal scale = drag.ghost.width() / drag.original.width();
    if (const std::optional<Place> parking = parkingSide(pos)) {
        m_parking.commitPlace(window, *parking, drag.original, drag.ghost.center().y(), from);
    } else if (scale < parkBelow) {
        m_parking.park(window, QRectF(drag.ghost.topLeft(), drag.original * scale), drag.original);
        m_parking.animate(window, from);
        m_parking.arrange(window);
    } else {
        m_parking.resizeAnimated(window, RectF(drag.ghost.x(), drag.ghost.y(), drag.original.width(), drag.original.height()), from);
    }
    closeRanks();
    qInfo("glance: clip dropped on the desktop: moved there");
}

// The drag ended. Pasted (moved): the app closes the clip; it stays drawn
// where it was dropped until then (see closed), or slides back if it
// doesn't. Copied, or not taken: it slides back.
void Clips::dragEnded()
{
    if (!m_drag) {
        return;
    }
    if (m_drag->dropped && !m_drag->copy) {
        qInfo("glance: clip pasted (moved)");
        QTimer::singleShot(clipCloseWait, this, [this, window = m_drag->window]() {
            if (m_drag && m_drag->window == window) {
                slideBack();
            }
        });
        return;
    }
    qInfo(m_drag->dropped ? "glance: clip pasted (copied): back to its place" : "glance: clip not taken: back to its place");
    slideBack();
}

void Clips::slideBack()
{
    const ClipDrag drag = *m_drag;
    m_drag.reset();
    Window *window = drag.window;
    if (!window || !window->windowItem()) {
        return;
    }
    if (m_parking.isParked(window)) {
        m_parking.animate(window, drag.ghost);
    } else {
        // Not parked: glide from where it was dropped to its frame.
        m_parking.resizeAnimated(window, window->frameGeometry(), drag.ghost);
    }
    effects->addRepaintFull();
}

// ParkingLeft/Right if `pos` is in that side's parking band (see
// parkingBand).
std::optional<Place> Clips::parkingSide(const QPointF &pos)
{
    LogicalOutput *output = workspace()->outputAt(pos);
    if (!output) {
        return std::nullopt;
    }
    const RectF screen = output->geometryF();
    const qreal band = screen.width() * zoneFraction * parkingBand;
    if (pos.x() - screen.x() < band) {
        return Place::ParkingLeft;
    }
    if (screen.x() + screen.width() - pos.x() < band) {
        return Place::ParkingRight;
    }
    return std::nullopt;
}

// --- Painting ---

void Clips::prePaintScreen(ScreenPrePaintData &data)
{
    if (m_drag) {
        data.mask |= Effect::PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }
}

void Clips::prePaintWindow(Window *window, WindowPrePaintData &data)
{
    if (m_drag && window == m_drag->window) {
        data.setTransformed();
    }
}

// A clip being dragged is drawn under the pointer (see ghostRect).
bool Clips::paintWindow(Window *window, WindowPaintData &data)
{
    if (!m_drag) {
        return false;
    }
    if (window == m_drag->window && window->windowItem() && m_parking.currentlyDrawn(window).width() > 0) {
        retarget(data, window, m_parking.currentlyDrawn(window), m_drag->ghost);
    }
    return true;
}

} // namespace glance
