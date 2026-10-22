/* liblangevin.swig — SWIG interface for the Langevin ER-301 Object. */

%module langevin_liblangevin

%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include <Langevin.h>
#include <LangevinCurve.h>
#define SWIGLUA
using namespace langevin;
%}

%include <Langevin.h>
%include <LangevinCurve.h>
