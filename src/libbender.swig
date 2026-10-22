/* libbender.swig — SWIG interface for the Bender ER-301 Object. */

%module bender_libbender

%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include <Bender.h>
#include <BenderCurve.h>
#define SWIGLUA
using namespace bender;
%}

%include <Bender.h>
%include <BenderCurve.h>
