// Crit Launcher Native — Windows header setup, included before anything that
// touches <windows.h>. MSVC's SDK headers key off NOMINMAX and windowsx.h the
// moment they are parsed, so these must be defined before the first include of
// <windows.h> in EVERY translation unit. Doing it in one place is what makes
// that reliable: repeating the guards locally only works per-file.
//
// Include this first in any .cpp that pulls in <windows.h>.
#pragma once

#ifdef _WIN32

// windows.h otherwise defines min/max as macros, which turns every
// std::min(...)/std::max(...) into "std::((a) < (b) ? ...)" and fails with
// "'(' : illegal token on right side of '::'".
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Also opt out of the legacy min/max macros pulled in by <stdlib.h>.
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

// windowsx.h (GET_X_LPARAM / GET_Y_LPARAM and friends) is safe to pull in here
// so every TU shares one view of the SDK.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>

// rpcndr.h does "#define small char". The UI uses "small" as a parameter name
// (UIContext::Button) and the window-icon code uses it as a local, so the macro
// has to go as soon as the RPC headers are done.
#ifdef small
#undef small
#endif

// Some SDK revisions map these to macros taking a window handle, which collides
// with the Platform interface's argument-less virtuals.
#ifdef IsMaximized
#undef IsMaximized
#endif
#ifdef IsMinimized
#undef IsMinimized
#endif

#endif  // _WIN32
