%%{
    machine at_command_parser;
    access this->;

    eol = "\r" | "\n";

    action on_heartbeat { this->onAT(); }
    action on_echo_off  { this->onATE0(); }
    action on_echo_on   { this->onATE1(); }
    action on_gmr       { this->onATGMR(); }
    action on_rst       { this->onATRST(); }

    at_core = "AT" (
        eol @on_heartbeat |
        "E0" eol @on_echo_off |
        "E1" eol @on_echo_on |
        "+GMR" eol @on_gmr |
        "+RST" eol @on_rst
    );

    main := ( at_core | eol )*;
}%%

#include "AtCommandParser.h"

// Ragel -G2 emits goto-driven switches with intentional fall-throughs;
// suppress -Wimplicit-fallthrough for the whole TU so SDKs that build
// with -Werror -Wextra (Ameba new) don't reject the generated parser.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

namespace at {

%% write data;

AtCommandParser::AtCommandParser(std::ostream& out) : m_out(out) {
    %% write init;
}

AtCommandParser::~AtCommandParser() = default;

void AtCommandParser::process(char input) {
    if (this->echoEnabled) {
        this->send(input);
    }

    if (this->skipping) {
        if (input == '\r' || input == '\n') {
            this->onError();
            this->skipping = false;
            %% write init;
        }
        return;
    }

    const char *p = &input;
    const char *pe = p + 1;
    const char *eof = NULL;
    (void) eof;

    %% write exec;

    if (this->cs == at_command_parser_error) {
        if (input == '\r' || input == '\n') {
            this->onError();
            %% write init;
        } else {
            this->skipping = true;
        }
    }
}

}  // namespace at

#pragma GCC diagnostic pop
