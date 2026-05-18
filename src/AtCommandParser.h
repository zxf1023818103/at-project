#pragma once

#include <cstddef>
#include <ostream>

namespace at {

class AtCommandParser {
public:
    explicit AtCommandParser(std::ostream& out);

    ~AtCommandParser();

    void process(char input);

private:
    std::ostream& m_out;

    int cs;

    bool echoEnabled = true;

    bool skipping = false;

protected:
    void send(char c);

    void sendLine(const char* s);

private:
    void onAT();

    void onATE0();

    void onATE1();

    void onATGMR();

    void onATRST();

    void onError();
};

}  // namespace at
