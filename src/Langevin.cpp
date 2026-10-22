/* Langevin.cpp — a clock bender for the ER-301
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nicholas Breinich (nickb808) — see LICENSE and NOTICE.
 *
 * See Langevin.h for the design. Audio-thread rules: no allocation, no integer
 * divide or modulo, inlets sanitised at the read, NaN-proof accumulators. */

#include "Langevin.h"
#include <cmath>

namespace langevin {

Langevin::Langevin()
{
    addInput(mLeftIn);
    addInput(mRightIn);
    addOutput(mLeftOut);
    addOutput(mRightOut);
    addInput(mClockIn);
    addInput(mResetIn);
    addInput(mMultA);
    addInput(mBeatsA);
    addInput(mBalA);
    addInput(mLeftA);
    addInput(mRightA);
    addInput(mPauseA);
    addInput(mMultB);
    addInput(mBeatsB);
    addInput(mBalB);
    addInput(mLeftB);
    addInput(mRightB);
    addInput(mPauseB);
    addOutput(mTap1);
    addOutput(mTap2);
    addOutput(mTap3);
    addOutput(mTap4);
    addOption(mRunA);
    addOption(mRunB);
    addOption(mHoldA);
    addOption(mHoldB);
}

Langevin::~Langevin() {}

/* ── the curve ─────────────────────────────────────────────────────────── */

// c(u) = u / (u + k (1 - u)): 0 -> 0, 1 -> 1, k = 1 linear, k < 1 a fast
// start (log-like), k > 1 a slow start (exp-like). No libm.
static inline float shapeCurve(float u, float k)
{
    if (!(u > 0.0f)) return 0.0f;
    if (!(u < 1.0f)) return 1.0f;
    return u / (u + k * (1.0f - u));
}

float Langevin::rateAt(const Shape &s, double b, bool &silent)
{
    silent = false;
    const float M = s.mult;
    const float D = (float)s.beats;
    const float D1 = s.bal * D;               // stage 1: input -> x M
    const float D2 = D - D1;                  // stage 2: x M -> input
    const float bb = (float)b;
    if (bb < D1) {
        return 1.0f + (M - 1.0f) * shapeCurve(bb / D1, s.kL);
    }
    if (bb < D) {
        return 1.0f + (M - 1.0f) * (1.0f - shapeCurve((bb - D1) / D2, s.kR));
    }
    // the pause: where the curve ended (the input tempo after a stage 2,
    // x M when there is none), held — or silent
    if (!s.hold) silent = true;
    return D2 > 0.0f ? 1.0f : M;
}

/* ── Lua-callable API (UI thread; scalars only) ────────────────────────── */

int Langevin::hasClock()
{
    if (mEdges == 0 || mStopped) return 0;
    return 1;
}

float Langevin::getInputBpm()
{
    if (!hasClock() || mPeriod <= 0) return 0.0f;
    return 60.0f * kSampleRate / (float)mPeriod;
}

float Langevin::getRate(int ch)
{
    if (ch < 0 || ch >= kChannels) return 1.0f;
    return mCh[ch].rate;
}

float Langevin::getBpm(int ch)
{
    if (ch < 0 || ch >= kChannels) return 0.0f;
    const Chan &c = mCh[ch];
    if (!c.running || c.silent) return 0.0f;
    return getInputBpm() * c.rate;
}

int Langevin::getBeat(int ch)
{
    if (ch < 0 || ch >= kChannels || !mCh[ch].running) return 0;
    return mCh[ch].beat + 1;
}

int Langevin::getTotal(int ch)
{
    if (ch < 0 || ch >= kChannels) return 0;
    return mCh[ch].s.beats + mCh[ch].s.pause;
}

int Langevin::getRunning(int ch)
{
    return (ch >= 0 && ch < kChannels && mCh[ch].running) ? 1 : 0;
}

int Langevin::getLamp(int ch)
{
    if (ch < 0 || ch >= kChannels) return 0;
    return (mCh[ch].lamp > 0 || mCh[ch].high > 0) ? 1 : 0;
}

float Langevin::curveAt(int ch, float x)
{
    if (ch < 0 || ch >= kChannels) return 1.0f;
    const Shape &s = mCh[ch].s;
    if (!(x >= 0.0f)) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    bool silent;
    return rateAt(s, (double)(x * (float)(s.beats + s.pause)), silent);
}

/* ── audio thread ──────────────────────────────────────────────────────── */

void Langevin::readShape(Shape &s, od::Inlet &mult, od::Inlet &beats, od::Inlet &bal,
                       od::Inlet &left, od::Inlet &right, od::Inlet &pause,
                       od::Option &run, od::Option &hold)
{
    s.mult  = clampf(sanitize(mult.buffer()[0]), kMultMin, kMultMax);
    s.beats = (int)(clampf(sanitize(beats.buffer()[0]), 1.0f, (float)kBeatsMax) + 0.5f);
    s.bal   = clampf(sanitize(bal.buffer()[0]), 0.0f, 1.0f);
    s.left  = clampf(sanitize(left.buffer()[0]), -1.0f, 1.0f);
    s.right = clampf(sanitize(right.buffer()[0]), -1.0f, 1.0f);
    s.pause = (int)(clampf(sanitize(pause.buffer()[0]), 0.0f, (float)kPauseMax) + 0.5f);
    s.oneShot = run.value() == 2;
    s.hold    = hold.value() == 2;
    // k = 2^(-3 curve): curve +1 -> 1/8 (fast start), -1 -> 8 (slow start)
    s.kL = expf(-2.07944154f * s.left);
    s.kR = expf(-2.07944154f * s.right);
}

void Langevin::startCycle(Chan &c)
{
    c.running = true;
    c.beat = 0;
    c.phase = 0.0;
    bool silent;
    c.rate = rateAt(c.s, 0.0, silent);
    c.silent = silent;
    if (!silent) fire(c, c.rate);              // every cycle begins ON a beat
}

void Langevin::fire(Chan &c, float rate)
{
    int len = kTrigFrames;
    if (mPeriod > 0 && rate > 0.0f) {
        const int half = (int)((float)mPeriod / rate) >> 1;   // half the output interval
        if (half > 0 && len > half) len = half;
    }
    if (len < 1) len = 1;
    c.high = len;
    c.lamp = len > kLampFrames ? len : kLampFrames;
}

void Langevin::onEdge()
{
    const bool hadPeriod = mPeriod > 0;   // was the beat that just ended integrated?
    if (mEdges > 0 && mSince < kClkTimeout && !mStopped) mPeriod = mSince;
    else mPeriod = 0;                          // first edge / after a stop: unknown
    if (mEdges < (1 << 30)) ++mEdges;
    mSince = 0;

    const bool resume = mStopped;
    mStopped = false;
    const bool reset = mResetPending;
    mResetPending = false;
    for (int k = 0; k < kChannels; ++k) {
        Chan &c = mCh[k];
        if (resume || reset) { startCycle(c); continue; }   // clock back / reset: beat 1
        if (!c.running) continue;                            // a finished one-shot waits
        // the first beat after a (re)start had no period to integrate with:
        // credit it now with its mid-beat rate (x1 owes exactly 1 pulse)
        if (!hadPeriod) {
            bool silent;
            float rr = rateAt(c.s, (double)c.beat + 0.5, silent);
            if (rr < kRateMin) rr = kRateMin;
            c.phase += (double)rr;
        }
        ++c.beat;
        if (c.beat >= c.s.beats + c.s.pause) {
            if (c.s.oneShot) { c.running = false; c.silent = true; c.rate = 1.0f; }
            else startCycle(c);
        } else {
            // a pulse due within 2 % of an interval of this edge lands ON it
            // (rounding in the per-sample sum must not slip it a sample)
            // int, not long long: (long long) pulls in libgcc's __aeabi_d2lz /
            // __aeabi_l2d — both ARE in the firmware's app/extra.sym, but an
            // int keeps the imports to what noether / huygens already load
            // on the device; the phase is guarded below 1e9, inside an int
            const double whole = (double)(int)(c.phase + 0.5);
            if (c.phase > whole - 0.02 && c.phase < whole + 0.02) c.phase = whole;
        }
    }
}

void Langevin::process()
{
    // the audio: an exact pass-through (standard §4)
    {
        const float *aL = mLeftIn.buffer(), *aR = mRightIn.buffer();
        float *oL = mLeftOut.buffer(), *oR = mRightOut.buffer();
        for (int i = 0; i < FRAMELENGTH; ++i) { oL[i] = aL[i]; oR[i] = aR[i]; }
    }
    readShape(mCh[0].s, mMultA, mBeatsA, mBalA, mLeftA, mRightA, mPauseA, mRunA, mHoldA);
    readShape(mCh[1].s, mMultB, mBeatsB, mBalB, mLeftB, mRightB, mPauseB, mRunB, mHoldB);

    const float *clk = mClockIn.buffer();
    const float *rst = mResetIn.buffer();
    float *trig[kChannels] = { mTap1.buffer(), mTap2.buffer() };
    float *rcv[kChannels]  = { mTap3.buffer(), mTap4.buffer() };

    // sticky-state guards (positive tests, standard §5)
    for (int k = 0; k < kChannels; ++k) {
        Chan &c = mCh[k];
        if (!(c.phase >= 0.0 && c.phase < 1.0e9)) c.phase = 0.0;
        if (!(c.rate > 0.0f && c.rate < 100.0f)) c.rate = 1.0f;
        if (!(c.rateCv > -10.0f && c.rateCv < 10.0f)) c.rateCv = 0.0f;
    }

    float prevC = mPrevClk, prevR = mPrevReset;
    for (int i = 0; i < FRAMELENGTH; ++i) {
        const float cin = sanitize(clk[i]);
        const float r = sanitize(rst[i]);
        if (r > 0.5f && prevR <= 0.5f) mResetPending = true;
        prevR = r;

        // time advances first: an edge P samples after the last measures P
        if (mSince < (1 << 30)) ++mSince;
        const bool edge = cin > 0.5f && prevC <= 0.5f && mSince >= kMinPeriod;
        prevC = cin;
        if (edge) onEdge();
        if (!edge && !mStopped && mPeriod > 0 && mSince > 2 * mPeriod + kMinPeriod) {
            // the clock stopped: silence, and a new cycle at the next edge
            mStopped = true;
            for (int k = 0; k < kChannels; ++k) { mCh[k].running = false; mCh[k].silent = true; mCh[k].rate = 1.0f; }
        }
        if (!mStopped && mPeriod > 0) {
            // phase = pulses owed up to THIS sample: test, then advance by
            // the rate over this sample (so x1 lands exactly on the edges)
            const double inv = 1.0 / (double)mPeriod;
            double frac = (double)mSince * inv;
            if (frac > 0.9999) frac = 0.9999;  // a late edge: hold just before the beat
            for (int k = 0; k < kChannels; ++k) {
                Chan &c = mCh[k];
                if (!c.running) continue;
                bool silent;
                float rate = rateAt(c.s, (double)c.beat + frac, silent);
                if (rate < kRateMin) rate = kRateMin;
                c.rate = rate; c.silent = silent;
                if (c.phase >= 1.0) {
                    c.phase -= 1.0;
                    if (!silent && c.high == 0) fire(c, rate);
                }
                c.phase += (double)rate * inv;
            }
        }
        for (int k = 0; k < kChannels; ++k) {
            Chan &c = mCh[k];
            trig[k][i] = c.high > 0 ? 1.0f : 0.0f;
            if (c.high > 0) --c.high;
        }
    }
    mPrevClk = prevC; mPrevReset = prevR;

    // the rate taps: log2(rate) / 10 (1 V/oct, ER-301 scale), one logf per
    // block per channel, interpolated from the last block's value
    for (int k = 0; k < kChannels; ++k) {
        Chan &c = mCh[k];
        const float target = (c.running && !mStopped) ? 0.144269504f * logf(c.rate) : 0.0f;  // log2(r)/10
        const float from = c.rateCv;
        const float step = (target - from) * (1.0f / (float)FRAMELENGTH);
        for (int i = 0; i < FRAMELENGTH; ++i) rcv[k][i] = from + step * (float)(i + 1);
        c.rateCv = target;
        c.lamp -= FRAMELENGTH;
        if (c.lamp < 0) c.lamp = 0;
    }
}

} // namespace langevin
