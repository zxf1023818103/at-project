#include "Logger.h"

namespace at {

Logger::Logger(Level level) : m_mutex(), m_level(level) {
    m_mutex.give();
}

const char* Logger::levelString(Level level) {
    const char *levelStrings[4] = { "DEBUG", "INFO", "WARN", "ERROR" };
    return levelStrings[level];
}

void Logger::setLevel(Level level) {
    m_level = level;
}

Logger::Level Logger::level() const {
    return m_level;
}

void Logger::logFormat(Level level, const char *fmt, ...) {
    if (level < m_level) {
        return;
    }

    ::va_list ap1, ap2;
    ::va_start(ap1, fmt);
    ::va_copy(ap2, ap1);
    const int expectedSize = ::vsnprintf(nullptr, 0, fmt, ap1);
    ::va_end(ap1);
    my_assert(expectedSize > 0);
    const size_t bufferSize = static_cast<size_t>(expectedSize) + 1;
    unique_ptr<char[]> buffer(new char[bufferSize]);
    ::vsnprintf(buffer.get(), bufferSize, fmt, ap2);
    ::va_end(ap2);

    m_mutex.take();
#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
    puts(buffer.get());
#else
    printf("%s\r\n", buffer.get());
#endif
    m_mutex.give();
}

Logger *getLogger() {
    static Logger logger(Logger::DEBUG);
    return &logger;
}

}  // namespace at
