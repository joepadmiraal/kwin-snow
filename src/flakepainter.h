/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "flake.h"
#include "settings.h"

#include <QList>

#include <memory>

namespace KWin
{
struct GLVertex2D;
}

namespace KWin
{
class GLTexture;
class RenderTarget;
class RenderViewport;
}

namespace Snow
{

/**
 * Draws the Flakes of one output, above every window.
 *
 * One pass over the whole population, with no per-window work anywhere: an
 * enabled Snow Catcher class is Solid, so a Flake can never occupy a pixel a
 * window covers, and painting above or below the stack produces the same image
 * (ADR-0003). The pass hangs off SnowEffect::paintScreen() rather than
 * postPaintScreen(), which is the same point in the frame -- after every window
 * -- but is the call KWin hands a render target to.
 *
 * Each style's shape is rendered once into a texture and then drawn as a quad
 * per Flake, so a few hundred Flakes cost a few hundred quads rather than a few
 * hundred gradients or several thousand strokes. The quads go into a single
 * vertex buffer and are uploaded in one go, and drawn in one call -- or, under
 * the `depth` style, in one call per depth plane over ranges of that same
 * buffer; see paint().
 *
 * There is nothing here for QPainter compositing, which would need a second
 * renderer for a fallback nothing runs on a machine that can composite at all.
 * On such a session the snow falls and settles and is simply not drawn.
 */
class FlakePainter
{
public:
    FlakePainter();
    ~FlakePainter();

    /**
     * Draw @a flakes into @a renderTarget in @a style.
     *
     * @a flakes are in global logical pixels, the coordinates the whole model
     * works in (ADR-0001); @a viewport is what turns them into the device
     * pixels the output is actually rendered at.
     */
    void paint(const KWin::RenderTarget &renderTarget,
               const KWin::RenderViewport &viewport,
               const QList<Flake> &flakes,
               FlakeStyle style);

private:
    /** A sprite texture, and whether making it has already been tried. */
    struct Sprite {
        std::unique_ptr<KWin::GLTexture> texture;
        /** Uploading happens once; a failure must not be retried every frame. */
        bool uploaded = false;
    };

    /**
     * The texture @a style draws with, uploaded on first use -- which is the
     * first frame, because that is the first time there is a current GL context
     * to make a texture in.
     */
    KWin::GLTexture *textureFor(FlakeStyle style);

    Sprite m_blob;
    Sprite m_crystal;

    /**
     * The quads of the frame being drawn, kept between frames for the room it
     * has already claimed rather than for anything in it.
     *
     * A few thousand Flakes are a couple of hundred kilobytes of vertices, and
     * building them in a list that is thrown away at the end of every frame
     * means asking the allocator for that much, thirty times a second, for as
     * long as it is snowing. Reusing one buffer costs the same arithmetic and
     * no allocation at all once the snow has reached its density.
     */
    QList<KWin::GLVertex2D> m_vertices;
};

} // namespace Snow
