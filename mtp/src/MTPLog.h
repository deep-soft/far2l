#ifndef MTPLOG_H
#define MTPLOG_H

#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void DebugLog(const char* format, ...);
#define DBG(fmt, ...) DebugLog("[%s] " fmt, __FUNCTION__, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // MTPLOG_H
