// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Fitting the emulated sound to the clock of the audio device

#pragma once

#include <cmath>

// The emulation runs by the host's clock and the audio device by its own
// crystal, so the buffer between them slowly fills or runs dry: 100 ppm apart
// is usual, 0.2% has been seen. The level is held by playing the sound that
// much faster or slower, as a tiny constant change of the rate, resampled.
//
// It must not be done by dropping or repeating frames. One frame in 64, which
// it used to be, lands the band's edge in a held note as a quarter of a
// semitone for as long as it lasts, with sidebands of -30 dB at 689 Hz around
// every harmonic: the tone channels of the Агат ЯЗС howled with it, and every
// few seconds on a device further off. Beepers and speech hide all of this,
// sustained tones show it at once.
//
// So the rate never follows the level as it stands. What the device leaves in
// the buffer shakes by a slice of the emulation and by the scheduling of two
// threads, 5 ms and more on a busy host, while the drift it has to answer takes
// minutes. A low-pass of the level goes into a PI loop whose integral term ends
// up as the drift itself: once settled the rate stands still at that value,
// under a cent for a usual device, whatever the level does around the target.
//
// The target itself is a trade of latency for safety, and what a host needs is
// not known beforehand. It starts at the least that works on a healthy one and
// goes up when the reserve proves too small: the device found the buffer short,
// or was left next to nothing. By what was missing, at once, so that the second
// stall of the same size is not heard; the emulation catching up has already put
// the level there. It stays up until the next machine: coming down again on a
// host that needs it would buy a little latency with a click every few minutes.
// A stopped machine does not count, nor do the first seconds or a speed other
// than 1x: no reserve helps there. The setting of the window fixes the target
// instead (set_fixed()).
//
// A gap the rate could not bridge is not left to it, at 5 ms a second: at the
// target of the Large setting the sound would start a tenth of a semitone flat
// for half a minute. After a long gap, when it is heard anyway, the device
// waits until the buffer has the target (filling()), and what the emulation
// makes over it catching up is dropped at once (surplus()). So it is after a
// short gap that raised nothing, as under a set target too small for the host:
// that is heard as well, and the rate would stay at its limit working it off.
//
// Not a state of the machine: nothing here is saved. Called under the lock of
// the buffer, by the emulation thread (push) and the audio thread (measure)
class AudioRateMatch
{
    // The most the sound is moved: 8.6 cents, reached only far from the target
    const double m_max_rate = 0.005;
    // The low-pass of the level, seconds
    const double m_tau_level = 1.0;
    // Seconds to work off an error in the level: 10 ms of it move the rate by 0.25%
    const double m_tau_loop = 4.0;
    // A reserve under this share of the target has nearly failed
    const double m_near = 0.125;
    // Seconds between two raises of the target: a stall is one trouble
    const double m_raise_gap = 1.0;
    // Seconds of play before trouble counts: the buffer is still settling
    const double m_settle = 3.0;
    // Seconds after a long gap that a level over the target is the emulation
    // catching up, which takes a few ms a second of gap
    const double m_catch_up = 0.25;

public:
    // In frames: the target to start from and the most it can grow to
    void setup(double sample_rate, double target, double max_target)
    {
        m_sample_rate = sample_rate;
        m_base = target;
        m_max_target = max_target;
        m_target = target;
        m_fixed = false;
        m_short = 0;
        m_playing = 0;
        m_since_raise = m_raise_gap;
        m_filling = true;
        m_gap_left = 0;
        m_measured = false;
        m_level = target;
        m_drift = 0;
        m_step = 1;
        m_pos = 0;
        for (int i = 0; i < 4; i++) m_a[i] = m_b[i] = 0;
    }

    // A fixed target in frames, between setup()'s two; 0 goes back to the
    // automatic one, from the start. Right away, by a gap or a cut at the hand
    // of the user: the rate would take half a minute over it
    void set_fixed(double target)
    {
        m_fixed = target > 0;
        m_target = !m_fixed ? m_base
                 : (target < m_base) ? m_base
                 : (target > m_max_target) ? m_max_target : target;
        m_level = m_target;
        m_filling = true;
        m_gap_left = m_catch_up;
    }

    // What the buffer is kept at now, frames
    double target() const { return m_target; }

    // The device asked for "wanted" frames, found fewer or was made to wait,
    // and got none
    void underrun(double wanted)
    {
        m_short += wanted;
        if (m_short > m_max_target) m_filling = true;
    }

    // Whether the device has to wait for the buffer to fill to the target
    bool filling(double level, double wanted)
    {
        if (m_filling && level >= m_target + wanted) m_filling = false;
        return m_filling;
    }

