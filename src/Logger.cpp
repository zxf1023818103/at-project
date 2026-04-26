#include "Logger.h"

namespace at {

Logger::Logger(size_t queueLength, Level level)
    : Semaphore(), m_queue(queueLength), m_level(level) {
    give();
}

void Logger::flush() {
    take();
    for (;;) {
        auto buffer = m_queue.receive(0);
        if (buffer != nullptr) {
            char* p = static_cast<char*>(buffer.get());
#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
            puts(p);
#else
            printf("%s\r\n", p);
#endif
        }
        else {
            break;
        }
    }
    give();
}

const char* Logger::levelString(Level level) {
    const char *levelStrings[4] = { "DEBUG", "INFO", "WARN", "ERROR" };
    return levelStrings[level];
}

void Logger::setLevel(Level level) {
    m_level = level;
}

Logger::Level Logger::level() {
    return m_level;
}

void Logger::logFormat(Level level, const char *fmt, ...) {
    if (level >= m_level) {
        ::va_list ap1, ap2;
        ::va_start(ap1, fmt);
        ::va_copy(ap2, ap1);
        const int expectedSize = ::vsnprintf(nullptr, 0, fmt, ap1);
        ::va_end(ap1);
        my_assert(expectedSize > 0);
        size_t bufferSize = static_cast<size_t>(expectedSize) + 1;
        shared_ptr<char[]> buffer(new char[bufferSize], [](char *p) { delete[] p; });
        const int returnedSize = ::vsnprintf(buffer.get(), bufferSize, fmt, ap2);

        (void) expectedSize;
        (void) returnedSize;
        my_assert(expectedSize == returnedSize);

        m_queue.send(buffer);
        flush();
        va_end(ap2);
    }
}

Logger::~Logger() {
    flush();
}

static Logger *s_logger = nullptr;

Logger *getLogger() {
    if (s_logger == nullptr) {
        s_logger = new Logger(1, Logger::DEBUG);
    }
    return s_logger;
}

}  // namespace at
