/* Host verification harness for Bender (no hardware).
 * Build: g++ -std=c++11 -O2 -ffast-math -Itest/host -Isrc \
 *          src/Bender.cpp test/host/main.cpp -o test/t
 * Modes: ident · ramp · shape · pause · oneshot · reset · stop · rate ·
 *        audio · viz · nan · cpu · asan
 *
 * Every test prints the number it judged (standard §6/§10). Pulse counts
 * are judged against the integral of the tempo curve: pulses in a cycle =
 * 1 (the cycle's own, on its beat) + whole crossings of the integral of the
 * rate over the cycle's beats. */
#include "Bender.h"
#include "BenderCurve.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <ctime>

using bender::Bender;

static const int kSR = 48000;

struct Rig {
    Bender h;
    long t = 0;
    std::vector<long> rises[2];          // trigger rising edges, A / B
    std::vector<float> rate[2];          // the rate taps, every sample
    std::vector<long> clockEdges;
    float prev[2] = { 0.0f, 0.0f };
    bool keepRate = false;

    void set(od::Inlet &in, float v) { for (int i = 0; i < FRAMELENGTH; ++i) in.buffer()[i] = v; }
    void shape(int ch, float mult, float beats, float bal, float left, float right, float pause)
    {
        if (ch == 0) { set(h.mMultA, mult); set(h.mBeatsA, beats); set(h.mBalA, bal); set(h.mLeftA, left); set(h.mRightA, right); set(h.mPauseA, pause); }
        else         { set(h.mMultB, mult); set(h.mBeatsB, beats); set(h.mBalB, bal); set(h.mLeftB, left); set(h.mRightB, right); set(h.mPauseB, pause); }
    }
    template <class C, class R>
    void run(long frames, C clockAt, R resetAt)
    {
        const long blocks = (frames + FRAMELENGTH - 1) / FRAMELENGTH;
        float prevClk = h.mClockIn.buffer()[FRAMELENGTH - 1];
        for (long b = 0; b < blocks; ++b) {
            for (int i = 0; i < FRAMELENGTH; ++i) {
                const float c = clockAt(t + i) ? 1.0f : 0.0f;
                if (c > 0.5f && prevClk <= 0.5f) clockEdges.push_back(t + i);
                prevClk = c;
                h.mClockIn.buffer()[i] = c;
                h.mResetIn.buffer()[i] = resetAt(t + i) ? 1.0f : 0.0f;
            }
            h.process();
            const float *o[2] = { h.mTap1.buffer(), h.mTap2.buffer() };
            const float *rv[2] = { h.mTap3.buffer(), h.mTap4.buffer() };
            for (int k = 0; k < 2; ++k)
                for (int i = 0; i < FRAMELENGTH; ++i) {
                    if (o[k][i] > 0.5f && prev[k] <= 0.5f) rises[k].push_back(t + i);
                    prev[k] = o[k][i];
                    if (keepRate) rate[k].push_back(rv[k][i]);
                }
            t += FRAMELENGTH;
        }
    }
    void clear() { for (int k = 0; k < 2; ++k) { rises[k].clear(); rate[k].clear(); } clockEdges.clear(); }
    static int count(const std::vector<long> &v, long from, long to)
    {
        int n = 0; for (long x : v) if (x >= from && x < to) ++n; return n;
    }
};

struct Steady {
    long P, first;
    bool operator()(long t) const { return t >= first && ((t - first) % P) < 4; }
};
struct Never { bool operator()(long) const { return false; } };

// the integral of the rate over [0, B] beats of a shape, numerically (fine)
static double integral(const Bender::Shape &s, double B)
{
    const int n = 200000;
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        bool silent;
        const double b = (i + 0.5) * B / n;
        const float r = Bender::rateAt(s, b, silent);
        sum += silent ? 0.0 : (double)r;
    }
    return sum * B / n;
}

