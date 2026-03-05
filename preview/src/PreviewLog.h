#ifndef PREVIEWLOG_H
#define PREVIEWLOG_H

#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NDEBUG
void PreviewDebugLog(const char* format, ...);
#define DBG(fmt, ...) PreviewDebugLog("[%s] " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // PREVIEWLOG_H
