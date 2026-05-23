#pragma once

#include "Common.h"
#include "Semaphore.h"

namespace at {

class Logger {
public:
    enum Level {
        DEBUG,
        INFO,
        WARN,
        ERROR,
    };

    explicit Logger(Level level = WARN);

    static const char* levelString(Level level);

    void setLevel(Level level);

    Level level() const;

    void logFormat(Level level, const char *fmt, ...);

private:
    Semaphore m_mutex;

    Level m_level;
};

Logger *getLogger();

template<typename Name, typename Iterator>
void dumpHex(Name name, Iterator begin, Iterator end) {
    stringstream ss;
    ss << name << "[" << distance(begin, end) << "]: " << toHexString<Iterator>(begin, end);
    getLogger()->logFormat(Logger::DEBUG, "%s", ss.str().c_str());
}

}  // namespace at
