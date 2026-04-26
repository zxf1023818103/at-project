#include "LowerBluetoothTask.h"

namespace at {

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

bool LowerBluetoothTask::stackRunning() const {
    ::T_GAP_DEV_STATE state;
    ::le_get_gap_param(GAP_PARAM_DEV_STATE, &state);
    return state.gap_init_state == GAP_INIT_STATE_STACK_READY;
}

void LowerBluetoothTask::waitForStackRunning() const {
    while (!stackRunning()) {
        ::vTaskDelay(pdMS_TO_TICKS(100));
    }
}

::T_APP_RESULT LowerBluetoothTask::gapCallback(uint8_t type, void *args) {
    ::T_LE_CB_DATA *data = static_cast<::T_LE_CB_DATA*>(args);
    switch (type) {
#if defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT
        case GAP_MSG_LE_DATA_LEN_CHANGE_INFO: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_DATA_LEN_CHANGE_INFO");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=%d", data->p_le_data_len_change_info->conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  max_tx_octets=0x%x", data->p_le_data_len_change_info->max_tx_octets);
            getLogger()->logFormat(Logger::DEBUG, "  max_tx_time=0x%x", data->p_le_data_len_change_info->max_tx_time);
            break;
        }
#endif // defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT
        case GAP_MSG_LE_MODIFY_WHITE_LIST: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_MODIFY_WHITE_LIST");
            getLogger()->logFormat(Logger::DEBUG, "  operation=%d", data->p_le_modify_white_list_rsp->operation);
            getLogger()->logFormat(Logger::DEBUG, "  cause=0x%x", data->p_le_modify_white_list_rsp->cause);
            break;
        }
        case GAP_MSG_LE_SCAN_INFO: {
            shared_ptr<BluetoothAdvReport> advReport(reinterpret_cast<BluetoothAdvReport*>(::malloc(sizeof(BluetoothAdvReport) + data->p_le_scan_info->data_len - 1)), [](BluetoothAdvReport *p) { ::free(p); });
            advReport->addrType = static_cast<uint8_t>(data->p_le_scan_info->remote_addr_type);
            ::memcpy(advReport->bdAddr, data->p_le_scan_info->bd_addr, 6);
            advReport->rssi = data->p_le_scan_info->rssi;
            advReport->advType = static_cast<uint8_t>(data->p_le_scan_info->adv_type);
            advReport->dataLen = data->p_le_scan_info->data_len;
            ::memcpy(advReport->data, data->p_le_scan_info->data, data->p_le_scan_info->data_len);
            getAdvReportQueue()->send(advReport);
            break;
        }
    }
    return ::APP_RESULT_SUCCESS;
}

