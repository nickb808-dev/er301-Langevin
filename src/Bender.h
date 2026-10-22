/* Bender.h — a clock bender for the ER-301
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nicholas Breinich (nickb808) — see LICENSE and NOTICE.
 *
 * After the ADDAC406 Clock Bender (user's guide rev. 1, April 2026): two
 * outputs, A and B, each a pulse train whose TEMPO follows a curve over a
 * cycle counted in beats of the input clock —
 *   stage 1  input tempo -> input x M   (left curve), balance * D beats
 *   stage 2  input x M  -> input tempo  (right curve), the rest of D
 *   pause    P beats: REST (silent) or HOLD (the last tempo, steady)
 * then the next cycle starts ON an input beat, with a pulse, so every cycle
 * lands back in time with the clock while no two intervals inside it are
 * alike. Loop, or one-shot (wait for reset). Reset restarts both on the next
 * input beat.
 *
 * Offered as LOCAL sources, as Huygens does it: the unit's chain output is
 * the channel's audio, passed through bit-exact; the taps are its sub-outs
 * after the audio (stolmine multi-out, M6 in the Locals chooser):
 *   Tap1  A triggers        Tap3  A rate (CV)
 *   Tap2  B triggers        Tap4  B rate (CV)
 * A rate tap is log2(current tempo / input tempo) / 10 — the ER-301's 1 V/oct
 * scale (1.0 = 10 octaves) — so into Noether's V/Oct the loop's speed
 * follows the bent clock exactly. 0 at the input tempo.
 *
 * CURVES without libm: c(u) = u / (u + k (1 - u)), k = 2^(-3 curve), curve
 * in [-1, 1]: 0 linear, + fast start (log-like), - slow start (exp-like).
 * One divide per sample per channel; no pow.
 *
 * CLOCK: the period is measured edge to edge; the beat position between
 * edges is interpolated from it. No edge for 2 periods = the clock stopped:
 * the outputs stop (silent, rate 0), and the next edge starts a new cycle
 * (Nick, 2026-10-22 — unlike the 406, which keeps bending forever).
 *
 * Audio-thread rules (standard §2, §5, §5a): no allocation, no integer
 * divide or modulo, every inlet sanitised at the read, positive-test guards
 * on the phase accumulators. logf twice per block per channel (the rate
 * taps, interpolated across the block) — libm exp/log are fine on the
 * device; only sin/cos miscompute (habitat memory). */

#pragma once

#ifndef SWIGLUA
#include <od/objects/Object.h>
#include <od/config.h>
#include <cstdint>
#endif

namespace bender {

class Bender : public od::Object
{
public:
    Bender();
    virtual ~Bender();

    /* ── THE LUA-CALLABLE API ───────────────────────────────────────────
     * EVERYTHING Lua CALLS ON THE HEAD IS DECLARED HERE, ABOVE THE GUARD.
     * Declarations only, plain types only (standard §1a). */

    float getInputBpm();          // measured; 0 = no clock
    float getBpm(int ch);         // channel 0 (A) / 1 (B): its current tempo; 0 = silent
    float getRate(int ch);        // current tempo / input tempo (1 = the input's)
    int   getBeat(int ch);        // beat within the cycle, 1-based; 0 = not running
    int   getTotal(int ch);       // beats in the cycle (duration + pause)
    int   getRunning(int ch);     // 1 while the channel is in a cycle
    int   getLamp(int ch);        // 1 while its trigger output is lit (display)
    int   hasClock();
    // The curve for the display: the tempo ratio at cycle position x in
    // [0, 1] with the channel's current settings (UI thread; scalars only).
    float curveAt(int ch, float x);

#ifndef SWIGLUA
    void process() override;

    od::Inlet  mLeftIn   {"Left In"};
    od::Inlet  mRightIn  {"Right In"};
    od::Outlet mLeftOut  {"Left Out"};
    od::Outlet mRightOut {"Right Out"};

    od::Inlet  mClockIn  {"Clock"};
    od::Inlet  mResetIn  {"Reset"};

