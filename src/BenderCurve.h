/* BenderCurve.h — the tempo curves, plus the sub-display readout.
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nicholas Breinich (nickb808) — see LICENSE and NOTICE.
 *
 * BenderCurve (the main graphic): the tempo curve of a cycle, input tempo
 * at the bottom line, x M up (or down, below 1). A bright, B faint (one idea
 * bright, standard §8); each with a dot at the beat it is on; the pause is a
 * dotted stretch at the curve's resting tempo. A lamp per output in the
 * corner lights on its trigger.
 *
 * BenderInfo (the sub display): input bpm, then each channel's current bpm
 * and beat / cycle length.
 *
 * Scalars only from the engine (standard §12d); every virtual inline. */

#pragma once

#include <od/graphics/Graphic.h>
#include <od/graphics/constants.h>
#include "Bender.h"
#include <cstdio>

namespace bender {

class BenderCurve : public od::Graphic
{
public:
    BenderCurve(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpHead(0) {}
    virtual ~BenderCurve() { if (mpHead) mpHead->release(); }

    void follow(Bender *p)
    {
        if (mpHead) mpHead->release();
        mpHead = p;
        if (mpHead) mpHead->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
        Bender *h = mpHead;
        const int x0 = mWorldLeft + 2, x1 = mWorldLeft + mWidth - 3;
        const int y0 = mWorldBottom + 6, y1 = mWorldBottom + mHeight - 12;   // y grows upward
        fb.hline(GRAY3, x0, x1, y0);
        if (!h) return;
        // the vertical scale: the larger of the two curves' reach, in ratio
        float top = 1.0f, bot = 1.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i <= 32; ++i) {
                const float r = h->curveAt(ch, (float)i / 32.0f);
                if (r > top) top = r;
                if (r < bot) bot = r;
            }
        const float span = (top - bot) > 1.0e-3f ? (top - bot) : 1.0f;
        // input tempo line (ratio 1)
        const int yIn = y0 + (int)((1.0f - bot) / span * (float)(y1 - y0));
        fb.hline(GRAY5, x0, x1, yIn, 2);
        for (int ch = 1; ch >= 0; --ch) {                 // B first, A drawn over it
            const int color = ch == 0 ? WHITE : GRAY6;
            const int total = h->getTotal(ch);
            int px = x0, py = yIn;
            for (int x = x0; x <= x1; ++x) {
                const float u = (float)(x - x0) / (float)(x1 - x0);
                const float r = h->curveAt(ch, u);
                const int y = y0 + (int)((r - bot) / span * (float)(y1 - y0));
                if (x > x0) fb.line(color, px, py, x, y);
                px = x; py = y;
            }
            const int beat = h->getBeat(ch);
            if (beat > 0 && total > 0) {
                const float u = ((float)beat - 0.5f) / (float)total;
                const int bx = x0 + (int)(u * (float)(x1 - x0));
                const float r = h->curveAt(ch, u);
                const int by = y0 + (int)((r - bot) / span * (float)(y1 - y0));
                if (ch == 0) fb.fillCircle(WHITE, bx, by, 2);
                else         fb.circle(GRAY9, bx, by, 2);
            }
        }
        // the two output lamps, top right
        for (int ch = 0; ch < 2; ++ch) {
            const int lx = x1 - 14 + ch * 10, ly = mWorldBottom + mHeight - 5;
            if (h->getLamp(ch)) fb.fillCircle(WHITE, lx, ly, 3);
            else                fb.circle(GRAY5, lx, ly, 3);
        }
        fb.text(GRAY9, x0, mWorldBottom + mHeight - 9, "A", 10);
        fb.text(GRAY5, x0 + 8, mWorldBottom + mHeight - 9, "B", 10);
    }
#endif

private:
    Bender *mpHead;
};

class BenderInfo : public od::Graphic
{
public:
    BenderInfo(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height), mpHead(0) {}
    virtual ~BenderInfo() { if (mpHead) mpHead->release(); }

    void follow(Bender *p)
    {
        if (mpHead) mpHead->release();
        mpHead = p;
        if (mpHead) mpHead->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
        Bender *h = mpHead;
        if (!h) return;
        const int x0 = mWorldLeft + 4;
        const int top = mWorldBottom + mHeight;
        char buf[40];
        const float in = h->getInputBpm();
        if (in > 0.0f) { snprintf(buf, sizeof buf, "in %.1f bpm", (double)in); fb.text(WHITE, x0, top - 14, buf, 12); }
        else fb.text(GRAY7, x0, top - 14, "no clock", 12);
        for (int ch = 0; ch < 2; ++ch) {
            const int y = top - 30 - ch * 14;
            const char name = ch == 0 ? 'A' : 'B';
            if (h->getRunning(ch)) {
                const float b = h->getBpm(ch);
                if (b > 0.0f) snprintf(buf, sizeof buf, "%c %.1f bpm  %d/%d", name, (double)b, h->getBeat(ch), h->getTotal(ch));
                else          snprintf(buf, sizeof buf, "%c rest  %d/%d", name, h->getBeat(ch), h->getTotal(ch));
            } else {
                snprintf(buf, sizeof buf, "%c waiting", name);
            }
            fb.text(ch == 0 ? GRAY11 : GRAY8, x0, y, buf, 10);
        }
    }
#endif

private:
    Bender *mpHead;
};

} // namespace bender
