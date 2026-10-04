// glance-clip: a clip window for Glance. Glance starts it with a clip file
// (text dropped on the desktop, or Meta+C) and places the window.
//
// The window shows the text large and read-only, without menus. The whole
// window is the text: press anywhere in it and drag, and the text goes along
// as a normal drag and drop, no selecting first. Accepted by the app it is
// dropped on, it is pasted there and the clip is gone, like a physical
// object moved; with Shift held at the drop it is copied and the clip stays.
// Glance draws the note following the pointer meanwhile, so it looks like
// moving it; dropped on the desktop it moves there, refused it slides back
// (Glance's side). Closing the window deletes the clip. The window moves by its title bar or
// with Meta+drag (Glance). Ctrl+C copies the text.
//
// It looks like a sticky note: yellow, title bar included (a KDE color
// scheme of its own, which KWin's title bar follows), and no title (the
// text is right below it).
//
// An image clip (an image file: dropped image data or an image file, see
// Glance) shows the image filling the window, in its proportions, and drags
// out as PNG data and as the file (for file managers and upload forms).

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <cstdio>

// The app id Glance recognizes clip windows by (Wayland app_id).
static constexpr const char *appId = "org.glance.Clip";
// Text size relative to the desktop's font, the margin around it, and the
// largest size the window starts at (logical pixels).
static constexpr qreal textScale = 1.4;
static constexpr int margin = 16;
static constexpr int startWidth = 480;
static constexpr int startMaxHeight = 480;
// Sticky-note colors: the note, its title bar, and the text.
static const QColor noteColor(255, 245, 157);
static const QColor titleBarColor(255, 236, 120);
static const QColor textColor(58, 52, 32);

// Write the sticky-note color scheme (a KDE .colors file) to the cache
// folder and return its path. The Plasma platform theme gives it to KWin
// for this app's title bars (KDE_COLOR_SCHEME_PATH, see main).
static QString writeColorScheme()
{
    const auto rgb = [](const QColor &c) {
        return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
    };
    QString scheme = QStringLiteral("[General]\nName=Glance Clip\n\n[WM]\n");
    for (const char *prefix : {"active", "inactive"}) {
        scheme += QStringLiteral("%1Background=%2\n%1Blend=%2\n%1Foreground=%3\n")
                      .arg(QLatin1String(prefix), rgb(titleBarColor), rgb(textColor));
    }
    for (const char *set : {"Window", "View", "Button", "Header", "Header][Inactive", "Tooltip"}) {
        const QColor &background = QLatin1String(set).startsWith(QLatin1String("Header")) ? titleBarColor : noteColor;
        scheme += QStringLiteral("\n[Colors:%1]\nBackgroundNormal=%2\nBackgroundAlternate=%2\nForegroundNormal=%3\n")
                      .arg(QLatin1String(set), rgb(background), rgb(textColor));
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/sticky-note.colors");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(scheme.toUtf8());
    }
    return path;
}

// An image clip's body: the image scaled to fit, centered (the window
// keeps the image's proportions, so it fills it).
class ImageView : public QWidget
{
public:
    explicit ImageView(const QImage &image)
        : m_image(image)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QSizeF size = m_image.size();
        size.scale(this->size(), Qt::KeepAspectRatio);
        const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
        painter.drawImage(target, m_image);
    }

private:
    QImage m_image;
};

class ClipWindow : public QWidget
{
public:
    // A text clip shows `text`; an image clip (`image` not null) `image`.
    ClipWindow(const QString &path, const QString &text, const QImage &image)
        : m_path(path)
        , m_text(text)
        , m_image(image)
    {
        setWindowTitle(QString()); // the text is right below
        QPalette palette = this->palette();
        for (QPalette::ColorRole role : {QPalette::Window, QPalette::Base}) {
            palette.setColor(role, noteColor);
        }
        for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text}) {
            palette.setColor(role, textColor);
        }
        setPalette(palette);
        setAutoFillBackground(true);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("klipper"), QIcon::fromTheme(QStringLiteral("edit-paste"))));

        auto copy = new QShortcut(QKeySequence::Copy, this);
        connect(copy, &QShortcut::activated, this, [this]() {
            if (isImage()) {
                QGuiApplication::clipboard()->setImage(m_image);
            } else {
                QGuiApplication::clipboard()->setText(m_text);
            }
        });

        // Logging out closes the window too; the clip file stays then.
        connect(qApp, &QGuiApplication::commitDataRequest, this, [this]() {
            m_sessionEnding = true;
        });

        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        if (isImage()) {
            auto view = new ImageView(image);
            view->setCursor(Qt::OpenHandCursor);
            view->installEventFilter(this);
            layout->addWidget(view);
            // Small enough for Glance's parking layout.
            setMinimumSize(40, 40);
            // Its own size (taken as logical pixels), scaled down to fit
            // the start size.
            QSize size = image.size();
            if (size.width() > startWidth || size.height() > startMaxHeight) {
                size.scale(startWidth, startMaxHeight, Qt::KeepAspectRatio);
            }
            resize(size.expandedTo(minimumSize()));
            return;
        }

        m_label = new QLabel(text);
        m_label->setTextFormat(Qt::PlainText);
        m_label->setWordWrap(true);
        m_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_label->setMargin(margin);
        m_label->setTextInteractionFlags(Qt::NoTextInteraction);
        QFont font = m_label->font();
        font.setPointSizeF(font.pointSizeF() * textScale);
        m_label->setFont(font);
        m_label->setCursor(Qt::OpenHandCursor);

        m_scroll = new QScrollArea;
        m_scroll->setWidget(m_label);
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        // No scroll bars, like a note (the wheel still scrolls; a cut-off
        // last line shows there's more).
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(m_scroll);

        // Presses and drags anywhere on the text (and the empty space
        // below it) start the drag.
        m_label->installEventFilter(this);
        m_scroll->viewport()->installEventFilter(this);

        // Glance lays parked clips out small (a narrow note in parking).
        setMinimumSize(100, 80);

        // As large as the text needs, up to a start size.
        const QFontMetrics metrics(font);
        resize(startWidth, std::clamp(neededHeight(startWidth), 3 * metrics.height(), startMaxHeight));
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_pressPos = mouse->position().toPoint();
                m_pressed = true;
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto mouse = static_cast<QMouseEvent *>(event);
            if (m_pressed && (mouse->buttons() & Qt::LeftButton)
                && (mouse->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_pressed = false;
                drag();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            m_pressed = false;
            break;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }

    // Made narrower than it started (Glance does that in parking): as tall
    // as the text needs at the new width, up to startMaxHeight, so a short
    // clip is a short note. Glance takes the height it chooses.
    // An image clip made wider or narrower (by Glance, or by hand) takes
    // the image's proportions again; one made only taller or shorter by
    // hand keeps the change.
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (isImage()) {
            if (event->size().width() == event->oldSize().width() || m_image.width() <= 0) {
                return;
            }
            const int height = std::max(minimumHeight(), qRound(width() * qreal(m_image.height()) / m_image.width()));
            if (std::abs(height - this->height()) > 1) {
                QTimer::singleShot(0, this, [this, height]() {
                    resize(width(), height);
                });
            }
            return;
        }
        if (width() >= startWidth) {
            return;
        }
        const int height = std::min(neededHeight(width()), startMaxHeight);
        if (std::abs(height - this->height()) > 1) {
            QTimer::singleShot(0, this, [this, height]() {
                resize(width(), height);
            });
        }
    }

    void closeEvent(QCloseEvent *event) override
    {
        if (!m_sessionEnding && !m_keepFile) {
            QFile::remove(m_path);
        }
        event->accept();
    }