    od::Inlet  mMultA    {"A Mult"};
    od::Inlet  mBeatsA   {"A Beats"};
    od::Inlet  mBalA     {"A Balance"};
    od::Inlet  mLeftA    {"A Left"};
    od::Inlet  mRightA   {"A Right"};
    od::Inlet  mPauseA   {"A Pause"};
    od::Inlet  mMultB    {"B Mult"};
    od::Inlet  mBeatsB   {"B Beats"};
    od::Inlet  mBalB     {"B Balance"};
    od::Inlet  mLeftB    {"B Left"};
    od::Inlet  mRightB   {"B Right"};
    od::Inlet  mPauseB   {"B Pause"};

    od::Outlet mTap1 {"Tap1"};   // A triggers
    od::Outlet mTap2 {"Tap2"};   // B triggers
    od::Outlet mTap3 {"Tap3"};   // A rate
    od::Outlet mTap4 {"Tap4"};   // B rate

    od::Option mRunA   {"A Run", 1};     // 1 loop, 2 one-shot (never 0: CHOICE_UNKNOWN)
    od::Option mRunB   {"B Run", 1};
    od::Option mHoldA  {"A Hold", 1};    // pause: 1 rest, 2 hold
    od::Option mHoldB  {"B Hold", 1};

    static constexpr int   kChannels   = 2;
    static constexpr float kMultMin    = 0.25f;
    static constexpr float kMultMax    = 8.0f;
    static constexpr int   kBeatsMax   = 64;
    static constexpr int   kPauseMax   = 32;
    static constexpr float kRateMin    = 0.05f;    // a curve never stalls the output
    static constexpr int   kTrigFrames = 240;      // 5 ms
    static constexpr int   kMinPeriod  = 96;       // 2 ms: faster edges are bounce
    static constexpr int   kClkTimeout = 4 * 48000;
    static constexpr int   kLampFrames = 2400;     // 50 ms: a trigger stays visible
    static constexpr float kSampleRate = 48000.0f;

    // one channel's settings, read once per block (and kept for the display)
    struct Shape {
        float mult = 2.0f;
        int   beats = 8;
        float bal = 1.0f;
        float left = 0.0f, right = 0.0f;
        int   pause = 0;
        bool  hold = false;
        bool  oneShot = false;
        float kL = 1.0f, kR = 1.0f;   // curve constants 2^(-3 curve), per block (expf)
    };
    // tempo ratio at beat position b (0 <= b < beats + pause) of shape s;
    // sets `silent` during a REST pause
    static float rateAt(const Shape &s, double b, bool &silent);

private:
    struct Chan {
        Shape  s;              // latched settings
        bool   running = false;
        int    beat = 0;       // whole input beats since this cycle began
        double phase = 0.0;    // output pulses accumulated this cycle
        float  rate = 1.0f;    // current ratio (display / the rate tap)
        bool   silent = true;
        int    high = 0, lamp = 0;
        float  rateCv = 0.0f;  // the rate tap's value at the end of the last block
    };
    Chan   mCh[kChannels];
    float  mPrevClk = 0.0f, mPrevReset = 0.0f;
    int    mSince = 1 << 30;   // samples since the last accepted edge
    int    mPeriod = 0;        // last measured interval (0 = unknown)
    int    mEdges = 0;
    bool   mResetPending = false;
    bool   mStopped = true;    // no clock: outputs silent until the next edge

    void onEdge();
    void startCycle(Chan &c);
    void fire(Chan &c, float rate);
    void readShape(Shape &s, od::Inlet &mult, od::Inlet &beats, od::Inlet &bal,
                   od::Inlet &left, od::Inlet &right, od::Inlet &pause,
                   od::Option &run, od::Option &hold);

    static inline float sanitize(float x)
    {
        union { float f; uint32_t u; } v; v.f = x;
        return ((v.u & 0x7f800000u) == 0x7f800000u) ? 0.0f : x;
    }
    static inline float clampf(float x, float lo, float hi)
    {
        return x < lo ? lo : (x > hi ? hi : x);
    }
#endif // SWIGLUA
};

} // namespace bender
