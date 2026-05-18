#include "AtTask.h"

#include "AtCommandParser.h"
#include "AtUartConfig.h"
#include "UartInStream.h"
#include "UartOutStream.h"

namespace at {

AtTask::AtTask() : Task() {}

AtTask::~AtTask() = default;

void AtTask::routine() {
    auto *dev = openAtUart();
    UartOutStream out(dev);
    UartInStream  in(dev);
    AtCommandParser parser(out);

    out << "\r\nat-project AT ready\r\n";
    out.flush();

    char c;
    while (in.get(c)) {
        parser.process(c);
    }
}

}  // namespace at