LowerBluetoothTask::LowerBluetoothTask(string deviceName)
    : Task(), m_eventQueue(API_TASK_GAP_MSG_QUEUE_SIZE), m_ioQueue(API_TASK_IO_MSG_QUEUE_SIZE) {
    my_assert(!stackRunning());
    ::bt_trace_init();

    /* gap_config_xxx() shall be before bte_init() */
    ::gap_config_max_le_link_num(RTK_BLE_GAP_MAX_LINKS);
    ::gap_config_max_le_paired_device(RTK_BLE_GAP_MAX_LINKS);
    ::gap_config_max_mtu_size(180);
    ::gap_config_deinit_flow(0);
    ::gap_config_ccc_bits_count(16, 0);
#if F_BT_DEINIT
    ::bte_init();
#endif
    my_assert(::le_gap_init(RTK_BLE_GAP_MAX_LINKS));

    /* le_set_gap_param() shall be after bte_init() */
    uint8_t masterInitMtuRequest = 1;
    ::le_set_gap_param(GAP_PARAM_MASTER_INIT_GATT_MTU_REQ, sizeof masterInitMtuRequest, &masterInitMtuRequest);

    uint8_t slaveInitMtuRequest = 0;
    ::le_set_gap_param(GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof slaveInitMtuRequest, &slaveInitMtuRequest);
#if defined(F_BT_LE_5_0_SET_PHYS_SUPPORT) && F_BT_LE_5_0_SET_PHYS_SUPPORT
    uint8_t preferAllPhy = 0;
    ::le_set_gap_param(GAP_PARAM_DEFAULT_PHYS_PREFER, sizeof preferAllPhy, &preferAllPhy);

    uint8_t preferTxPhy = 1 | 1 << 1;
    ::le_set_gap_param(GAP_PARAM_DEFAULT_TX_PHYS_PREFER, sizeof preferTxPhy, &preferTxPhy);

    uint8_t preferRxPhy = 1 | 1 << 1;
    ::le_set_gap_param(GAP_PARAM_DEFAULT_RX_PHYS_PREFER, sizeof preferRxPhy, &preferRxPhy);
#endif

#if defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT
    uint16_t maxTxOctets = 0x40;
    ::le_set_gap_param(GAP_PARAM_DEFAULT_DATA_LEN_MAX_TX_OCTETS, sizeof maxTxOctets, &maxTxOctets);

    uint16_t maxTxTime = 0x200;
    ::le_set_gap_param(GAP_PARAM_DEFAULT_DATA_LEN_MAX_TX_TIME, sizeof maxTxTime, &maxTxTime);
#endif // defined(F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT) && F_BT_LE_4_2_DATA_LEN_EXT_SUPPORT

    ::le_register_app_cb(gapCallback);

    static bool coexInited = false;
    if (!coexInited) {
        coexInited = true;
        ::bt_coex_init();
    }
    ::gap_config_cccd_not_check(::CONFIG_GATT_CCCD_NOT_CHECK);

    uint16_t default_appearance = GAP_GATT_APPEARANCE_UNKNOWN;
    ::gaps_set_parameter(GAPS_PARAM_APPEARANCE, sizeof(uint16_t), &default_appearance);

    uint8_t device_name_prop = GAPS_PROPERTY_WRITE_ENABLE;
    ::gaps_set_parameter(GAPS_PARAM_DEVICE_NAME_PROPERTY, sizeof(device_name_prop), &device_name_prop);

    uint8_t appearance_prop = GAPS_PROPERTY_WRITE_ENABLE;
    ::gaps_set_parameter(GAPS_PARAM_APPEARANCE_PROPERTY, sizeof(appearance_prop), &appearance_prop);

    setDeviceNameInternal(deviceName);

    ::server_builtin_service_reg(true);
    ::server_init(RTK_BT_GATTS_SERVICE_NUM - 2);
}

LowerBluetoothTask::~LowerBluetoothTask() {
    if (stackRunning()) {
#if F_BT_DEINIT
        ::bte_deinit();
#endif
#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
        ::bt_trace_uninit();
#else
        ::bt_trace_deinit();
#endif
    }
}

void LowerBluetoothTask::onDisconnected(uint8_t connId, uint16_t reason) {
    getLogger()->logFormat(Logger::DEBUG, "onDisconnected connId=0x%X reason=0x%X", connId, reason);
}

void LowerBluetoothTask::onConnected(uint8_t connId) {
    getLogger()->logFormat(Logger::DEBUG, "onConnected connId=0x%X", connId);
    ::le_adv_start();
}

