#pragma once

#include <iostream>

namespace at {

class AtCommandParser {
public:
    explicit AtCommandParser();

    ~AtCommandParser();

    void process(char input);

private:
    int cs;

    bool echoEnabled;

    bool skipping = false;

protected:
    void send(char c);

    void send(char *data, std::size_t size);

private:
    void onAT();

    void onATE0();

    void onATE1();
};

}  // namespace at
