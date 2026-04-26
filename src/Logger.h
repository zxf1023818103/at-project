#pragma once

#include "Semaphore.h"
#include "SharedPtrQueue.h"

namespace at {

class Logger : protected Semaphore {
    using LogQueue = SharedPtrQueue<char[]>;

public:
    enum Level {
        DEBUG,
        INFO,
        WARN,
        ERROR,
    };

    explicit Logger(size_t queueLength = 1, Level level = WARN);

private:
    const LogQueue m_queue;

    Level m_level;

protected:
    virtual void flush();

public:
    static const char* levelString(Level level);

    void setLevel(Level level);

    Level level();

    void logFormat(Level level, const char *fmt, ...);

    virtual ~Logger() override;
};

Logger *getLogger();

template<typename Name, typename Iterator>
void dumpHex(Name name, Iterator begin, Iterator end) {
    stringstream ss;
    ss << name << "[" << distance(begin, end) << "]: " << toHexString<Iterator>(begin, end);
    getLogger()->logFormat(Logger::DEBUG, "%s", ss.str().c_str());
}

}  // namespace at
