#include "SharedPtrQueueTestItem.h"

namespace at {

SharedPtrQueueTestItem::SharedPtrQueueTestItem(int v) : RefCountObject(), value(v) {
    printf("SharedPtrQueueTestItem with value %d created\n", value);
}

SharedPtrQueueTestItem::~SharedPtrQueueTestItem() {
    printf("SharedPtrQueueTestItem with value %d destroyed\n", value);
}

}  // namespace at
