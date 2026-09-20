/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "cappainter.h"

#include "cap.h"
#include "catcher.h"
#include "snowlogging.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QVector2D>
#include <QVector4D>

#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <opengl/glutils.h>

#include <algorithm>
#include <array>

namespace Snow
{

// A quad is two triangles, and a triangle is three vertices.
static constexpr int s_verticesPerQuad = 6;

CapPainter::CapPainter() = default;

CapPainter::~CapPainter()
{
    if (!m_profile) {
        return;
    }

    // A texture is a handle in a GL context, and an effect is not unloaded with
    // one current. Making it current is what lets this go back rather than be
    // deleted against whatever context happens to be bound.
    KWin::effects->makeOpenGLContextCurrent();
    m_profile.reset();
}

KWin::GLTexture *CapPainter::profileTexture()
{
    if (!m_uploaded) {
        m_uploaded = true;
        m_profile = KWin::GLTexture::upload(capProfileImage());

        if (m_profile) {
            // Linear, because the body column is a gradient sampled at whatever
            // depth the snow happens to be standing at. The two columns are
            // always sampled dead on their own texel centres, so nothing bleeds
            // sideways between them.
            m_profile->setFilter(GL_LINEAR);
            // Clamped, because a Cap deeper or shallower than the profile runs
            // samples past its ends and must not wrap round to the other one.
            m_profile->setWrapMode(GL_CLAMP_TO_EDGE);
        } else {
            qCWarning(KWIN_EFFECT_SNOW) << "Could not upload the Cap profile; the snow will pile up unseen";
        }
    }

    return m_profile.get();
}

void CapPainter::paint(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                       const QMatrix4x4 &modelViewProjection, const Catcher &catcher,
                       const Settings &settings, qreal opacity, const KWin::Region &deviceRegion)
{
    if (!KWin::effects->isOpenGLCompositing() || opacity <= 0) {
        return;
    }

    // Nothing is being repainted, so nothing of this band can reach the screen:
    // the scissored render below over an empty region emits nothing whatever is
    // in the buffer. Said here rather than left to that, so the Cap's vertices
    // are not built to be thrown away.
    if (deviceRegion.isEmpty()) {
        return;
    }

    KWin::GLTexture *texture = profileTexture();
    if (!texture) {
        return;
    }

    const Snowline &snowline = catcher.snowline();
    const int columns = snowline.columnCount();
    if (columns <= 0) {
        return;
    }

    const QRectF geometry = catcher.geometry();
    const qreal top = geometry.top();
    const bool roundedCorners = catcher.hasRoundedCorners();

    // The posts the Cap is built between: the contour sampled at each Column's
    // centre, with the Catcher's two edges closing it off at bare. That is the
    // prototype's path, and it is why the corner ramp has anything to ramp
    // toward -- the band arrives at the corner already at nothing.
    //
    // Straight into the posts rather than through a contour of its own: the
    // drawn depth of a Column is wanted once, here, and a list of them the
    // width of the Catcher would be built and thrown away in every frame
    // (cap.h: capDepthAt).
    m_posts.resize(columns + 2);
    m_posts[0] = QPointF(geometry.left(), top);
    for (int column = 0; column < columns; ++column) {
        const qreal depth = capDepthAt(snowline, column, settings.capStyle, roundedCorners);
        m_posts[column + 1] = QPointF(geometry.left() + snowline.columnCentre(column), top - depth);
    }
    m_posts[columns + 1] = QPointF(geometry.right(), top);

    // An image uploaded into a texture arrives upside down, so a texture
    // coordinate goes through the texture's own matrix rather than being used
    // as it is.
    const QMatrix4x4 textureMatrix = texture->matrix(KWin::NormalizedCoordinates);
    // Against the snow that is standing rather than against the configured cap,
    // so that a Cap left deeper than the cap by a lowered `maxDepth` keeps its
    // shading while it melts down to it.
    const qreal reference = capReferenceDepth(snowline, settings.maxDepth);
    const qreal profileTop = top + capProfileTop(reference);
    const qreal profileSpan = capProfileSpan(reference);

    // The model is in logical pixels and the projection is over device ones, so
    // this is the one place the scale factor of the output is applied -- and
    // the only reason a depth in logical pixels looks the same on every display.
    const qreal scale = viewport.scale();

    // The band is a quad between every pair of posts, and the leading edge one
    // between every pair of Columns: two a Column, which is what the buffer is
    // sized to before anything is written into it.
    const int quads = (m_posts.size() - 1) + (columns - 1);
    m_vertices.resize(quads * s_verticesPerQuad);
    KWin::GLVertex2D *cursor = m_vertices.data();

    const auto writeQuad = [&](int profileColumn, const std::array<QPointF, 4> &corners) {
        const qreal u = (profileColumn + 0.5) / s_capProfileColumns;
        for (const int index : {0, 1, 2, 0, 2, 3}) {
            const QPointF &corner = corners.at(index);
            // The shading is anchored to the Catcher rather than to the
            // contour, so a vertex's place in the profile is a function of
            // where it is and of nothing else.
            const qreal v = (corner.y() - profileTop) / profileSpan;
            *cursor++ = KWin::GLVertex2D{
                .position = QVector2D(corner.x() * scale, corner.y() * scale),
                .texcoord = QVector2D(textureMatrix.map(QPointF(u, v))),
            };
        }
    };

    // Taper the underlap to zero wherever the contour reaches bare. A fixed
    // underlap would draw a stripe across the whole Catcher after one landing.
    for (int post = 0; post + 1 < m_posts.size(); ++post) {
        const QPointF &left = m_posts.at(post);
        const QPointF &right = m_posts.at(post + 1);
        const qreal leftFoot = top + std::min(s_capUnderlap, top - left.y());
        const qreal rightFoot = top + std::min(s_capUnderlap, top - right.y());
        writeQuad(s_capBodyColumn, {left, right, QPointF(right.x(), rightFoot), QPointF(left.x(), leftFoot)});
    }

    // The bright line along the leading edge, centred on the contour. Only
    // between the Columns themselves: the two closing segments are the ends of
    // the band, not part of its top. Its thickness also tapers to zero on bare
    // Columns, so the highlight cannot leave a line where no snow has landed.
    const qreal half = s_capEdgeWidth / 2;
    for (int post = 1; post + 2 < m_posts.size(); ++post) {
        const QPointF &left = m_posts.at(post);
        const QPointF &right = m_posts.at(post + 1);
        const qreal leftHalf = std::min(half, top - left.y());
        const qreal rightHalf = std::min(half, top - right.y());
        writeQuad(s_capEdgeColumn,
                  {QPointF(left.x(), left.y() - leftHalf), QPointF(right.x(), right.y() - rightHalf),
                   QPointF(right.x(), right.y() + rightHalf), QPointF(left.x(), left.y() + leftHalf)});
    }

    Q_ASSERT(cursor == m_vertices.data() + m_vertices.size());

    KWin::ShaderManager *manager = KWin::ShaderManager::instance();
    KWin::GLShader *shader = manager->pushShader(KWin::ShaderTrait::MapTexture
                                                 | KWin::ShaderTrait::Modulate
                                                 | KWin::ShaderTrait::TransformColorspace);
    shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, modelViewProjection);
    // The profile was painted in sRGB; what the output wants it in is the
    // render target's business, and on an HDR one it is not sRGB.
    shader->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(),
                                  KWin::RenderingIntent::Perceptual);
    // A window fading in or out carries its snow with it. The profile image is
    // premultiplied and a modulation that scales all four channels alike leaves
    // it premultiplied.
    shader->setUniform(KWin::GLShader::Vec4Uniform::ModulationConstant,
                       QVector4D(opacity, opacity, opacity, opacity));

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // Clipped to what is being repainted, or a Cap standing in a part of the
    // buffer that already holds it is blended over itself; see FlakePainter.
    glEnable(GL_SCISSOR_TEST);
    texture->bind();

    KWin::GLVertexBuffer *vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(m_vertices);
    vbo->render(deviceRegion, GL_TRIANGLES, true);

    texture->unbind();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    manager->popShader();
}

} // namespace Snow
