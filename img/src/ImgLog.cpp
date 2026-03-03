#include "ImgLog.h"
#include <ctime>

void ImgDebugLog(const char* format, ...) {
    static FILE* logFile = nullptr;

    if (!logFile) {
        logFile = fopen("/tmp/img_plugin.log", "a");
        if (!logFile) {
            return;
        }
    }

    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(logFile, "[%s] ", time_buf);

    va_list args;
    va_start(args, format);
    vfprintf(logFile, format, args);
    va_end(args);

    fprintf(logFile, "\n");
    fflush(logFile);
}
