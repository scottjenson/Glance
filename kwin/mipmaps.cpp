// Mipmaps for small windows (see mipmaps.h).
#include "mipmaps.h"

#include "focusring.h"
#include "tuning.h"

#include <core/output.h>
#include <core/pixelgrid.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <opengl/glutils.h>
#include <opengl/glvertexbuffer.h>
#include <scene/itemgeometry.h>
#include <scene/itemrenderer.h>
#include <scene/outlinedborderitem.h>
#include <scene/windowitem.h>
#include <scene/workspacescene.h>

#include <cmath>

using namespace KWin;

namespace glance
{

// Where the window is at full size (frame, title bar, shadow), snapped to
// device pixels: its item's own bounds, untransformed, at the frame's
// top-left corner. Not EffectWindow::expandedGeometry: that is where it
// is drawn, Glance's transform included.
static RectF fullSizeGeometry(EffectWindow *window, qreal scale)
{
    return snapToPixels(window->windowItem()->boundingRect().translated(window->frameGeometry().topLeft()), scale);
}

Mipmaps::Mipmaps(FocusRing &focusRing)
    : m_focusRing(focusRing)
{
    connect(effects, &EffectsHandler::windowDeleted, this, &Mipmaps::forget);
}

Mipmaps::~Mipmaps()
{
    if (!m_images.empty()) {
        if (!EglContext::currentContext()) {
            effects->makeOpenGLContextCurrent(); // textures go with the context
        }
        m_images.clear();
    }
}

bool Mipmaps::drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *window, int mask,
                         const Region &deviceRegion, const WindowPaintData &data)
{
    WindowItem *item = window->windowItem();
    if (!item || item->transform().m11() >= mipmapBelow || !effects->isOpenGLCompositing()) {
        forget(window);
        return false;
    }
    auto [it, added] = m_images.try_emplace(window);
    Image &image = it->second;
    if (added) {
        auto dirty = [this](EffectWindow *window) {
            if (auto it = m_images.find(window); it != m_images.end()) {
                it->second.dirty = true;
            }
        };
        image.damaged = connect(window, &EffectWindow::windowDamaged, this, dirty);
        // Its full-size bounds, not where it is drawn (which changes on
        // every frame of a drag).
        image.resized = connect(item, &Item::boundingRectChanged, this, [dirty, window]() {
            dirty(window);
        });
    }
    if (!update(window, image)) {
        forget(window);
        return false;
    }
    paint(renderTarget, viewport, window, deviceRegion, data, image.texture.get());
    // The focus ring over it, drawn as usual (the window's other items
    // are left out).
    Item *ring = m_focusRing.ring();
    if (ring && ring->parentItem() == item) {
        effects->scene()->renderer()->renderItem(renderTarget, viewport, item, mask, deviceRegion, data, [item, ring](Item *other) {
            return other != item && other != ring;
        }, nullptr);
    }
    return true;
}

// Redraw the window's full-size image if its content changed, and its
// mipmaps. False if there is no image (no size, or out of memory).
bool Mipmaps::update(EffectWindow *window, Image &image)
{
    const qreal scale = window->screen()->scale();
    const RectF geometry = fullSizeGeometry(window, scale);
    const QSize size = (geometry.size() * scale).toSize();
    if (size.isEmpty()) {
        return false;
    }
    if (!image.texture || image.texture->size() != size) {
        const int levels = 1 + int(std::log2(std::max(size.width(), size.height())));
        image.framebuffer.reset();
        image.texture = GLTexture::allocate(GL_RGBA8, size, levels);
        if (!image.texture) {
            return false;
        }
        image.texture->setFilter(GL_LINEAR_MIPMAP_LINEAR);
        image.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        image.framebuffer = std::make_unique<GLFramebuffer>(image.texture.get());
        image.dirty = true;
    }
    if (!image.dirty) {
        return true;
    }

    // At full size: the paint data's matrix comes just before the item's
    // transform (createRenderNode in scene/itemrenderer_opengl.cpp), so
    // its inverse undoes Glance's draw transform.
    const QTransform transform = window->windowItem()->transform();
    WindowPaintData data;
    data.setXScale(1.0 / transform.m11());
    data.setYScale(1.0 / transform.m22());
    data.setXTranslation(-transform.dx() / transform.m11());
    data.setYTranslation(-transform.dy() / transform.m22());

    RenderTarget target(image.framebuffer.get());
    RenderViewport targetViewport(geometry, scale, target, QPoint());
    GLFramebuffer::pushFramebuffer(image.framebuffer.get());
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    Item *ring = m_focusRing.ring();
    effects->scene()->renderer()->renderItem(target, targetViewport, window->windowItem(),
                                             Effect::PAINT_WINDOW_TRANSFORMED | Effect::PAINT_WINDOW_TRANSLUCENT,
                                             Region::infinite(), data, [ring](Item *item) {
                                                 return item == ring;
                                             }, nullptr);
    GLFramebuffer::popFramebuffer();

    image.texture->bind();
    image.texture->generateMipmaps();
    image.texture->unbind();
    image.dirty = false;
    return true;
}