bool LowerBluetoothTask::handleGapMessage(::T_IO_MSG *ioMsg) {
    const char *gapAdvStateStrTbl[] = {
        "GAP_ADV_STATE_IDLE",
        "GAP_ADV_STATE_START",
        "GAP_ADV_STATE_ADVERTISING",
        "GAP_ADV_STATE_STOP",
    };
    const char *gapAdvSubStateStrTbl[] = {
        "GAP_ADV_TO_IDLE_CAUSE_STOP",
        "GAP_ADV_TO_IDLE_CAUSE_CONN",
    };
    const char *gapConnStateStrTbl[] = {
        "GAP_CONN_STATE_DISCONNECTED",
        "GAP_CONN_STATE_CONNECTING",
        "GAP_CONN_STATE_CONNECTED",
        "GAP_CONN_STATE_DISCONNECTING",
    };
    const char *gapInitStateStrTbl[] = {
        "GAP_INIT_STATE_INIT",
        "GAP_INIT_STATE_STACK_READY",
    };
    const char *gapScanStateStrTbl[] = {
        "GAP_SCAN_STATE_IDLE",
        "GAP_SCAN_STATE_START",
        "GAP_SCAN_STATE_SCANNING",
        "GAP_SCAN_STATE_STOP",
    };
    const char *gapAuthenStateStrTbl[] = {
        "GAP_AUTHEN_STATE_STARTED",
        "GAP_AUTHEN_STATE_COMPLETE",
    };
    const char *gap_conn_param_update_status_str_tbl[] = {
        "GAP_CONN_PARAM_UPDATE_STATUS_SUCCESS",
        "GAP_CONN_PARAM_UPDATE_STATUS_FAIL",
        "GAP_CONN_PARAM_UPDATE_STATUS_PENDING",
    };
    ::T_LE_GAP_MSG gapMsg;
    ::memcpy(&gapMsg, &ioMsg->u.param, sizeof ioMsg->u.param);

    switch (ioMsg->subtype) {
        case GAP_MSG_LE_DEV_STATE_CHANGE: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_DEV_STATE_CHANGE");
            getLogger()->logFormat(Logger::DEBUG, "  gap_adv_state=%s", gapAdvStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_adv_state]);
            getLogger()->logFormat(Logger::DEBUG, "  gap_adv_sub_state=%s", gapAdvSubStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_adv_sub_state]);
            getLogger()->logFormat(Logger::DEBUG, "  gap_conn_state=%s", gapConnStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_conn_state]);
            getLogger()->logFormat(Logger::DEBUG, "  gap_init_state=%s", gapInitStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_init_state]);
            getLogger()->logFormat(Logger::DEBUG, "  gap_scan_state=%s", gapScanStateStrTbl[gapMsg.msg_data.gap_dev_state_change.new_state.gap_scan_state]);
            getLogger()->logFormat(Logger::DEBUG, "  cause=0x%X", gapMsg.msg_data.gap_dev_state_change.cause);
            break;
        }
        case GAP_MSG_LE_CONN_STATE_CHANGE: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_CONN_STATE_CHANGE");
            getLogger()->logFormat(Logger::DEBUG, "  new_state=%s", gapConnStateStrTbl[gapMsg.msg_data.gap_conn_state_change.new_state]);
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%X", gapMsg.msg_data.gap_conn_state_change.conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  disc_cause=0x%X", gapMsg.msg_data.gap_conn_state_change.disc_cause);
            switch (gapMsg.msg_data.gap_conn_state_change.new_state) {
                case GAP_CONN_STATE_DISCONNECTED: {
                    onDisconnected(gapMsg.msg_data.gap_conn_state_change.conn_id,
                        gapMsg.msg_data.gap_conn_state_change.disc_cause);
                    break;
                }
                case GAP_CONN_STATE_CONNECTED: {
                    onConnected(gapMsg.msg_data.gap_conn_state_change.conn_id);
                    break;
                }
                case GAP_CONN_STATE_DISCONNECTING: {
                    break;
                }
                case GAP_CONN_STATE_CONNECTING: {
                    break;
                }
                default: {
                    break;
                }
            }
            break;
        }
        case GAP_MSG_LE_CONN_MTU_INFO: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_CONN_MTU_INFO");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", gapMsg.msg_data.gap_conn_mtu_info.conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  mtu_size=%d", gapMsg.msg_data.gap_conn_mtu_info.mtu_size);
            break;
        }
        case GAP_MSG_LE_CONN_PARAM_UPDATE: {
            uint8_t conn_id = gapMsg.msg_data.gap_conn_param_update.conn_id;
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_CONN_PARAM_UPDATE");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  status=%s", gap_conn_param_update_status_str_tbl[gapMsg.msg_data.gap_conn_param_update.status]);
            switch (gapMsg.msg_data.gap_conn_param_update.status) {
                case GAP_CONN_PARAM_UPDATE_STATUS_SUCCESS: {
                    uint16_t conn_interval, conn_slave_latency, conn_supervision_timeout;
                    ::le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
                    ::le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_slave_latency, conn_id);
                    ::le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_supervision_timeout, conn_id);
                    break;
                }
                case GAP_CONN_PARAM_UPDATE_STATUS_FAIL: {
                    break;
                }
                case GAP_CONN_PARAM_UPDATE_STATUS_PENDING: {
                }
                break;
                default: {
                    break;
                }
            }
            break;
        }
        case GAP_MSG_LE_AUTHEN_STATE_CHANGE: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_AUTHEN_STATE_CHANGE");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", gapMsg.msg_data.gap_authen_state.conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  new_state=%s", gapAuthenStateStrTbl[gapMsg.msg_data.gap_authen_state.new_state]);
            getLogger()->logFormat(Logger::DEBUG, "  status=0x%x", gapMsg.msg_data.gap_authen_state.status);
            break;
        }
        case GAP_MSG_LE_BOND_JUST_WORK: {
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_JUST_WORK");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", gapMsg.msg_data.gap_bond_just_work_conf.conn_id);
            uint8_t conn_id = gapMsg.msg_data.gap_bond_just_work_conf.conn_id;
            ::le_bond_just_work_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
            break;
        }
        case GAP_MSG_LE_BOND_PASSKEY_DISPLAY: {
            uint32_t display_value = 0;
            uint8_t conn_id = gapMsg.msg_data.gap_bond_passkey_display.conn_id;
            ::le_bond_get_display_key(conn_id, &display_value);
            ::le_bond_passkey_display_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_PASSKEY_DISPLAY");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%0x", conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  display_value=%lu", display_value);
            break;
        }
        case GAP_MSG_LE_BOND_USER_CONFIRMATION: {
            uint32_t display_value = 0;
            uint8_t conn_id = gapMsg.msg_data.gap_bond_user_conf.conn_id;
            ::le_bond_get_display_key(conn_id, &display_value);
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_USER_CONFIRMATION");
            getLogger()->logFormat(Logger::DEBUG, "conn_id=0x%0X", conn_id);
            getLogger()->logFormat(Logger::DEBUG, "passkey=%lu", display_value);
            break;
        }
        case GAP_MSG_LE_BOND_PASSKEY_INPUT: {
            uint8_t conn_id = gapMsg.msg_data.gap_bond_passkey_input.conn_id;
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_PASSKEY_INPUT");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%x", conn_id);
            break;
        }
