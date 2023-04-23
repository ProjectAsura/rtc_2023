#pragma once

#define RTC_DEVELOP (0) // 開発版.
#define RTC_RELEASE (1) // 提出版.

#define RTC_TARGET  RTC_DEVELOP

#if (RTC_TARGET == RTC_DEVELOP)
#define RTC_DEBUG_CODE(code)     code
#else
#define RTC_DEBUG_CODE(code)
#endif//RTC_TARGET == RTC_DEVELOP

#define RTC_UNUSED(var)     (void)var
