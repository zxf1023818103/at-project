#pragma once

#include "Common.h"
#include "Logger.h"

namespace at {

template<typename Uint8Container = vector<uint8_t>>
struct BluetoothField {

    uint8_t type;

    Uint8Container data;
};

template<typename Uint8Container = vector<uint8_t>, template<typename...> typename List = initializer_list>
using BluetoothFieldList = List<BluetoothField<Uint8Container>>;

template<typename BluetoothFieldIterator>
void printBluetoothFieldList(const char *name, BluetoothFieldIterator inputBegin, size_t inputLength) {
    getLogger()->logFormat(Logger::DEBUG, "Advertisement %s has %zu field(s):", name, inputLength);
    while (inputLength-- > 0) {
        getLogger()->logFormat(Logger::DEBUG, "  Field: type=0x%02X data_len=%zu data=%s", inputBegin->type, size(inputBegin->data), toHexString(data(inputBegin->data), data(inputBegin->data) + size(inputBegin->data)).c_str());
        ++inputBegin;
    }
}

}  // namespace at
