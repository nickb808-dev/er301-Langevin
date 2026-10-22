// Host-test stub of od/objects/Object.h (Port / Option / Object), with the
// SDK's member names. Object is ReferenceCounted in the SDK (od/extras/
// ReferenceCounted.h: attach / release), which the display graphics use to
// hold the engine — modelled as a counter the tests can read.
#pragma once
#include <od/config.h>
namespace od {
struct Port {
    const char *mName;
    float mBuf[FRAMELENGTH];
    explicit Port(const char *n) : mName(n) { for (int i = 0; i < FRAMELENGTH; ++i) mBuf[i] = 0.0f; }
    float *buffer() { return mBuf; }
};
using Inlet  = Port;
using Outlet = Port;
// Menu option (preset-serialised on device; 1-based, per the OptionControl
// convention).
struct Option {
    const char *mName;
    int mValue;
    Option(const char *n, int v = 0) : mName(n), mValue(v) {}
    int  value() { return mValue; }  // non-const, matching the device SDK
    void set(int v) { mValue = v; }
};
struct Object {
    virtual ~Object() {}
    virtual void process() {}
    void addInput(Port &) {}
    void addOutput(Port &) {}
    void addOption(Option &) {}
    void attach()  { ++mRefs; }
    void release() { --mRefs; }
    int mRefs = 0;
};
} // namespace od
