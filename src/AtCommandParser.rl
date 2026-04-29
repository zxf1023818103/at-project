%%{
    machine at_command_parser;
    access this->;

    eol = "\r\n" | "\n" | "\r";

    action on_heartbeat { this->onAT(); }
    action on_echo_off { this->onATE0(); }
    action on_echo_on { this->onATE1(); }

    at_core = "AT" (
        eol @on_heartbeat |
        "E0" eol @on_echo_off |
        "E1" eol @on_echo_on
    );

    main := ( at_core | eol )*;
}%%

#include "AtCommandParser.h"

namespace at {

%% write data;

AtCommandParser::AtCommandParser() {
    %% write init;
}

AtCommandParser::~AtCommandParser() = default;

void AtCommandParser::process(char input) {
    if (this->echoEnabled) {
        // putchar(input);
    }

    if (this->skipping) {
        if (input == '\r' || input == '\n') {
            // printf("ERROR\r\n");
            skipping = false;
            %% write init;
        }
        return;
    }

    const char *p = &input;
    const char *pe = p + 1;
    const char *eof = NULL;

    %% write exec;

    if (this->cs == at_command_parser_error) {
        if (input == '\r' || input == '\n') {
            // printf("ERROR\r\n");
            %% write init;
        } else {
            skipping = true;
        }
    }
}

}  // namespace at
