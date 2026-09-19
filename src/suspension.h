/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace KWin
{
class IdleDetector;
}

namespace Snow
{

/** Why the snow has stopped, when it has. */
enum class SuspensionReason {
    /** The active window is fullscreen: a game, a video, a presentation. */
    FullscreenWindow,
    /** The session is locked. */
    SessionLocked,
    /** Nobody has touched the session for long enough to have walked away. */
    SessionIdle,
};

/**
 * Whether the snow should be falling at all, and whether it should be on screen.
 *
 * The effect asks for a repaint of the whole screen for every frame it
 * animates, so what it costs is entirely a question of how often it does that
 * and whether it does it when nobody is looking. This is the second half of
 * that answer -- the frame rate cap is the first -- and it is the only thing
 * here that watches KWin for anything other than windows and outputs.
 *
 * The two answers are separate because the spec's Behaviour table asks for two
 * different things. A fullscreen window and a lock screen are surfaces the snow
 * must not be drawn over, so they suspend the effect *entirely*: it leaves the
 * frame, which also lets KWin scan a fullscreen surface straight out to the
 * display again. An idle session is still showing the desktop the snow is lying
 * on, so idle stops the animation and leaves the last frame standing.
 *
 * Nothing in here is reset on resume: a Snowline belongs to its Catcher and the
 * registries go on tracking windows while the snow is stopped, so the snow
 * picks up where it left off. It does not melt in the meantime either,
 * because the simulation is not stepped -- the clock skips rather than runs.
 */
class Suspension : public QObject
{
    Q_OBJECT

public:
    explicit Suspension(QObject *parent = nullptr);
    ~Suspension() override;

    /** Why the snow is stopped, or nothing at all when it is running. */
    std::optional<SuspensionReason> reason() const
    {
        return m_reason;
    }

    /**
     * Whether the snow should be moving.
     *
     * False the moment it is suspended for any reason. This is what stops the
     * frames being asked for, and asking for frames is what the effect costs.
     */
    bool isAnimated() const
    {
        return !m_reason.has_value();
    }

    /**
     * Whether the snow should still be drawn where it stands.
     *
     * False only for the two reasons that are something else being on screen:
     * the Flakes are drawn above the whole window stack (ADR-0003), so a
     * fullscreen window or a lock screen would have snow drawn over it. Idle is
     * not one of those -- the desktop is still what is on screen, and taking
     * the snow out of it would make it flicker away on the next unrelated
     * repaint, which is exactly the "looks broken" that ticket 08 refused to
     * accept for battery power.
     */
    bool isDrawn() const;

    /** One phrase for the log: what the snow is doing and why. */
    QString describe() const;

Q_SIGNALS:
    /** Either of the two answers above has changed. */
    void changed();

private:
    void update();

    /** Follow the fullscreen state of the window that has the focus, and only that one. */
    void watchActiveWindow();

    std::optional<SuspensionReason> currentReason() const;

    std::unique_ptr<KWin::IdleDetector> m_idleDetector;
    QMetaObject::Connection m_fullScreenWatch;
    std::optional<SuspensionReason> m_reason;
    bool m_isIdle = false;
};

} // namespace Snow
