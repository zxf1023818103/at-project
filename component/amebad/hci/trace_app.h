/*
 * trace_app.h — minimal stand-in for the vendored Realtek BT trace subsystem.
 *
 * The original trace stack (trace.h — a 125 KB macro header — plus trace_task.c
 * and trace_uart.c, a FreeRTOS task pumping a dedicated trace UART) was removed.
 * The controller bring-up (hci_adapter/process/board, cycle_queue) only ever used
 * the HCI_PRINT_* logging macros; they are no-ops here. Re-route to DiagPrintf if
 * BT controller bring-up logging is ever needed.
 */
#ifndef _TRACE_APP_H_
#define _TRACE_APP_H_

#define HCI_PRINT_ERROR0(...)   ((void)0)
#define HCI_PRINT_ERROR1(...)   ((void)0)
#define HCI_PRINT_ERROR2(...)   ((void)0)
#define HCI_PRINT_ERROR3(...)   ((void)0)
#define HCI_PRINT_WARN0(...)    ((void)0)
#define HCI_PRINT_WARN1(...)    ((void)0)
#define HCI_PRINT_WARN2(...)    ((void)0)
#define HCI_PRINT_WARN3(...)    ((void)0)
#define HCI_PRINT_INFO0(...)    ((void)0)
#define HCI_PRINT_INFO1(...)    ((void)0)
#define HCI_PRINT_INFO2(...)    ((void)0)
#define HCI_PRINT_INFO3(...)    ((void)0)
#define HCI_PRINT_TRACE0(...)   ((void)0)
#define HCI_PRINT_TRACE1(...)   ((void)0)
#define HCI_PRINT_TRACE2(...)   ((void)0)
#define HCI_PRINT_TRACE3(...)   ((void)0)

#endif /* _TRACE_APP_H_ */
