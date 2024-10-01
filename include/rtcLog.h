//-----------------------------------------------------------------------------
// File : rtcLog.h
// Desc : Logger.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

#include <cstdio>

#if defined(DEBUG) || defined(_DEBUG)
#define RTC_DLOG(x, ...)    fprintf_s(stdout, "[File:%s, Line:%d] " x "\n", __FILE__, __LINE__, ##__VA_ARGS__ )
#else
#define RTC_DLOG(x, ...)
#endif

#define RTC_ELOG(x, ...)    fprintf_s(stderr, "[File:%s, Line:%d] " x "\n", __FILE__, __LINE__, ##__VA_ARGS__ )
