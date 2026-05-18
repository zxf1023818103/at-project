#include "AtCommandParser.h"

#include "Uart.h"

#if defined AT_UART_BACKEND_AMEBA
extern "C" void sys_reset(void);
#elif defined AT_UART_BACKEND_BFLB
extern "C" int GLB_SW_POR_Reset(void);
#endif

namespace at {

void AtCommandParser::send(char c) {
    m_out.put(c);
    m_out.flush();
}

void AtCommandParser::sendLine(const char* s) {
    m_out << s;
    m_out.flush();
}

void AtCommandParser::onAT() {
    sendLine("\r\nOK\r\n");
}

void AtCommandParser::onATE0() {
    echoEnabled = false;
    sendLine("\r\nOK\r\n");
}

void AtCommandParser::onATE1() {
    echoEnabled = true;
    sendLine("\r\nOK\r\n");
}

void AtCommandParser::onATGMR() {
    sendLine("\r\nat-project 0.1\r\n\r\nOK\r\n");
}

void AtCommandParser::onATRST() {
    sendLine("\r\nOK\r\n");
#if defined AT_UART_BACKEND_AMEBA
    sys_reset();
#elif defined AT_UART_BACKEND_BFLB
    GLB_SW_POR_Reset();
#endif
    while (true) { }
}

void AtCommandParser::onError() {
    sendLine("\r\nERROR\r\n");
}

}  // namespace at
