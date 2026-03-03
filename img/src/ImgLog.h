#ifndef IMGLOG_H
#define IMGLOG_H

#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NDEBUG
void ImgDebugLog(const char* format, ...);
#define DBG(fmt, ...) ImgDebugLog("[%s] " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // IMGLOG_H