// Draw the image where the window is drawn: KWin's own transforms for a
// window item (its position, the paint data's matrix, then the item's
// transform), with the texture's mipmaps (see OffscreenData::paint in
// effect/offscreeneffect.cpp, which this follows).
void Mipmaps::paint(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *window,
                    const Region &deviceRegion, const WindowPaintData &data, GLTexture *texture)
{
    const qreal scale = viewport.scale();
    const RectF frame = snapToPixels(window->frameGeometry(), scale);
    const RectF rect = fullSizeGeometry(window, scale).translated(-frame.topLeft());
    WindowQuad quad;
    quad[0] = WindowVertex(rect.topLeft(), QPointF(0, 0));
    quad[1] = WindowVertex(rect.topRight(), QPointF(1, 0));
    quad[2] = WindowVertex(rect.bottomRight(), QPointF(1, 1));
    quad[3] = WindowVertex(rect.bottomLeft(), QPointF(0, 1));

    RenderGeometry geometry;
    geometry.setVertexSnappingMode(RenderGeometry::VertexSnappingMode::Round);
    geometry.appendWindowQuad(quad, scale);
    geometry.postProcessTextureCoordinates(texture->matrix(NormalizedCoordinates));

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const auto map = vbo->map<GLVertex2D>(geometry.size());
    if (!map) {
        return;
    }
    geometry.copy(*map);
    vbo->unmap();
    vbo->bindArrays();

    QMatrix4x4 mvp = viewport.projectionMatrix();
    mvp.translate(std::round(window->x() * scale), std::round(window->y() * scale));
    mvp *= data.toMatrix(scale);
    mvp.scale(scale, scale);
    mvp *= QMatrix4x4(window->windowItem()->transform());
    mvp.scale(1.0 / scale, 1.0 / scale);

    GLShader *shader = ShaderManager::instance()->shader(ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::AdjustSaturation
                                                         | ShaderTrait::TransformColorspace);
    ShaderBinder binder(shader);
    const qreal rgb = data.brightness() * data.opacity();
    const qreal a = data.opacity();
    const auto toXYZ = renderTarget.colorDescription()->containerColorimetry().toXYZ();
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, mvp);
    shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
    shader->setUniform(GLShader::FloatUniform::Saturation, data.saturation());
    shader->setUniform(GLShader::Vec3Uniform::PrimaryBrightness, QVector3D(toXYZ(1, 0), toXYZ(1, 1), toXYZ(1, 2)));
    shader->setUniform(GLShader::IntUniform::TextureWidth, texture->width());
    shader->setUniform(GLShader::IntUniform::TextureHeight, texture->height());
    shader->setColorspaceUniforms(ColorDescription::sRGB, renderTarget.colorDescription(), RenderingIntent::Perceptual);

    const bool clipping = deviceRegion != Region::infinite();
    const Region clipRegion = clipping ? viewport.transform().map(deviceRegion, renderTarget.transformedSize()) : Region::infinite();
    if (clipping) {
        glEnable(GL_SCISSOR_TEST);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    texture->bind();
    vbo->draw(clipRegion, GL_TRIANGLES, 0, geometry.count(), clipping);
    texture->unbind();
    glDisable(GL_BLEND);
    if (clipping) {
        glDisable(GL_SCISSOR_TEST);
    }
    vbo->unbindArrays();
}

void Mipmaps::forget(EffectWindow *window)
{
    auto it = m_images.find(window);
    if (it == m_images.end()) {
        return;
    }
    disconnect(it->second.damaged);
    disconnect(it->second.resized);
    if (!EglContext::currentContext()) {
        effects->makeOpenGLContextCurrent();
    }
    m_images.erase(it);
}

} // namespace glance
