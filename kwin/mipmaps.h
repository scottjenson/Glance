// Mipmaps for small windows (docs/dragging-and-parking.md, Drawing small
// windows). KWin draws a scaled window by blending the 4 window pixels
// nearest each screen pixel, so drawn below half size it skips most of
// them: text and thin lines break up, and shimmer as the window moves.
// A window drawn smaller than mipmapBelow is drawn instead from an image
// of itself at full size and that image's mipmaps (copies at 1/2, 1/4,
// ..., each averaging 2x2 pixels of the one before), which the GPU blends
// at the drawn size. The image is redrawn only when the window's content
// changes (KWin's window damage). The focus ring isn't in it: it is drawn
// over it as usual, so it stays sharp.
#pragma once

#include <QObject>

#include <map>
#include <memory>

namespace KWin
{
class EffectWindow;
class GLFramebuffer;
class GLTexture;
class Region;
class RenderTarget;
class RenderViewport;
class WindowPaintData;
}

namespace glance
{

class FocusRing;

class Mipmaps : public QObject
{
    Q_OBJECT

public:
    explicit Mipmaps(FocusRing &focusRing);
    ~Mipmaps() override;

    // Draws `window` from its mipmaps if it is drawn small (from the
    // effect's drawWindow); false: KWin draws it as usual.
    bool drawWindow(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::EffectWindow *window,
                    int mask, const KWin::Region &deviceRegion, const KWin::WindowPaintData &data);

private:
    // A window's full-size image, and whether its content changed since.
    struct Image
    {
        std::unique_ptr<KWin::GLTexture> texture;
        std::unique_ptr<KWin::GLFramebuffer> framebuffer;
        bool dirty = true;
        QMetaObject::Connection damaged;
        QMetaObject::Connection resized;
    };

    bool update(KWin::EffectWindow *window, Image &image);
    void paint(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::EffectWindow *window,
               const KWin::Region &deviceRegion, const KWin::WindowPaintData &data, KWin::GLTexture *texture);
    void forget(KWin::EffectWindow *window);

    FocusRing &m_focusRing;
    std::map<KWin::EffectWindow *, Image> m_images;
};

} // namespace glance
