/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"

#include <QList>
#include <QPointF>

#include <memory>

class QMatrix4x4;

namespace KWin
{
class GLTexture;
class RenderTarget;
class RenderViewport;
struct GLVertex2D;
}

namespace Snow
{

class Catcher;

/**
 * Draws the Cap of one Snow Catcher.
 *
 * Every Cap but the ground's is drawn inside its own Catcher's paintWindow
 * pass, which is what makes occlusion free: KWin paints windows back to front,
 * so an overlapping window covers the Cap of the window beneath it with no
 * clipping code anywhere in the effect (ADR-0003). The ground's Cap goes in
 * just after the wallpaper, which is the one point in a frame that is over the
 * desktop and under every window; see SnowEffect::paintWindow().
 *
 * A Cap is one draw. The band is a strip of quads following the contour down
 * to a line just inside the Catcher, and the bright line along the leading
 * edge is a second strip over the top of it; both sample the same two-column
 * profile image, so the two shapes -- one shaded by where it is on the screen,
 * one flat -- need neither a second texture nor a second shader.
 *
 * As with FlakePainter there is nothing here for QPainter compositing: on such
 * a session the snow lands and settles and is simply not drawn.
 */
class CapPainter
{
public:
    CapPainter();
    ~CapPainter();

    /**
     * Draw @a catcher's Cap into @a renderTarget at @a opacity.
     *
     * @a modelViewProjection places it. For a window that is the viewport's own
     * projection with the window's paint transform folded in, so that a Cap
     * follows its Catcher through whatever effect below is moving it; for the
     * ground it is the projection alone, because the ground belongs to the
     * output rather than to the wallpaper it is drawn over.
     */
    void paint(const KWin::RenderTarget &renderTarget,
               const KWin::RenderViewport &viewport,
               const QMatrix4x4 &modelViewProjection,
               const Catcher &catcher,
               const Settings &settings,
               qreal opacity);

private:
    /**
     * The profile texture, uploaded on first use -- which is the first frame,
     * because that is the first time there is a current GL context to make a
     * texture in.
     */
    KWin::GLTexture *profileTexture();

    std::unique_ptr<KWin::GLTexture> m_profile;
    /** Uploading happens once; a failure must not be retried every frame. */
    bool m_uploaded = false;

    /**
     * The posts and the quads of the Cap being drawn, kept between Caps for the
     * room they have already claimed rather than for anything in them.
     *
     * A Cap is drawn for every Snow Catcher that is holding snow, in every
     * frame, and the ground's is a post and two quads per five logical pixels
     * of the whole width of an output. Built in lists of their own that is a
     * handful of allocations per window per frame; built in these it is none,
     * once the widest Catcher on the desktop has been drawn once.
     */
    QList<QPointF> m_posts;
    QList<KWin::GLVertex2D> m_vertices;
};

} // namespace Snow
