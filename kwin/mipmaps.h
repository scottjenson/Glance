// Mipmaps for small windows: a window drawn below mipmapBelow, or a tilted
// parking tile, is drawn from a full-size image of itself and its mipmaps,
// redrawn only when its content changes (docs/dragging-and-parking.md,
// Drawing small windows). The focus ring is drawn over it, except on
// tilted windows, whose image includes it so it turns with them.
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
class ParkedWindows;

class Mipmaps : public QObject
{
    Q_OBJECT

public:
    Mipmaps(ParkedWindows &parking, FocusRing &focusRing);
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
        // The focus ring is in the image (a tilted window).
        bool withRing = false;
        QMetaObject::Connection damaged;
        QMetaObject::Connection resized;
    };

    bool update(KWin::EffectWindow *window, Image &image, bool withRing);
    void paint(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport, KWin::EffectWindow *window,
               const KWin::Region &deviceRegion, const KWin::WindowPaintData &data, KWin::GLTexture *texture);
    void forget(KWin::EffectWindow *window);

    ParkedWindows &m_parking;
    FocusRing &m_focusRing;
    std::map<KWin::EffectWindow *, Image> m_images;
};

} // namespace glance