    // The machine runs at another speed: the device is not meant to be kept
    // full, and too much asked of the host says nothing about the reserve at 1x
    void hold_off() { m_playing = 0; }

    // Frames to drop right after measure(): the emulation catching up on a gap
    // puts them on top of the target, and they would only be heard late
    double surplus(double level, double taken) const
    {
        return (m_gap_left > 0 && level > m_target + taken) ? level - m_target : 0;
    }

    // The device has just taken "taken" frames and left "level" in the buffer.
    // Only when it got all it asked for: a stopped machine or a run at maximum
    // speed gives it nothing, which says nothing about the two clocks
    void measure(double level, double taken)
    {
        const double dt = taken / m_sample_rate;
        m_since_raise += dt;
        m_playing += dt;
        if (m_gap_left > 0) m_gap_left -= dt;
        if (m_short > m_max_target) {
            // Stopped, or too long for any reserve
            m_playing = 0;
            m_gap_left = m_catch_up;
            m_level = m_target;
        } else if (m_short > 0) {
            if (!raise_target(m_short)) m_gap_left = m_catch_up;
        }
        else if (level < m_target * m_near)
            raise_target(0);
        m_short = 0;
        if (!m_measured) {
            m_level = level;
            m_measured = true;
        } else
            m_level += (level - m_level) * dt / (m_tau_level + dt);
        const double error = (m_level - m_target) / m_sample_rate;
        const double r = error / m_tau_loop + m_drift;
        // Not while pinned to the limit: the memory of a host that stalled
        // would outlast the stall. Damping 1
        if (std::fabs(r) < m_max_rate)
            m_drift += error * dt / (4 * m_tau_loop * m_tau_loop);
        m_step = 1 + ((r > m_max_rate) ? m_max_rate : (r < -m_max_rate) ? -m_max_rate : r);
    }

    // How much faster than it was made the sound plays: 0.001 is 0.1%
    double rate() const { return m_step - 1; }

    // A frame comes in, out(a, b) gets the frames for the device: one, or none
    // or two when the position slips past a whole frame
    template <typename Out>
    void push(float a, float b, Out out)
    {
        for (int i = 0; i < 3; i++) {
            m_a[i] = m_a[i + 1];
            m_b[i] = m_b[i + 1];
        }
        m_a[3] = a;
        m_b[3] = b;
        while (m_pos < 1.0) {
            const float t = (float)m_pos;
            out(cubic(m_a, t), cubic(m_b, t));
            m_pos += m_step;
        }
        m_pos -= 1.0;
    }

private:
    bool raise_target(double by)
    {
        if (m_fixed || m_playing < m_settle || m_since_raise < m_raise_gap || m_target >= m_max_target) return false;
        m_since_raise = 0;
        const double t = m_target + ((by > m_base / 2) ? by : m_base / 2);
        const double raised = (t > m_max_target) ? m_max_target : t;
        // What the device went without is on its way as the emulation catches
        // up, and the level with it: taken for a lack, it would slow the sound
        // to the limit for nothing. A near miss really is that low
        if (by > 0) m_level += raised - m_target;
        m_target = raised;
        return true;
    }

    // Catmull-Rom through four frames, t between the middle two. Its response
    // dips most halfway between frames, and the position sweeps through that
    // at the rate of the drift: -0.05 dB at 5 kHz and -0.7 dB at 10 kHz of
    // 44100 Hz, where linear would give -0.6 and -2.4 dB as a tremolo
    static float cubic(const float *x, float t)
    {
        const float c1 = 0.5f * (x[2] - x[0]);
        const float c2 = x[0] - 2.5f * x[1] + 2 * x[2] - 0.5f * x[3];
        const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
        return ((c3 * t + c2) * t + c1) * t + x[1];
    }

    double m_sample_rate = 44100;
    double m_base = 0;                  // the target to start from
    double m_max_target = 0;
    double m_target = 0;
    bool m_fixed = false;               // set_fixed(), no raises
    double m_short = 0;                 // frames the device went without since it last got any
    double m_playing = 0;               // seconds since the machine last stood
    double m_since_raise = 0;           // seconds since the target went up
    bool m_filling = true;              // the device waits for the target
    double m_gap_left = 0;              // seconds surplus() is still in force
    bool m_measured = false;
    double m_level = 0;                 // the level, low-passed
    double m_drift = 0;                 // the integral term
    double m_step = 1;                  // frames in per frame out
    double m_pos = 0;                   // position past m_a[1], in frames
    float m_a[4] = {0, 0, 0, 0};        // the last four frames of each channel
    float m_b[4] = {0, 0, 0, 0};
};
