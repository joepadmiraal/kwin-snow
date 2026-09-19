/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "catcher.h"

#include "cap.h"

#include <core/output.h>
#include <effect/effectwindow.h>

namespace Snow
{

QString catcherClassName(CatcherClass catcherClass)
{
    switch (catcherClass) {
    case CatcherClass::Window:
        return QStringLiteral("window");
    case CatcherClass::Panel:
        return QStringLiteral("panel");
    case CatcherClass::Ground:
        return QStringLiteral("ground");
    }
    Q_UNREACHABLE();
}

QString windowLabel(const KWin::EffectWindow *window)
{
    const QString caption = window->caption();
    if (!caption.isEmpty()) {
        return caption;
    }
    const QString windowClass = window->windowClass();
    return windowClass.isEmpty() ? QStringLiteral("<unnamed>") : windowClass;
}

Catcher::Catcher(CatcherClass catcherClass, KWin::EffectWindow *window, KWin::LogicalOutput *output, const QRectF &geometry)
    : m_catcherClass(catcherClass)
    , m_window(window)
    , m_output(output)
    , m_geometry(geometry)
    , m_snowline(geometry.width())
{
}

void Catcher::setGeometry(const QRectF &geometry)
{
#ifndef QT_NO_DEBUG
    // Moving a Catcher is free: a Snowline is indexed in the Catcher's own
    // local X, so nothing short of a change of width can reach it (ADR-0001).
    // A drag calls this on every frame, so debug builds check that rather than
    // take it on trust.
    const bool sameWidth = geometry.width() == m_geometry.width();
    const Snowline before = m_snowline;
#endif

    m_geometry = geometry;
    m_snowline.setWidth(geometry.width());

#ifndef QT_NO_DEBUG
    // Inside the guard as well as the snapshot above: a release Q_ASSERT still
    // compiles its condition, so a bare one here does not build without the
    // two locals it names.
    Q_ASSERT(!sameWidth || m_snowline == before);
#endif
}

qreal Catcher::surfaceAt(int column) const
{
    const qreal depth = hasRoundedCorners()
        ? m_snowline.depth(column) * capCornerRamp(column, m_snowline.columnCount())
        : m_snowline.depth(column);
    return m_geometry.top() - depth;
}

bool Catcher::isSolid(const Settings &settings) const
{
    switch (m_catcherClass) {
    case CatcherClass::Window:
        return settings.snowOnWindows;
    case CatcherClass::Panel:
        return settings.snowOnPanels;
    case CatcherClass::Ground:
        return settings.snowOnDesktop;
    }
    Q_UNREACHABLE();
}

bool Catcher::isCatching(const Settings &settings) const
{
    return isSolid(settings) && !isConcealed();
}

void Catcher::deposit(int column, qreal mass, const Settings &settings)
{
    m_snowline.deposit(column, mass, settings.maxDepth);
}

void Catcher::settle(qreal delta, const Settings &settings)
{
    if (m_snowline.isEmpty()) {
        // Most Catchers on a busy desktop are bare -- anything under a window,
        // and anything that has just melted out -- and there is nothing for
        // either pass to do to a Snowline of zeroes.
        return;
    }

    m_snowline.relax(delta);

    // A class that has been switched off stops catching at once, but its
    // Snowlines are not cleared: they melt, faster than they were melting, and
    // the Caps go with them (spec: Behaviour, "Settings changed"). They go on
    // relaxing while they do, because a pile that is melting is still a pile.
    m_snowline.melt(delta, meltRateFor(isSolid(settings), settings.meltRate));
}

QString Catcher::describe() const
{
    return QStringLiteral("%1 on %2  surface y=%3 x=[%4,%5]  %6  snowline: %7")
        .arg(catcherClassName(m_catcherClass),
             m_output ? m_output->name() : QStringLiteral("<no output>"))
        .arg(m_geometry.top())
        .arg(m_geometry.left())
        .arg(m_geometry.right())
        .arg(m_window ? windowLabel(m_window) : QStringLiteral("(the ground)"),
             m_snowline.describe());
}

} // namespace Snow
