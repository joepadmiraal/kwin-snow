/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "catcher.h"
#include "settings.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <unordered_map>

namespace KWin
{
class EffectWindow;
class LogicalOutput;
}

namespace Snow
{

/**
 * Which surfaces can catch snow right now, and for how long.
 *
 * The registry owns every Catcher and keeps the set in step with KWin: windows
 * and Panels appear and disappear with their EffectWindow, the ground appears
 * and disappears with its output, and a Catcher follows its window across
 * outputs. Because a Snowline hangs off a Catcher, Catcher lifetime *is*
 * Snowline lifetime (ADR-0001) -- which is why the rules for what is a Catcher
 * live in one place rather than being re-decided at paint time.
 *
 * Nothing here paints anything, and the only simulation it drives is the one
 * nothing else can reach: the Snowlines hang off Catchers, so settling them is
 * the registry's to do.
 */
class CatcherRegistry : public QObject
{
    Q_OBJECT

public:
    explicit CatcherRegistry(QObject *parent = nullptr);
    ~CatcherRegistry() override;

    /**
     * Every live Catcher, in paint order: the grounds first, then windows and
     * Panels bottom-to-top in KWin's stacking order. For the log, and for the
     * per-output list below; hit testing uses that one.
     */
    QList<Catcher *> catchers() const;

    /**
     * The Catchers the Flakes of @a output can land on, in paint order: that
     * output's ground first, then windows and Panels bottom-to-top in KWin's
     * stacking order. Hit testing walks this back to front (spec: Model).
     *
     * Per output because a landing line is a line in *global* coordinates, and
     * the outputs are laid out in the same global space: a surface that is
     * nowhere near this output can still lie straight across it. The ground of
     * a monitor standing above this one is exactly that -- a line along its
     * bottom edge, which is this output's top edge -- and left in, it catches
     * every Flake of this output in the first pixel row, so that the snow here
     * disappears while the monitor above goes on snowing (catchesOver).
     *
     * A window is still offered to every output its top edge crosses, so one
     * straddling two monitors side by side catches over the whole of that edge
     * and each output's Flakes land on the part of it in front of them; the
     * Columns outside an output are simply never hit, because a Flake never
     * leaves the output it belongs to.
     */
    QList<Catcher *> catchersFor(KWin::LogicalOutput *output) const;

    /** The Catcher for @a window, or nullptr when that window catches nothing. */
    Catcher *catcherFor(KWin::EffectWindow *window) const;

    /** The ground Catcher of @a output, or nullptr when the output is gone. */
    Catcher *ground(KWin::LogicalOutput *output) const;

    /**
     * Settle every Catcher on @a output for @a delta seconds -- relaxation and
     * melt, once per frame of that output.
     *
     * Per output rather than over everything, because a frame is per output: a
     * Catcher belongs to exactly one of them, so this settles each Snowline
     * exactly once however many outputs are drawing.
     */
    void settle(KWin::LogicalOutput *output, qreal delta, const Settings &settings);

private:
    /**
     * Every live Catcher whose surface the Flakes of @a output can land on, in
     * paint order, or every one of them when @a output is nullptr.
     *
     * The one walk over the world, because the two questions above are the same
     * walk with and without a filter -- and because the walk is also where each
     * Catcher is asked once a frame whether anybody can see it.
     */
    QList<Catcher *> collect(const KWin::LogicalOutput *output) const;

    void addOutput(KWin::LogicalOutput *output);
    void removeOutput(KWin::LogicalOutput *output);
    void outputGeometryChanged(KWin::LogicalOutput *output);

    void trackWindow(KWin::EffectWindow *window);
    void forgetWindow(KWin::EffectWindow *window);
    /** Bring @a window's Catcher, if it should have one, in step with KWin. */
    void refreshWindow(KWin::EffectWindow *window);
    void refreshAllWindows();

    /** Every Catcher and its Snowline, one per line. */
    QString describeAll() const;
    void logCatchers(const QString &reason);

    /**
     * Whether @a catcher's surface is one the Flakes of @a output can land on:
     * a landing line on this output, or this output's own ground.
     *
     * The ground is asked by identity rather than by geometry because a ground
     * line sits exactly on the edge between two stacked outputs, where no
     * arithmetic can tell whose it is. Everything else is asked by geometry,
     * because a window belongs to whichever outputs it lies over rather than to
     * the one KWin happens to call its screen.
     */
    static bool catchesOver(const Catcher *catcher, const KWin::LogicalOutput *output);

    /**
     * Whether @a window is off screen right now: hidden, minimised, put away by
     * show desktop, or on a virtual desktop or an activity that is not the
     * current one. Pushed into its Catcher once a frame by collect().
     */
    static bool isConcealed(const KWin::EffectWindow *window);

    static std::optional<CatcherClass> classify(const KWin::EffectWindow *window);
    static bool isTopEdgePanel(const QRectF &frameGeometry, const QRectF &outputGeometry);
    static QRectF groundGeometry(const KWin::LogicalOutput *output);

    std::unordered_map<KWin::LogicalOutput *, std::unique_ptr<Catcher>> m_grounds;
    std::unordered_map<KWin::EffectWindow *, std::unique_ptr<Catcher>> m_windowCatchers;
};

} // namespace Snow
