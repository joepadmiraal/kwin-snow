/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"
#include "snowline.h"

#include <QRectF>
#include <QString>

namespace KWin
{
class EffectWindow;
class LogicalOutput;
}

namespace Snow
{

/**
 * The three classes of Snow Catcher. Each class is enabled independently; a
 * disabled class is transparent to Flakes (spec: Model). Enabling arrives with
 * the configuration ticket -- for now every class is Solid.
 */
enum class CatcherClass {
    Window,
    Panel,
    Ground,
};

/** The class name as it appears in logs and, later, in configuration keys. */
QString catcherClassName(CatcherClass catcherClass);

/** Something to call @a window in a log line. Panels have no caption. */
QString windowLabel(const KWin::EffectWindow *window);

/**
 * A surface Flakes land on and accumulate against.
 *
 * A Catcher is defined entirely by its catching surface. `geometry().top()` is
 * the line Flakes land on, `geometry().left()` is the origin of the local X
 * axis a Snowline is indexed in, and `geometry().width()` is how far that axis
 * runs -- all in global *logical* pixels (ADR-0001), so a Catcher that moves
 * between outputs of different scale keeps its Snowline.
 *
 * For a window or a Panel the surface is `EffectWindow::frameGeometry()`, which
 * KWin documents as excluding both server-side and client-side drop shadows.
 * The ground is synthetic -- one per output, with no EffectWindow behind it --
 * and its surface is a zero-height strip along the bottom edge of its output.
 *
 * A Catcher owns its Snowline for exactly as long as it exists, which is what
 * makes Catcher lifetime the answer to every question about when accumulated
 * snow survives: a move or a minimise or a desktop switch never touches it, a
 * resize re-spans it, and closing the window takes it with the Catcher.
 */
class Catcher
{
public:
    Catcher(CatcherClass catcherClass, KWin::EffectWindow *window, KWin::LogicalOutput *output, const QRectF &geometry);

    CatcherClass catcherClass() const
    {
        return m_catcherClass;
    }

    /** The window this Catcher belongs to, or nullptr for the ground. */
    KWin::EffectWindow *window() const
    {
        return m_window;
    }

    /** The output this Catcher is on. The simulation is per output (spec: Rendering). */
    KWin::LogicalOutput *output() const
    {
        return m_output;
    }
    void setOutput(KWin::LogicalOutput *output)
    {
        m_output = output;
    }

    QRectF geometry() const
    {
        return m_geometry;
    }

    /**
     * Follow the catching surface. A move leaves the Snowline untouched; a
     * change of width re-spans it, anchored at the left edge (ADR-0001).
     */
    void setGeometry(const QRectF &geometry);

    /** The snow this Catcher is holding. */
    Snowline &snowline()
    {
        return m_snowline;
    }
    const Snowline &snowline() const
    {
        return m_snowline;
    }

    /**
     * The Column of this Catcher's Snowline that @a globalX falls in, or -1
     * when that X misses the Catcher. The one place the global X that Flakes
     * live in is turned into the local X a Snowline is indexed in.
     */
    int columnAt(qreal globalX) const
    {
        return m_snowline.columnAt(globalX - m_geometry.left());
    }

    /**
     * Whether this Catcher's class is enabled, and so Solid: Flakes stop on it
     * rather than falling through to whatever is below (spec: Model).
     *
     * A class, not a state: this stays true for a Catcher nobody can see, which
     * is what isCatching() below is for. It is the melt rate that reads it that
     * way (settings.h: meltRateFor), because a Snowline melting at the
     * accelerated rate is what "the class was switched off" looks like, and a
     * minimised window has not had anything switched off.
     */
    bool isSolid(const Settings &settings) const;

    /**
     * Whether this Catcher's window is off screen: minimised, an auto-hidden
     * Panel, or on a virtual desktop that is not the current one. Always false
     * for the ground, which has no window and never goes anywhere.
     *
     * Pushed in by CatcherRegistry (isConcealed()), the same way geometry is,
     * rather than read off the window here: it is one answer to all of those,
     * and asking once a frame beats hunting a signal for each of them.
     */
    bool isConcealed() const
    {
        return m_concealed;
    }
    void setConcealed(bool concealed)
    {
        m_concealed = concealed;
    }

    /**
     * Whether Flakes land on this Catcher right now: its class is Solid and it
     * is not concealed.
     *
     * The second half is what stops a Catcher nobody can see from taking snow
     * out of the air. Its own Snowline is untouched -- it is preserved across
     * the hide and comes back with the Catcher, which is what the spec asks for
     * (spec: Behaviour, "Panel auto-hidden", "Window minimised") -- but while it
     * is away the Flakes it would have caught carry on down to whatever is
     * below. Without this an auto-hidden Panel goes on filling invisibly and
     * leaves a bare strip of desktop the width of the screen; measured at 11.4
     * logical px on the Panel against 0.17 on the ground under it.
     */
    bool isCatching(const Settings &settings) const;

    /**
     * Whether this Catcher's Cap ramps to nothing across its outermost Columns,
     * to approximate the rounded corners of a window decoration. Every class
     * does but the ground, which has no corners: it is the bottom edge of a
     * whole output.
     */
    bool hasRoundedCorners() const
    {
        return m_catcherClass != CatcherClass::Ground;
    }

    /**
     * The line a Flake lands on over @a column, in global logical pixels: the
     * catching surface, raised by the snow standing there. It rises as a pile
     * grows, which is why a Flake is tested against this rather than against
     * the Catcher's top edge.
     *
     * "Standing there" is the *drawn* depth, so the corner ramp is applied here
     * too: ramping only the Cap would pile snow up invisibly over a rounded
     * corner and then hold Flakes in the air on top of it.
     */
    qreal surfaceAt(int column) const;

    /** Land @a mass logical pixels of snow in @a column. */
    void deposit(int column, qreal mass, const Settings &settings);

    /**
     * Let this Catcher's snow settle for @a delta seconds: spikes relax toward
     * an angle of repose and every Column melts a little.
     *
     * Called once per frame of the output this Catcher is on, whether or not
     * the class is Solid -- a disabled class stops catching, it does not stop
     * melting. It melts at the accelerated rate while it is disabled, which is
     * what turns switching a class off into snow going rather than snow
     * disappearing (settings.h: meltRateFor()).
     */
    void settle(qreal delta, const Settings &settings);

    /** One line identifying this Catcher, for logging. */
    QString describe() const;

private:
    const CatcherClass m_catcherClass;
    KWin::EffectWindow *const m_window;
    KWin::LogicalOutput *m_output;
    QRectF m_geometry;
    Snowline m_snowline;
    bool m_concealed = false;
};

} // namespace Snow
