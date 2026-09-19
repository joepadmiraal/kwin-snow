/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "flakepainter.h"

#include "flakesprite.h"
#include "snowlogging.h"

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector4D>

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <opengl/glutils.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Snow
{

/*
    How many depth planes the `depth` style is drawn in.

    A Flake's opacity under that style is a function of its z alone, and the
    generated shader has one modulation for a whole draw -- so a Flake's own
    opacity would mean either a draw per Flake or a shader of our own, and the
    ticket asks for the draw to be batched. Cutting the field into planes and
    drawing a plane at a time gives back the batch: the size, the fall speed and
    the landing mass still vary continuously per Flake, and only the opacity is
    quantised, in steps of 0.7 / 8 that nothing in a field of moving snow
    resolves.

    Drawing them far plane first is the other half of the parallax: near Flakes
    then blend over distant ones rather than in whatever order the population
    list happens to be in.
*/
static constexpr int s_depthPlanes = 8;

// A quad is two triangles, and a triangle is three vertices.
static constexpr int s_verticesPerFlake = 6;

FlakePainter::FlakePainter() = default;

FlakePainter::~FlakePainter()
{
    if (!m_blob.texture && !m_crystal.texture) {
        return;
    }

    // A texture is a handle in a GL context, and an effect is not unloaded with
    // one current. Making it current is what lets these go back rather than be
    // deleted against whatever context happens to be bound.
    KWin::effects->makeOpenGLContextCurrent();
    m_blob.texture.reset();
    m_crystal.texture.reset();
}

KWin::GLTexture *FlakePainter::textureFor(FlakeStyle style)
{
    const bool crystal = style == FlakeStyle::Crystal;
    Sprite &sprite = crystal ? m_crystal : m_blob;

    // `blob` and `depth` are the same sprite: what depth adds is a size and an
    // opacity per Flake, not a different shape (spec: Configuration).
    if (!sprite.uploaded) {
        sprite.uploaded = true;
        sprite.texture = KWin::GLTexture::upload(crystal ? crystalSpriteImage() : blobSpriteImage());

        if (sprite.texture) {
            // Linear, because a sprite is drawn at whatever size the Flake's
            // radius and z make it and lands on the pixel grid nowhere in
            // particular; nearest would make the snow crawl.
            sprite.texture->setFilter(GL_LINEAR);
            // Clamped, because a quad's edge samples the transparent rim of the
            // image and must not wrap round to the opaque middle.
            sprite.texture->setWrapMode(GL_CLAMP_TO_EDGE);
        } else {
            qCWarning(KWIN_EFFECT_SNOW) << "Could not upload the Flake sprite; the snow will fall unseen";
        }
    }

    return sprite.texture.get();
}

/** Write the two triangles of one sprite over the s_verticesPerFlake at @a out. */
static void writeFlake(KWin::GLVertex2D *out, const QPointF &centre, qreal half,
                       qreal rotation, const std::array<QVector2D, 4> &texcoords)
{
    const qreal cosine = std::cos(rotation);
    const qreal sine = std::sin(rotation);
    const auto corner = [&](qreal dx, qreal dy) {
        return QVector2D(centre.x() + dx * cosine - dy * sine,
                         centre.y() + dx * sine + dy * cosine);
    };

    const std::array<QVector2D, 4> corners = {
        corner(-half, -half),
        corner(half, -half),
        corner(half, half),
        corner(-half, half),
    };

    int vertex = 0;
    for (const int index : {0, 1, 2, 0, 2, 3}) {
        out[vertex++] = KWin::GLVertex2D{
            .position = corners.at(index),
            .texcoord = texcoords.at(index),
        };
    }
}

/** Which depth plane @a flake is drawn in, of @a planes. */
static int planeOf(const Flake &flake, int planes)
{
    return std::clamp(int(flake.z * planes), 0, planes - 1);
}

void FlakePainter::paint(const KWin::RenderTarget &renderTarget, const KWin::RenderViewport &viewport,
                         const QList<Flake> &flakes, FlakeStyle style)
{
    if (flakes.isEmpty() || !KWin::effects->isOpenGLCompositing()) {
        return;
    }

    KWin::GLTexture *texture = textureFor(style);
    if (!texture) {
        return;
    }

    // An image uploaded into a texture arrives upside down, so the corners of
    // the sprite come from the texture's own matrix rather than from 0 and 1.
    const QMatrix4x4 textureMatrix = texture->matrix(KWin::NormalizedCoordinates);
    const std::array<QVector2D, 4> texcoords = {
        QVector2D(textureMatrix.map(QPointF(0, 0))),
        QVector2D(textureMatrix.map(QPointF(1, 0))),
        QVector2D(textureMatrix.map(QPointF(1, 1))),
        QVector2D(textureMatrix.map(QPointF(0, 1))),
    };

    // The model is in logical pixels and the projection is over device ones, so
    // this is the one place the scale factor of the output is applied -- and
    // the only reason a Flake's radius means the same thing on every display.
    const qreal scale = viewport.scale();
    const int planes = style == FlakeStyle::Depth ? s_depthPlanes : 1;

    // The whole population goes into one buffer, a plane at a time, so that the
    // planes are ranges of a single upload rather than eight of their own. That
    // takes knowing how long each of them is before anything is written, which
    // is what this first pass over the Flakes is for; the second fills it.
    // A covered Flake is behind a window and is not drawn at all: skipped in
    // both passes, so it costs no vertices rather than invisible ones.
    std::array<int, s_depthPlanes> planeCount{};
    for (const Flake &flake : flakes) {
        if (flake.covered) {
            continue;
        }
        ++planeCount[planeOf(flake, planes)];
    }

    std::array<int, s_depthPlanes> planeFirst{};
    std::array<int, s_depthPlanes> cursor{};
    int total = 0;
    for (int plane = 0; plane < planes; ++plane) {
        planeFirst[plane] = total;
        cursor[plane] = total;
        total += planeCount[plane] * s_verticesPerFlake;
    }

    m_vertices.resize(total);
    KWin::GLVertex2D *const vertices = m_vertices.data();

    for (const Flake &flake : flakes) {
        if (flake.covered) {
            continue;
        }

        const FlakeSprite sprite = spriteFor(flake, style);
        const int plane = planeOf(flake, planes);
        writeFlake(vertices + cursor[plane], QPointF(flake.x, flake.y) * scale,
                   sprite.size * scale / 2, sprite.rotation, texcoords);
        cursor[plane] += s_verticesPerFlake;
    }

    KWin::ShaderManager *manager = KWin::ShaderManager::instance();
    KWin::GLShader *shader = manager->pushShader(KWin::ShaderTrait::MapTexture
                                                 | KWin::ShaderTrait::Modulate
                                                 | KWin::ShaderTrait::TransformColorspace);
    shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    // The sprites were painted in sRGB; what the output wants them in is the
    // render target's business, and on an HDR one it is not sRGB.
    shader->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(),
                                  KWin::RenderingIntent::Perceptual);

    // The sprite images are premultiplied, and a modulation that scales all
    // four channels alike leaves them premultiplied.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    texture->bind();

    // One upload for the whole population, and then a draw over each plane's
    // range of it. Only the opacity differs between the planes, and the uniform
    // it sets is the reason they cannot be one draw (see s_depthPlanes); the
    // vertices have no reason to be uploaded more than once.
    KWin::GLVertexBuffer *vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(m_vertices);
    vbo->bindArrays();

    for (int plane = 0; plane < planes; ++plane) {
        if (planeCount[plane] == 0) {
            continue;
        }

        // The plane is drawn at the opacity of the depth it is centred on. For
        // every style but `depth` there is one plane and one flat opacity.
        const qreal opacity = flakeOpacity((plane + 0.5) / planes, style);
        shader->setUniform(KWin::GLShader::Vec4Uniform::ModulationConstant,
                           QVector4D(opacity, opacity, opacity, opacity));

        // No region: the effect asks for a full repaint every frame, because
        // falling snow is damage nothing else reports.
        vbo->draw(GL_TRIANGLES, planeFirst[plane], planeCount[plane] * s_verticesPerFlake);
    }

    vbo->unbindArrays();
    texture->unbind();
    glDisable(GL_BLEND);
    manager->popShader();
}

} // namespace Snow