private:
    bool isImage() const
    {
        return !m_image.isNull();
    }

    // The clip as data for a drag: the text, or the image as PNG (any app
    // that takes images takes PNG) and the file.
    QMimeData *mimeData() const
    {
        auto mime = new QMimeData;
        if (!isImage()) {
            mime->setText(m_text);
            return mime;
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        m_image.save(&buffer, "PNG");
        mime->setData(QStringLiteral("image/png"), png);
        mime->setImageData(m_image);
        mime->setUrls({QUrl::fromLocalFile(m_path)});
        return mime;
    }

    // The whole clip as a drag. Only copying is offered to the target (any
    // app that takes text accepts a copy); whether the clip then goes is
    // ours to decide: it does unless Shift is held at the drop.
    // Glance draws the note itself under the pointer, so the drag picture
    // is invisible. Meanwhile the window takes no pointer input (a 1-pixel
    // input mask), so drops near where it was fall through to what is
    // below, rather than onto the clip itself.
    void drag()
    {
        auto drag = new QDrag(this);
        drag->setMimeData(mimeData());
        QPixmap invisible(1, 1);
        invisible.fill(Qt::transparent);
        drag->setPixmap(invisible);
        windowHandle()->setMask(QRegion(0, 0, 1, 1));
        const Qt::DropAction result = drag->exec(Qt::CopyAction, Qt::CopyAction);
        windowHandle()->setMask(QRegion());
        const bool copy = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier;
        std::printf("glance-clip: drag %s%s\n", result == Qt::IgnoreAction ? "not accepted" : "accepted",
                    result == Qt::IgnoreAction ? "" : (copy ? ", Shift: copied, the clip stays" : ": moved, the clip goes"));
        std::fflush(stdout);
        if (result != Qt::IgnoreAction && !copy) {
            if (isImage()) {
                // The app may read the file a while later (a file manager
                // asks whether to copy it first): the window goes, the
                // file a minute later.
                m_keepFile = true;
                QGuiApplication::setQuitOnLastWindowClosed(false);
                close();
                QTimer::singleShot(std::chrono::minutes(1), qApp, [path = m_path]() {
                    QFile::remove(path);
                    QCoreApplication::quit();
                });
            } else {
                close();
            }
        }
    }

    // The height the text needs (with its margins) at `width`, at least
    // one line.
    int neededHeight(int width) const
    {
        const QFontMetrics metrics(m_label->font());
        const QRect needed = metrics.boundingRect(QRect(0, 0, width - 2 * margin, 100000), Qt::TextWordWrap, m_text);
        return std::max(needed.height(), metrics.height()) + 2 * margin;
    }

    QString m_path;
    QString m_text;
    QImage m_image;
    QLabel *m_label = nullptr;
    QScrollArea *m_scroll = nullptr;
    QPoint m_pressPos;
    bool m_pressed = false;
    bool m_sessionEnding = false;
    bool m_keepFile = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("glance-clip"));
    // An empty display name too, or Qt shows it as the (empty) title.
    QGuiApplication::setApplicationDisplayName(QStringLiteral(" "));
    QGuiApplication::setDesktopFileName(QLatin1String(appId));
    app.setProperty("KDE_COLOR_SCHEME_PATH", writeColorScheme());
    if (argc != 2) {
        std::fprintf(stderr, "usage: glance-clip <clip file>\n");
        return 2;
    }
    const QString path = QFile::decodeName(argv[1]);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "glance-clip: can't read %s\n", argv[1]);
        return 1;
    }
    const QByteArray data = file.readAll();
    // An image file is an image clip, anything else text.
    QImage image;
    if (QImageReader(path).canRead()) {
        image = QImageReader(path).read();
    }
    ClipWindow window(QFileInfo(path).absoluteFilePath(), image.isNull() ? QString::fromUtf8(data) : QString(), image);
    window.show();
    return app.exec();
}
