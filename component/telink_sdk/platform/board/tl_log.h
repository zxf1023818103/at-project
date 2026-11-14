/********************************************************************************************************
 * @file    tl_log.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2025
 *
 * @par     Copyright (c) 2025, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#if defined(MCU_CORE_TL752X)
#ifndef _EPM_LOG_H
#define _EPM_LOG_H

#include <stdint.h> //for basic data types: unit8_t, unit32_t, etc.

/* ============================================================================
 * Log Level Definitions
 * ============================================================================
 */
#define LOG_LEVEL_DBG             0
#define LOG_LEVEL_INF             1
#define LOG_LEVEL_WRN             2
#define LOG_LEVEL_ERR             3
#define LOG_LEVEL_NONE            4

/* ============================================================================
 * Default Configuration
 * ============================================================================
 */
#ifndef LOG_PRINT_LEVEL
#define LOG_PRINT_LEVEL LOG_LEVEL_INF  // Default INF level debug
#endif

/* Validation Check for LOG_PRINT_LEVEL */
#if (LOG_PRINT_LEVEL != LOG_LEVEL_DBG && \
     LOG_PRINT_LEVEL != LOG_LEVEL_INF && \
     LOG_PRINT_LEVEL != LOG_LEVEL_WRN && \
     LOG_PRINT_LEVEL != LOG_LEVEL_ERR && \
     LOG_PRINT_LEVEL != LOG_LEVEL_NONE)
#error "Invalid LOG_PRINT_LEVEL defined. Please use a valid log level."
#endif

/* ============================================================================
 * Public Logging API
 * ============================================================================
 * Usage Examples:
 *   LOG_ERR("System error: %d", error_code);
 *   LOG_INF("Connection established: handle=0x%04x", handle);
 *   LOG_PRINT("Always logged: %d", value);
 */

/* Define Logging Macros */

/* Error Logging */
#if (LOG_PRINT_LEVEL <= LOG_LEVEL_ERR)
#define LOG_ERR(format, ...) ASYNC_LOG_DUMP(format, ##__VA_ARGS__)
#else
#define LOG_ERR(...)
#endif

/* Warning Logging */
#if (LOG_PRINT_LEVEL <= LOG_LEVEL_WRN)
#define LOG_WRN(format, ...) ASYNC_LOG_DUMP(format, ##__VA_ARGS__)
#else
#define LOG_WRN(...)
#endif

/* Info Logging */
#if (LOG_PRINT_LEVEL <= LOG_LEVEL_INF)
#define LOG_INF(format, ...) ASYNC_LOG_DUMP(format, ##__VA_ARGS__)
#else
#define LOG_INF(...)
#endif

/* Debug Logging */
#if (LOG_PRINT_LEVEL == LOG_LEVEL_DBG)
#define LOG_DBG(format, ...) ASYNC_LOG_DUMP(format, ##__VA_ARGS__)
#else
#define LOG_DBG(...)
#endif

/* Argument Counting Implementation */
#define FL_INTERNAL_ARG_COUNT_PRIVATE(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define VA_ARGS_SIZE(...) FL_INTERNAL_ARG_COUNT_PRIVATE(0, ##__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/* Unconditional Logging */
#define LOG_PRINT(format, ...) ASYNC_LOG_DUMP(format, ##__VA_ARGS__)

/* Internal Function Declarations */
extern void async_log_dump(const char *format, int arg_size, ...);
extern void async_log_data_dump(const uint8_t *data, uint32_t bytes_num, const char *format);

/* Low-Level Logging Implementation Macros */
#define ASYNC_LOG_DUMP(format, ...)                         \
    do {                                                    \
        async_log_dump((const char *)format, VA_ARGS_SIZE(__VA_ARGS__), ##__VA_ARGS__); \
    } while (0)

#define ASYNC_LOG_U8_DUMP(data, len, format)                \
    do {                                                    \
        async_log_data_dump(data, len, (const char *)format); \
    } while (0)

/* Hexdump Logging Macros */

/* Error Hexdump Logging */
#if (LOG_PRINT_LEVEL <= LOG_LEVEL_ERR)
#define LOG_HEXDUMP_ERR(data, len, format) ASYNC_LOG_U8_DUMP(data, len, format)
#else
#define LOG_HEXDUMP_ERR(data, len, format)
#endif

/* Warning Hexdump Logging */
#if (LOG_PRINT_LEVEL <= LOG_LEVEL_WRN)
#define LOG_HEXDUMP_WRN(data, len, format) ASYNC_LOG_U8_DUMP(data, len, format)
#else
#define LOG_HEXDUMP_WRN(data, len, format)
#endif

/* Info Hexdump Logging */
#if (LOG_PRINT_LEVEL <= LOG_LEVEL_INF)
#define LOG_HEXDUMP_INF(data, len, format) ASYNC_LOG_U8_DUMP(data, len, format)
#else
#define LOG_HEXDUMP_INF(data, len, format)
#endif

/* Debug Hexdump Logging */
#if (LOG_PRINT_LEVEL == LOG_LEVEL_DBG)
#define LOG_HEXDUMP_DBG(data, len, format) ASYNC_LOG_U8_DUMP(data, len, format)
#else
#define LOG_HEXDUMP_DBG(data, len, format)
#endif

/* Unconditional Logging */
#define LOG_HEXDUMP_PRINT(data, len, format) ASYNC_LOG_U8_DUMP(data, len, format)

#endif // _EPM_LOG_H
#endif