static Bender::Shape mk(float mult, int beats, float bal, float left, float right, int pause, bool hold = false)
{
    Bender::Shape s;
    s.mult = mult; s.beats = beats; s.bal = bal; s.left = left; s.right = right; s.pause = pause; s.hold = hold;
    s.kL = expf(-2.07944154f * left); s.kR = expf(-2.07944154f * right);
    return s;
}

/* ── modes ─────────────────────────────────────────────────────────────── */

// x1: the output is the clock, edge for edge
static int t_ident()
{
    Rig r;
    r.shape(0, 1.0f, 4, 0.5f, 0, 0, 0);
    r.shape(1, 1.0f, 4, 1.0f, 0.7f, -0.3f, 0);
    const long P = 4800, first = 300;
    r.run(first + 40 * P + 10, Steady{P, first}, Never());
    int off = 0;
    for (int k = 0; k < 2; ++k) for (long x : r.rises[k]) if ((x - first) % P != 0) ++off;
    const bool ok = r.rises[0].size() == 41 && r.rises[1].size() == 41 && off == 0;
    printf("ident: x1 -> %zu / %zu pulses (expect 41), %d not on a clock edge  %s\n",
           r.rises[0].size(), r.rises[1].size(), off, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

// A linear ramp x1 -> x4 over 8 beats: pulses per cycle = 1 + floor of the
// integral (20 -> 20, the 20th crossing IS the next cycle's start); every
// cycle starts on a clock edge; intervals strictly shrink inside a cycle
// (the 406's "no interval between two pulses is the same").
static int t_ramp()
{
    Rig r;
    r.shape(0, 4.0f, 8, 1.0f, 0, 0, 0);
    r.shape(1, 1.0f, 8, 1.0f, 0, 0, 0);
    const long P = 4800, first = 300;
    r.run(first + 8 * P * 6 + 10, Steady{P, first}, Never());
    const Bender::Shape s = mk(4.0f, 8, 1.0f, 0, 0, 0);
    const double I = integral(s, 8.0);
    const int expect = (int)floor(I + 1e-6);              // the start + crossings 1..I-1
    int bad = 0, notOnEdge = 0, notShrinking = 0;
    for (int c = 1; c < 6; ++c) {                           // cycle 0 has no period at its start
        const long a = first + (long)c * 8 * P, b = a + 8 * P;
        const int n = Rig::count(r.rises[0], a, b);
        if (n != expect) ++bad;
        bool startOnEdge = false;
        for (long x : r.rises[0]) if (x == a) startOnEdge = true;
        if (!startOnEdge) ++notOnEdge;
        long prevGap = 1L << 40, last = -1;
        for (long x : r.rises[0]) if (x >= a && x < b) {
            if (last >= 0) { const long g = x - last; if (g >= prevGap) ++notShrinking; prevGap = g; }
            last = x;
        }
    }
    const bool ok = bad == 0 && notOnEdge == 0 && notShrinking == 0;
    printf("ramp: x1 -> x4 over 8 beats -> %d pulses a cycle (integral %.3f -> expect %d), %d cycles off, %d not starting on their beat, %d intervals not shrinking  %s\n",
           Rig::count(r.rises[0], first + 8 * P, first + 16 * P), I, expect, bad, notOnEdge, notShrinking, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

// Curves and balance, against the integral: the pulse count over the first
// half of a cycle follows the curve (fast start > linear > slow start), and
// every count matches the integral of its own curve.
static int t_shape()
{
    int fails = 0;
    const long P = 4800, first = 300;
    int halves[3];
    const float lefts[3] = { 1.0f, 0.0f, -1.0f };
    for (int c = 0; c < 3; ++c) {
        Rig r;
        r.shape(0, 4.0f, 8, 1.0f, lefts[c], 0, 0);
        r.shape(1, 1.0f, 8, 1.0f, 0, 0, 0);
        r.run(first + 8 * P * 3 + 10, Steady{P, first}, Never());
        const long a = first + 8 * P;
        halves[c] = Rig::count(r.rises[0], a, a + 4 * P);
        const Bender::Shape s = mk(4.0f, 8, 1.0f, lefts[c], 0, 0);
        const int expect = (int)ceil(integral(s, 4.0) - 1e-6);   // the start + crossings below the half's integral
        const bool ok = abs(halves[c] - expect) <= 1;
        printf("shape: left curve %+.0f -> %d pulses in the first 4 beats (integral says %d)  %s\n", (double)lefts[c], halves[c], expect, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    const bool order = halves[0] > halves[1] && halves[1] > halves[2];
    printf("shape: fast start %d > linear %d > slow start %d  %s\n", halves[0], halves[1], halves[2], order ? "ok" : "FAIL");
    if (!order) ++fails;
    {   // a triangle (balance 0.5, equal curves) is symmetric
        Rig r;
        r.shape(0, 4.0f, 8, 0.5f, 0.5f, -0.5f, 0);
        r.shape(1, 1.0f, 8, 1.0f, 0, 0, 0);
        r.run(first + 8 * P * 3 + 10, Steady{P, first}, Never());
        const long a = first + 8 * P;
        const int up = Rig::count(r.rises[0], a, a + 4 * P), down = Rig::count(r.rises[0], a + 4 * P, a + 8 * P);
        const bool ok = abs(up - down) <= 1;
        printf("shape: triangle, left +0.5 / right -0.5 -> %d pulses going out, %d coming back (mirror)  %s\n", up, down, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    return fails ? 1 : 0;
}

// The pause: REST is silent for its beats; HOLD keeps the tempo the curve
// ended on (the input's after a stage 2; x M with balance 1).
static int t_pause()
{
    int fails = 0;
    const long P = 4800, first = 300;
    for (int hold = 0; hold < 2; ++hold) for (float bal : { 0.5f, 1.0f }) {
        Rig r;
        r.shape(0, 3.0f, 4, bal, 0, 0, 4);
        r.shape(1, 1.0f, 4, 1.0f, 0, 0, 0);
        r.h.mHoldA.set(hold ? 2 : 1);
        r.run(first + 8 * P * 3 + 10, Steady{P, first}, Never());
        const long a = first + 8 * P + 4 * P, b = a + 4 * P;        // cycle 1's pause
        const int n = Rig::count(r.rises[0], a + 1, b);
        int expect = 0;
        if (hold) expect = bal < 1.0f ? 3 : 11;                       // x1: beats 2-4 (the 1st IS the curve's end); x3: 12 - 1
        bool steady = true;
        if (hold) {
            long last = -1, gap0 = -1;
            for (long x : r.rises[0]) if (x > a && x < b) { if (last >= 0) { const long g = x - last; if (gap0 < 0) gap0 = g; else if (labs(g - gap0) > 1) steady = false; } last = x; }
        }
        const bool ok = abs(n - expect) <= 1 && steady;
        printf("pause: %s, balance %.1f -> %d pulses in 4 pause beats (expect %d), steady %d  %s\n",
               hold ? "hold" : "rest", (double)bal, n, expect, steady, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    return fails ? 1 : 0;
}

// One-shot: one cycle, then silence until a reset; the reset restarts it on
// the next beat.
static int t_oneshot()
{
    const long P = 4800, first = 300;
    Rig r;
    r.shape(0, 2.0f, 4, 0.5f, 0, 0, 0);
    r.shape(1, 1.0f, 4, 1.0f, 0, 0, 0);
    r.h.mRunA.set(2);
    const long resetAt = first + 20 * P + 1000;
    r.run(first + 30 * P, Steady{P, first}, [&](long t) { return t >= resetAt && t < resetAt + 4; });
    const int during = Rig::count(r.rises[0], first, first + 4 * P);
    const int after = Rig::count(r.rises[0], first + 4 * P + 10, resetAt);
    bool restarted = false;
    for (long x : r.rises[0]) if (x == first + 21 * P) restarted = true;
    const int second = Rig::count(r.rises[0], first + 21 * P, first + 25 * P);
    const int then = Rig::count(r.rises[0], first + 25 * P + 10, first + 30 * P);
    const bool ok = during > 4 && after == 0 && restarted && second > 4 && then == 0;
    printf("oneshot: %d pulses in the cycle, %d after it, reset -> restarts on the next beat %d with %d pulses, %d after  %s\n",
           during, after, restarted, second, then, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

// Reset mid-cycle: both channels restart on the next beat (a pulse there,
// beat 1 again).
static int t_reset()
{
    const long P = 4800, first = 300;
    Rig r;
    r.shape(0, 3.0f, 8, 1.0f, 0, 0, 0);
    r.shape(1, 0.5f, 8, 0.0f, 0, 0, 0);
    const long rAt = first + 13 * P + 2000;                     // beat 5 of cycle 1
    r.run(first + 30 * P, Steady{P, first}, [&](long t) { return t >= rAt && t < rAt + 4; });
    const long next = first + 14 * P;
    bool a = false, b = false;
    for (long x : r.rises[0]) if (x == next) a = true;
    for (long x : r.rises[1]) if (x == next) b = true;
    // the cycle after the reset is a whole cycle: count = the steady cycle's
    const int n1 = Rig::count(r.rises[0], next, next + 8 * P);
    const int n0 = Rig::count(r.rises[0], first + 8 * P, first + 16 * P - 6 * P);   // (cycle 1 was cut at beat 6)
    (void)n0;
    const Bender::Shape s = mk(3.0f, 8, 1.0f, 0, 0, 0);
    const int expect = (int)floor(integral(s, 8.0) + 1e-6);
    const bool ok = a && b && n1 == expect;
    printf("reset: mid-cycle -> A restarts on the next beat %d, B %d, then a full cycle of %d pulses (expect %d)  %s\n",
           a, b, n1, expect, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

// The clock stops: the outputs stop within two periods, the rate taps go to
// 0; when the clock returns a new cycle starts on its first beat.
static int t_stop()
{
    const long P = 4800, first = 300;
    Rig r;
    r.keepRate = true;
    r.shape(0, 4.0f, 8, 1.0f, 0, 0, 0);
    r.shape(1, 0.5f, 8, 1.0f, 0, 0, 0);
    const long last = first + 11 * P;                           // stops mid-cycle
    const long back = last + 5 * kSR;
    auto clk = [&](long t) { return (t >= first && t <= last + 3 && ((t - first) % P) < 4) || (t >= back && ((t - back) % P) < 4); };
    r.run(back + 10 * P, clk, Never());
    const int quiet = Rig::count(r.rises[0], last + 2 * P + 200, back) + Rig::count(r.rises[1], last + 2 * P + 200, back);
    float rateQuiet = 0; for (long i = last + 3 * P; i < back - 10; ++i) rateQuiet = fmaxf(rateQuiet, fabsf(r.rate[0][(size_t)i]));
    bool restart = false; for (long x : r.rises[0]) if (x == back) restart = true;
    const int after = Rig::count(r.rises[0], back, back + 8 * P);
    const bool ok = quiet == 0 && rateQuiet == 0.0f && restart && after > 8 && r.h.hasClock() == 1;
    printf("stop: clock stops -> %d pulses in the 5 s gap, rate tap peak %.3f there, returns -> beat 1 on its first edge %d, %d pulses in the next 8 beats  %s\n",
           quiet, (double)rateQuiet, restart, after, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

// The rate taps: log2(rate) / 10 (1 V/oct, ER-301 scale) of the curve where
// the cycle is — exactly 0 at x1 — and the PULSES agree with it: the
// interval between two pulses is the period / the rate between them.
static int t_rate()
{
    int fails = 0;
    const long P = 4800, first = 300;
    {
        Rig r; r.keepRate = true;
        r.shape(0, 1.0f, 8, 0.5f, 0, 0, 0);
        r.shape(1, 4.0f, 8, 1.0f, 0, 0, 0);
        r.run(first + 8 * P * 3, Steady{P, first}, Never());
        float m0 = 0; for (long i = first + 2 * P; i < (long)r.rate[0].size(); ++i) m0 = fmaxf(m0, fabsf(r.rate[0][(size_t)i]));
        // B: a linear x1 -> x4 ramp; at beat 4.5 of cycle 2 the rate is 1 + 3 * 4.5 / 8
        const long at = first + 16 * P + (long)(4.5 * P);
        const double want = log2(1.0 + 3.0 * 4.5 / 8.0) / 10.0;
        const double got = r.rate[1][(size_t)at];
        // the pulses around that point: interval vs period / rate
        long before = -1, after = -1;
        for (long x : r.rises[1]) { if (x <= at) before = x; else if (after < 0) after = x; }
        const double gapWant = (double)P / (1.0 + 3.0 * ((double)(before + after) / 2.0 - (first + 16 * P)) / P / 8.0);
        const bool ok = m0 == 0.0f && fabs(got - want) < 2e-3 && after > before && fabs((double)(after - before) - gapWant) < 0.02 * gapWant;
        printf("rate: x1 -> tap peak %.4f (must be 0); ramp at beat 4.5 -> %.4f (want %.4f, 1 V/oct); pulse gap %ld vs period / rate %.0f  %s\n",
               (double)m0, got, want, after - before, gapWant, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    return fails ? 1 : 0;
}

// The audio pass-through: bit-exact on both channels while both benders run.
static int t_audio()
{
    Rig r;
    r.shape(0, 3.0f, 6, 0.4f, 0.3f, -0.6f, 2);
    r.shape(1, 0.5f, 5, 0.8f, -0.2f, 0.9f, 1);
    uint32_t rng = 7u; long diff = 0, n = 0;
    for (int b = 0; b < 600; ++b) {
        for (int i = 0; i < FRAMELENGTH; ++i) {
            rng = rng * 1664525u + 1013904223u; r.h.mLeftIn.buffer()[i]  = (float)(int32_t)rng * 4.6566e-10f;
            rng = rng * 1664525u + 1013904223u; r.h.mRightIn.buffer()[i] = (float)(int32_t)rng * 4.6566e-10f * 3.0f;
        }
        float inL[FRAMELENGTH], inR[FRAMELENGTH];
        memcpy(inL, r.h.mLeftIn.buffer(), sizeof inL); memcpy(inR, r.h.mRightIn.buffer(), sizeof inR);
        r.run(FRAMELENGTH, Steady{2400, 7}, Never());
        for (int i = 0; i < FRAMELENGTH; ++i, ++n) {
            if (memcmp(&inL[i], &r.h.mLeftOut.buffer()[i], 4)) ++diff;
            if (memcmp(&inR[i], &r.h.mRightOut.buffer()[i], 4)) ++diff;
        }
    }
    const bool ok = diff == 0 && r.rises[0].size() > 20 && r.rises[1].size() > 10;
    printf("audio: %ld frames x 2 channels through Bender, %ld differ (must be 0); A %zu / B %zu pulses  %s\n",
           n, diff, r.rises[0].size(), r.rises[1].size(), ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* ── display ───────────────────────────────────────────────────────────── */
struct RecFB : od::FrameBuffer {
    int lines = 0, circles = 0, fills = 0, texts = 0, minX = 1 << 20, maxX = -(1 << 20), minY = 1 << 20, maxY = -(1 << 20);
    std::vector<std::string> words;
    void see(int x, int y) { if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y; }
    void pixel(od::Color, int x, int y) override { see(x, y); }
    void line(od::Color, int x0, int y0, int x1, int y1) override { ++lines; see(x0, y0); see(x1, y1); }
    void hline(od::Color, int x, int x2, int y, int) override { see(x, y); see(x2, y); }
    void vline(od::Color, int x, int y, int y2, int) override { see(x, y); see(x, y2); }
    int text(od::Color, int x, int y, const char *s, int, int) override { ++texts; words.push_back(s); see(x, y); return x + 6 * (int)strlen(s); }
    void circle(od::Color, int x, int y, int r) override { ++circles; see(x - r, y - r); see(x + r, y + r); }
    void fillCircle(od::Color, int x, int y, int r) override { ++fills; see(x - r, y - r); see(x + r, y + r); }
};

static int t_viz()
{
    int fails = 0;
    Rig r;
    r.shape(0, 4.0f, 8, 0.6f, 0.4f, -0.4f, 2);
    r.shape(1, 0.5f, 6, 0.3f, 0, 0, 0);
    bender::BenderCurve curve(0, 0, 85, 64);
    curve.follow(&r.h);
    {
        RecFB fb; curve.draw(fb);
        const bool ok = fb.lines > 100 && fb.minX >= 0 && fb.maxX <= 85 && fb.minY >= 0 && fb.maxY <= 64 && fb.circles == 2;
        printf("viz: idle -> %d curve segments, %d lamps, bbox x %d..%d y %d..%d  %s\n", fb.lines, fb.circles, fb.minX, fb.maxX, fb.minY, fb.maxY, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    {
        r.run(4800 * 3 + 300, Steady{4800, 10}, Never());
        RecFB fb; curve.draw(fb);
        const bool ok = fb.fills >= 1 && fb.maxY <= 64 && fb.minY >= 0;
        printf("viz: running -> %d filled marks (A's playhead / lit lamps)  %s\n", fb.fills, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    {
        bender::BenderInfo info(0, 0, 128, 64);
        info.follow(&r.h);
        RecFB fb; info.draw(fb);
        bool bpm = false; for (auto &w : fb.words) if (w.find("in 600.0 bpm") != std::string::npos) bpm = true;
        const bool ok = bpm && fb.texts == 3 && fb.minY >= 0 && fb.maxY <= 64;
        printf("viz: info -> %d lines, input 600.0 bpm shown %d  %s\n", fb.texts, bpm, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
        info.follow(nullptr);
    }
    curve.follow(nullptr);
    const bool refs = r.h.mRefs == 0;
    printf("viz: graphics released the engine -> refs %d  %s\n", r.h.mRefs, refs ? "ok" : "FAIL");
    if (!refs) ++fails;
    return fails ? 1 : 0;
}

/* ── robustness ────────────────────────────────────────────────────────── */
static bool clean(Rig &r)
{
    const float *t[4] = { r.h.mTap1.buffer(), r.h.mTap2.buffer(), r.h.mTap3.buffer(), r.h.mTap4.buffer() };
    for (int k = 0; k < 4; ++k) for (int i = 0; i < FRAMELENGTH; ++i) {
        const float x = t[k][i];
        if (k < 2 && !(x == 0.0f || x == 1.0f)) return false;
        if (k >= 2 && !(x > -1.0f && x < 1.0f)) return false;
    }
    return true;
}

static int t_nan()
{
    int fails = 0, cases = 0;
    const float poisons[] = { NAN, INFINITY, -INFINITY };
    for (int port = 0; port < 14; ++port) for (float p : poisons) {
        Rig r;
        r.shape(0, 3.0f, 6, 0.5f, 0.2f, -0.2f, 1);
        r.shape(1, 0.5f, 4, 0.5f, 0, 0, 0);
        r.run(4800 * 5, Steady{4800, 10}, Never());
        od::Inlet *ins[14] = { &r.h.mClockIn, &r.h.mResetIn, &r.h.mMultA, &r.h.mBeatsA, &r.h.mBalA, &r.h.mLeftA, &r.h.mRightA,
                               &r.h.mPauseA, &r.h.mMultB, &r.h.mBeatsB, &r.h.mBalB, &r.h.mLeftB, &r.h.mRightB, &r.h.mPauseB };
        for (int i = 0; i < FRAMELENGTH; ++i) ins[port]->buffer()[i] = p;
        r.h.process();
        bool ok = clean(r);
        r.shape(0, 3.0f, 6, 0.5f, 0.2f, -0.2f, 1);
        r.shape(1, 0.5f, 4, 0.5f, 0, 0, 0);
        for (int i = 0; i < FRAMELENGTH; ++i) r.h.mResetIn.buffer()[i] = 0.0f;
        r.clear();
        r.run(4800 * 12, Steady{4800, r.t + 10}, Never());
        ok = ok && clean(r) && r.rises[0].size() >= 12;
        ++cases;
        if (!ok) { printf("nan: port %d = %f -> outputs clean / recovered FAIL (%zu pulses after)\n", port, (double)p, r.rises[0].size()); ++fails; }
    }
    printf("nan: %d failures over %d cases\n", fails, cases);
    return fails ? 1 : 0;
}

static int t_cpu()
{
    Rig r;
    r.shape(0, 8.0f, 4, 0.5f, 0.8f, -0.8f, 0);
    r.shape(1, 0.25f, 4, 0.5f, -0.8f, 0.8f, 0);
    r.run(4800 * 4, Steady{1200, 10}, Never());
    clock_t t0 = clock();
    const int blocks = 50000;
    r.run((long)blocks * FRAMELENGTH, Steady{1200, r.t + 3}, Never());
    const double us = 1e6 * (double)(clock() - t0) / CLOCKS_PER_SEC / blocks;
    printf("cpu: %.2f us per block on this host, harness included (budget on device: 2667 us)\n", us);
    return us < 60.0 ? 0 : 1;
}

static int t_asan()
{
    int bad = 0;
    for (float M : { 0.0f, 0.25f, 1.0f, 8.0f, 99.0f, -3.0f }) for (float beats : { 0.0f, 1.0f, 64.0f, 500.0f })
    for (float bal : { -1.0f, 0.0f, 1.0f, 2.0f }) for (long P : { 2L, 97L, 4800L, 300000L }) {
        Rig r;
        r.shape(0, M, beats, bal, 1.0f, -1.0f, 32);
        r.shape(1, M, beats, 1.0f - bal, -5.0f, 5.0f, 99);
        r.h.mRunA.set(2); r.h.mHoldB.set(2);
        r.run(48000, Steady{P, 3}, [&](long t) { return (t % (5 * P + 1)) < 2; });
        if (!clean(r)) ++bad;
        r.h.mRunA.set(0); r.h.mHoldA.set(7);       // CHOICE_UNKNOWN / out of range
        r.run(4800, Steady{P, 3}, Never());
        if (!clean(r)) ++bad;
        for (int ch = -1; ch <= 2; ++ch) { (void)r.h.curveAt(ch, 0.5f); (void)r.h.getBpm(ch); (void)r.h.getBeat(ch); (void)r.h.getLamp(ch); }
    }
    printf("asan: %d dirty output blocks\n", bad);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: t <mode>\n"); return 2; }
    const char *m = argv[1];
    int r = 2;
    if      (!strcmp(m, "ident"))   r = t_ident();
    else if (!strcmp(m, "ramp"))    r = t_ramp();
    else if (!strcmp(m, "shape"))   r = t_shape();
    else if (!strcmp(m, "pause"))   r = t_pause();
    else if (!strcmp(m, "oneshot")) r = t_oneshot();
    else if (!strcmp(m, "reset"))   r = t_reset();
    else if (!strcmp(m, "stop"))    r = t_stop();
    else if (!strcmp(m, "rate"))    r = t_rate();
    else if (!strcmp(m, "audio"))   r = t_audio();
    else if (!strcmp(m, "viz"))     r = t_viz();
    else if (!strcmp(m, "nan"))     r = t_nan();
    else if (!strcmp(m, "cpu"))     r = t_cpu();
    else if (!strcmp(m, "asan"))    r = t_asan();
    else { fprintf(stderr, "unknown mode %s\n", m); return 2; }
    printf("%s: %s\n", m, r == 0 ? "PASS" : "FAIL");
    return r;
}