#if F_BT_LE_SMP_OOB_SUPPORT
        case GAP_MSG_LE_BOND_OOB_INPUT: {
            uint8_t oob_data[GAP_OOB_LEN] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            uint8_t conn_id = gapMsg.msg_data.gap_bond_oob_input.conn_id;
            ::le_bond_set_param(GAP_PARAM_BOND_OOB_DATA, GAP_OOB_LEN, oob_data);
            ::le_bond_oob_input_confirm(conn_id, GAP_CFM_CAUSE_ACCEPT);
            getLogger()->logFormat(Logger::DEBUG, "GAP_MSG_LE_BOND_OOB_INPUT");
            getLogger()->logFormat(Logger::DEBUG, "  conn_id=0x%X", conn_id);
            getLogger()->logFormat(Logger::DEBUG, "  oob_data=%s", toHexString(oob_data, oob_data + GAP_OOB_LEN).c_str());
            break;
        }
#endif
        default: {
            getLogger()->logFormat(Logger::DEBUG, "%s() unknown subtype %d", __func__, ioMsg->subtype);
            break;
        }
    }
    return true;
}

void LowerBluetoothTask::routine() {
    my_assert(::gap_start_bt_stack(m_eventQueue.handle(), m_ioQueue.handle(), m_eventQueue.length() + m_ioQueue.length()));
    for (;;) {
        uint8_t event = 0;
        m_eventQueue.receive(&event);
        if (event == ::EVENT_IO_TO_APP) {
            ::T_IO_MSG ioMsg;
            if (m_ioQueue.receive(&ioMsg, 0)) {
                if (ioMsg.type == ::IO_MSG_TYPE_BT_STATUS) {
                    if (!handleGapMessage(&ioMsg)) {
                        break;
                    }
                }
                else if (ioMsg.type == ::IO_MSG_TYPE_QDECODE) {
                    auto callbackPtrPtr = static_cast<shared_ptr<AbstractCallbackTask>*>(ioMsg.u.buf);
                    bool success = false;
                    if (callbackPtrPtr != nullptr) {
                        (*callbackPtrPtr)->startSync();
                        success = (*callbackPtrPtr)->success();
                    }
                    delete callbackPtrPtr;
                    if (!success) {
                        break;
                    }
                }
            }
        } else {
            ::gap_handle_msg(event);
        }
    }
}

void LowerBluetoothTask::notify() {
    uint8_t event = ::EVENT_IO_TO_APP;
    m_eventQueue.send(&event);
}

shared_ptr<MacAddress> LowerBluetoothTask::macAddressInternal() {
    auto macPtr = make_shared<MacAddress>();
    uint8_t mac[6];
    my_assert(::gap_get_param(GAP_PARAM_BD_ADDR, mac) == 0);
    reverse_copy(mac, mac + 6, macPtr->address.begin());
    return macPtr;
}

