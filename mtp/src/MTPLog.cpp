#include "MTPLog.h"

#include <time.h>

void DebugLog(const char* format, ...) {
    static FILE* logFile = nullptr;

    if (!logFile) {
        logFile = fopen("/tmp/mtp_plugin.log", "a");
        if (!logFile) {
            return;
        }
    }

    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    if (fprintf(logFile, "[%s] ", timestamp) < 0) {
        fclose(logFile);
        logFile = nullptr;
        return;
    }

    va_list args;
    va_start(args, format);
    if (vfprintf(logFile, format, args) < 0) {
        va_end(args);
        fclose(logFile);
        logFile = nullptr;
        return;
    }
    va_end(args);

    fprintf(logFile, "\n");
    fflush(logFile);
}
