/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "suspension.h"

#include "snowlogging.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <idledetector.h>
#include <input.h>

namespace Snow
{

// How long the session has to go untouched before the snow stops.
//
// Long enough that it is not something anybody watches happen: Plasma's own
// defaults dim the screen at five minutes, so by the time this fires the
// desktop is on its way out anyway. Deliberately not a setting -- neither of
// the spec's Configuration tables has a key for it, and a user who wants the
// snow to cost less sooner has the frame rate cap, which is the knob that
// actually answers that question.
static constexpr std::chrono::minutes s_idleTimeout(5);

Suspension::Suspension(QObject *parent)
    : QObject(parent)
{
    // FollowsInhibitors is what keeps the snow falling through a film: a player
    // that says the user is watching it holds the session awake, and KWin's
    // idle detection already honours that. Without it the snow would stop
    // halfway through anything watched rather than clicked through.
    //
    // The detector registers itself with KWin's input redirection when it is
    // constructed, so there has to be one to register with; there is no
    // compositor without input, but this is a plugin and a null dereference in
    // here takes the whole session down.
    if (KWin::input()) {
        m_idleDetector = std::make_unique<KWin::IdleDetector>(s_idleTimeout,
                                                              KWin::IdleDetector::OperatingMode::FollowsInhibitors);

        connect(m_idleDetector.get(), &KWin::IdleDetector::idle, this, [this] {
            m_isIdle = true;
            update();
        });
        connect(m_idleDetector.get(), &KWin::IdleDetector::resumed, this, [this] {
            m_isIdle = false;
            update();
        });
    } else {
        qCWarning(KWIN_EFFECT_SNOW) << "No input redirection: the snow will not stop when the session goes idle";
    }

    connect(KWin::effects, &KWin::EffectsHandler::screenLockingChanged, this, &Suspension::update);
    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &Suspension::watchActiveWindow);

    // The backstop, and the reason it is worth having one: the cost of missing
    // a reason to start again is snow that never falls again, where the cost of
    // asking too often is two pointer reads. A fullscreen window that goes away
    // without anything else being activated is the case in mind.
    connect(KWin::effects, &KWin::EffectsHandler::stackingOrderChanged, this, &Suspension::update);

    // The effect can be switched on at any time, so the world already exists:
    // evaluate it rather than assume the snow is running. Answering before the
    // first update() is what stops that one reporting the load as a change, so
    // that the line below is the one that reports it -- once, always, and
    // whichever way it came out.
    m_reason = currentReason();
    watchActiveWindow();

    qCDebug(KWIN_EFFECT_SNOW).noquote() << "Snow" << describe();
}

Suspension::~Suspension() = default;

bool Suspension::isDrawn() const
{
    if (!m_reason) {
        return true;
    }

    switch (*m_reason) {
    case SuspensionReason::FullscreenWindow:
    case SuspensionReason::SessionLocked:
        return false;
    case SuspensionReason::SessionIdle:
        return true;
    }

    return true;
}

QString Suspension::describe() const
{
    if (!m_reason) {
        return QStringLiteral("falling");
    }

    switch (*m_reason) {
    case SuspensionReason::FullscreenWindow:
        return QStringLiteral("suspended: a fullscreen window is active");
    case SuspensionReason::SessionLocked:
        return QStringLiteral("suspended: the session is locked");
    case SuspensionReason::SessionIdle:
        return QStringLiteral("frozen: the session is idle");
    }

    return QStringLiteral("falling");
}

std::optional<SuspensionReason> Suspension::currentReason() const
{
    // The order is not only for the log. The two reasons that take the effect
    // out of the frame come first, so that a fullscreen film left running until
    // the session goes idle is still reported as the fullscreen window it is --
    // reporting it as idle would leave the snow drawn over it.
    if (KWin::effects->isScreenLocked()) {
        return SuspensionReason::SessionLocked;
    }

    // "Fullscreen window active" (spec: Behaviour) read literally: the window
    // with the focus. A fullscreen window on one output and work on another is
    // then snowed on again the moment the work is what has the focus, which is
    // also the moment its desktop is what the user is looking at.
    const KWin::EffectWindow *active = KWin::effects->activeWindow();
    if (active && active->isFullScreen()) {
        return SuspensionReason::FullscreenWindow;
    }

    if (m_isIdle) {
        return SuspensionReason::SessionIdle;
    }

    return {};
}

void Suspension::watchActiveWindow()
{
    // Only the active window's fullscreen state matters, so there is only ever
    // one of these connections rather than one per window: a window that goes
    // fullscreen while it has the focus is the ordinary case, and one that was
    // already fullscreen when it got the focus arrives through windowActivated.
    QObject::disconnect(m_fullScreenWatch);

    if (KWin::EffectWindow *active = KWin::effects->activeWindow()) {
        m_fullScreenWatch = connect(active, &KWin::EffectWindow::windowFullScreenChanged,
                                    this, &Suspension::update);
    }

    update();
}

void Suspension::update()
{
    const std::optional<SuspensionReason> reason = currentReason();
    if (reason == m_reason) {
        return;
    }

    m_reason = reason;

    // The one line that says whether the effect is costing anything, which is
    // what makes this checkable in the nested session; see docs/development.md.
    qCDebug(KWIN_EFFECT_SNOW).noquote() << "Snow" << describe();

    Q_EMIT changed();
}

} // namespace Snow