void LowerBluetoothTask::setDeviceNameInternal(string deviceName) {
    if (deviceName.length() > GAP_DEVICE_NAME_LEN) {
        deviceName.resize(GAP_DEVICE_NAME_LEN);
    }
    ::gaps_set_parameter(GAPS_PARAM_DEVICE_NAME, deviceName.length(), const_cast<char*>(deviceName.c_str()));
}

bool LowerBluetoothTask::stopAdvertisingInternal() {
    return ::le_adv_stop() == ::GAP_CAUSE_SUCCESS;
}

void LowerBluetoothTask::setAdvertisingParametersInternal(const struct bt_le_adv_param *param, bool hasScanResponseData) {
    uint8_t adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC;
    ::le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);

    uint8_t adv_evt_type = GAP_ADTYPE_ADV_NONCONN_IND;
    if (param->options & BT_LE_ADV_OPT_CONNECTABLE) {
        adv_evt_type = GAP_ADTYPE_ADV_IND;
    }
    else {
        if (hasScanResponseData) {
            adv_evt_type = GAP_ADTYPE_ADV_SCAN_IND;
        } else {
            adv_evt_type = GAP_ADTYPE_ADV_NONCONN_IND;
        }
    }
    ::le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);

    uint8_t adv_direct_addr[GAP_BD_ADDR_LEN] = {0};
    ::le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);

    uint8_t adv_chann_map = GAP_ADVCHAN_ALL;
    ::le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);

    uint8_t adv_filter_policy = GAP_ADV_FILTER_ANY;
    if (param->options & BT_LE_ADV_OPT_FILTER_SCAN_REQ) {
        adv_filter_policy = GAP_ADV_FILTER_WHITE_LIST_SCAN;
    }
    else if (param->options & GAP_ADV_FILTER_WHITE_LIST_CONN) {
        adv_filter_policy = GAP_ADV_FILTER_WHITE_LIST_CONN;
    }
    ::le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);

    ::le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(param->interval_min), const_cast<uint16_t*>(&param->interval_min));
    ::le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(param->interval_max), const_cast<uint16_t*>(&param->interval_max));
}

void LowerBluetoothTask::waitForStart() const {
    Task::waitForStart();
    waitForStackRunning();
}

void LowerBluetoothTask::sendCallback(shared_ptr<AbstractCallbackTask> callback) {
    m_ioQueue.send(callback);
    notify();
}

#endif // realtek platforms

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 || defined BL616

LowerBluetoothTask::LowerBluetoothTask(string deviceName) : Task(), m_callbackQueue(API_TASK_IO_MSG_QUEUE_SIZE) {
#if defined BL602 || defined BL702
    ::ble_controller_init(configMAX_PRIORITIES - 1);
#endif // defined BL602 || defined BL702
#if defined BL606P || defined BL808 || defined BL616
    ::btble_controller_init(configMAX_PRIORITIES - 1);
#endif // defined BL606P || defined BL808 || defined BL616
    ::hci_driver_init();
    ::bt_enable(nullptr);

    setDeviceNameInternal(deviceName);
}

LowerBluetoothTask::~LowerBluetoothTask() {
    ::bt_disable();
}

void LowerBluetoothTask::sendCallback(shared_ptr<AbstractCallbackTask> callback) {
    m_callbackQueue.send(callback);
}

shared_ptr<MacAddress> LowerBluetoothTask::macAddressInternal() {
    auto macPtr = make_shared<MacAddress>();
    ::bt_addr_le_t local_addr;
    my_assert(::bt_get_local_public_address(&local_addr) == 0);
    reverse_copy(local_addr.a.val, local_addr.a.val + 6, begin(macPtr->address));
    return macPtr;
}

void LowerBluetoothTask::setDeviceNameInternal(string deviceName) {
    ::bt_set_name(deviceName.c_str());
}

bool LowerBluetoothTask::stopAdvertisingInternal() {
    return ::bt_le_adv_stop() == 0;
}

void LowerBluetoothTask::routine() {
    for (;;) {
        auto callbackTaskPtr = m_callbackQueue.receive();
        my_assert(callbackTaskPtr != nullptr);
        callbackTaskPtr->startSync();
        if (!callbackTaskPtr->success()) {
            break;
        }
    }
}

#endif // bouffalo platforms

}  // namespace at
