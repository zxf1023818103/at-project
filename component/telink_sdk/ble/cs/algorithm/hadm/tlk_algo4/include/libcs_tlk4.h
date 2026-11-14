/********************************************************************************************************
 * @file    libcs_tlk3.h
 *
 * @brief   This is the header file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    2023
 *
 * @par     Copyright (c) 2023, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

#ifndef LIBCS_TLK4_H_
#define LIBCS_TLK4_H_

#include <stdint.h>
#include <stddef.h>

//////////////////////////////////////////// copy from ranging_data.h /////////////////////////////////////////

// To ensure bitfields are packed as expected without padding.
#pragma pack(push, 1)
#define MAX_STEPS_SUPPORTED (512)
#define INTER_RESULT_EN     (1)
#define N_AP                (4)
/**
 * @brief Structure for the Ranging Header.
 * Corresponds to Table 3.7 in RAS_v1.0.pdf.
 */
typedef struct {
    uint16_t ranging_counter : 12;
    uint16_t configuration_id : 4;
    int8_t   selected_tx_power;
    uint8_t  antenna_paths_mask;
} RangingHeader;

/**
 * @brief Structure for the Subevent Header.
 * Corresponds to the "Subevent Header" part of Table 3.8 in RAS_v1.0.pdf.
 */
typedef struct {
    uint16_t start_acl_connection_event;
    int16_t  frequency_compensation; // Units of 0.01 ppm
    uint8_t  ranging_done_status : 4;
    uint8_t  subevent_done_status : 4;
    uint8_t  ranging_abort_reason : 4;
    uint8_t  subevent_abort_reason : 4;
    int8_t   reference_power_level; // Units: dBm
    uint8_t  number_of_steps_reported;
} SubeventHeader;

// TODO: All data types and sizes below are assumptions based on common practice.
// You MUST verify these against the Bluetooth Core Specification for accuracy.

typedef struct {
    uint8_t  packet_quality;
    int8_t   packet_rssi;
    uint8_t  packet_antenna;
    int16_t  measured_freq_offset;
} StepDataMode0;

typedef struct {
    uint8_t  packet_quality;
    uint8_t packet_nadm;
    int8_t   packet_rssi;
    uint16_t tod_toa; // Time of Departure / Time of Arrival
    uint8_t  packet_antenna;
    uint32_t packet_pct1;
    uint32_t packet_pct2;
} StepDataMode1;

// Represent a 3-byte unsigned integer in little-endian format
// Little-endian means the least significant byte is stored first.
typedef struct {
    uint8_t byte0;
    uint8_t byte1;
    uint8_t byte2;
} uint24_t;

// Structure to hold one interleaved tone data pair
typedef struct {
    uint24_t tone_pct;
    uint8_t  tone_quality_indicator;
} ToneDataPair;

// --- Detailed Step Data Structures (based on Table 3.13) ---
// TODO: The number of tones 'k' is defined in the Core Spec. Using a placeholder.
#define NUM_TONES_PLACEHOLDER 5

typedef struct {
    uint8_t  antenna_permutation_index;
    ToneDataPair tone_data[NUM_TONES_PLACEHOLDER];
    // Antenna Path data (I/Q samples) is variable and complex.
    // It would need to be parsed from the remaining data in the buffer.
} StepDataMode2;

typedef struct {
    uint8_t  packet_quality;
    uint16_t packet_nadm;
    int8_t   packet_rssi;
    uint64_t tod_toa;
    uint8_t  packet_antenna;
    uint16_t packet_pct1;
    uint16_t packet_pct2;
    uint8_t  antenna_permutation_index;
    ToneDataPair tone_data[NUM_TONES_PLACEHOLDER];
    // Antenna Path data would follow here.
} StepDataMode3;


/**
 * @brief Structure for a single CS Step, with fully decoded data.
 */
typedef struct {
    uint8_t  step_mode_raw; // Raw byte containing mode and aborted flag
    uint8_t  channel;
    uint8_t  data_len;      // The length of the data field in bytes
    union {                 // Decoded data payload
        StepDataMode0* mode0;
        StepDataMode1* mode1;
        StepDataMode2* mode2;
        StepDataMode3* mode3;
        uint8_t* raw_data; // Fallback for mismatched length
    } data;
} Step;

/**
 * @brief Structure for a complete Subevent, including header and all its steps.
 */
typedef struct {
    SubeventHeader header;
    Step* steps; // Dynamically allocated array of steps
} Subevent;

/**
 * @brief Main structure for the entire Ranging Data Body.
 * Corresponds to Table 3.6 in RAS_v1.0.pdf.
 */
typedef struct {
    RangingHeader header;
    Subevent* subevents;       // Dynamically allocated array of subevents
    size_t        num_subevents;   // Number of subevents in the array
} RangingDataBody;


#pragma pack(pop)


/**
 * @brief Decodes a raw buffer into a RangingDataBody structure.
 *
 * This function parses a buffer containing the Ranging Data Body and populates
 * a dynamically allocated RangingDataBody structure with detailed, mode-specific
 * step data.
 *
 * NOTE: The caller is responsible for freeing the returned structure and all its
 * nested dynamically allocated members using the `free_ranging_data_body` function.
 *
 * @param buffer Pointer to the start of the Ranging Data Body buffer.
 * @param buffer_size The total size of the buffer in bytes.
 * @return A pointer to a newly allocated and populated RangingDataBody struct,
 * or NULL if parsing fails (e.g., buffer too small).
 */
RangingDataBody* decode_ranging_data_body(const uint8_t* buffer, size_t buffer_size);

/**
 * @brief Frees all memory associated with a RangingDataBody structure.
 *
 * This function safely deallocates the main structure and all its nested
 * dynamically allocated members (subevents, steps, and step-specific data).
 *
 * @param data Pointer to the RangingDataBody structure to be freed.
 */
void free_ranging_data_body(RangingDataBody* data);

/**
 * @brief Prints the contents of a RangingDataBody structure to the console.
 *
 * A utility function to display the decoded ranging data in a human-readable format,
 * including detailed fields for each step mode.
 *
 * @param data Pointer to the RangingDataBody structure to print.
 */
void print_ranging_data_body(const RangingDataBody *data);

// Data structures
struct iq_sample_int16_t {
    int16_t i;
    int16_t q;
};

struct iq_sample_and_channel {
  uint8_t channel;
  uint8_t antenna_path;
  uint8_t local_tone_quality;
  uint8_t peer_tone_quality;
  struct iq_sample_int16_t local_iq_sample;
  struct iq_sample_int16_t peer_iq_sample;
};

// Function prototype
struct iq_sample_and_channel* get_all_iq_samples_and_channels(
    const RangingDataBody* local_data,
    const RangingDataBody* peer_data,
    size_t* count_out
);

const char *blc_get_cs_alg4_version(void );
//////////////////////////////////////////// copy from ranging_data.h /////////////////////////////////////////



//////////////////////////////////////////// copy from distance.h /////////////////////////////////////////

#define DISTANCE_TAG_SLOT_SIZE  4608U

typedef union {
    uint8_t  _opaque[DISTANCE_TAG_SLOT_SIZE];
    uint32_t _align;
} distance_tag_slot_t;

struct iq_sample {
    int16_t i;
    int16_t q;
};

struct iq_sample_and_channel_raw {
    uint8_t channel;
    uint8_t local_tone_quality;
    uint8_t peer_tone_quality;
    struct iq_sample local_iq_sample;
    struct iq_sample peer_iq_sample;
};

struct iq_pct_and_channel {
  uint8_t channel;      /**channel */
  uint8_t tone_quality; /**tone quality */
  struct iq_sample pct; /**one side pct */
};

typedef struct {
    uint8_t  channel;
    uint16_t toa_tod_initiator;
    uint16_t toa_tod_reflactor;
} rtt_sample_raw_t;

typedef struct { // todo , move to internal header file


    float   a0;
    float   a1;
    float   a2;
    float   a3;
    float   a4;


    uint8_t b0;
    float   b1;
    float   b2;
    uint8_t b3;
    uint8_t b4;
    float   b5;
    float   b6;
    float   b7;
    float   b8;
    float   b9;
    float   b10;
    float   b11;


    uint8_t c0;
    float   c1;
    float   c2;
    float   c3;
    float   c4;
    float   c5;
    float   c6;
    float   c7;
    float   c8;
    float   c9;
    float   c10;
    float   c11;
    float   c12;
    float   c13;


    uint8_t d0;
    float   d1;
    float   d2;
    float   d3;


    float   e0;
    float   e1;
    float   e2;
    float   e3;
    uint8_t e4;
    uint8_t e5;
    uint8_t e6;
    float   e7;
    float   e8;
    uint8_t e9;
    float   e10;
    float   e11;
    float   e12;
    float   e13;


    float   f0;
    float   f1;
    float   f2;
    float   f3;
    float   f4;


    float   g0;
    float   g1;
    float   g2;


    float   h0;
    float   h1;
    float   h2;
    float   h3;
    float   h4;
    float   h5;


    float   i0;
    float   i1;
    float   i2;
    float   i3;
    float   i4;
    float   i5;
    float   i6;
    float   i7;
    float   i8;
    float   i9;
    float   i10;
    float   i11;
    float   i12;

    float   j0;
    float   j1;

    /* ---- parameter k, auto adjust open field and indoor ---- */
    float   k0;
    float   k1;
    float   k2;
    float   k3;
    float   k4;
    float   k5;
    float   k6;
    float   k7;
    float   k8;

    /* ---- Group TK: Passive anchor topological spatial constraint parameters (new) ---- */
    float Tk0; // 1 indicates it is a passive anchor
    float Tk1; // 3 indicates restriction within d_main +/-3m range.
    float Tk2; // 3 indicates restriction within d_main +/-3m range.

    /* ---- Group Tkl: History buffer and constraint upper bound (interval-sensitive, directly configures runtime length) ---- */
    float Tkl0;
    float Tkl1;
    float Tkl2;
    float Tkl3;
    float Tkl4;

    float l0;
    float l1;
    float l2;
    float l3;
    float l4;
    float l5;

    float m0;
    float m1;
    float m2;
    float m3;
    float m4;
    float m5;
    float m6;

    float n0;
    float n1;
    float n2;
    float n3;
    float n4;
    float n5;
    float n6;
    float n7;

    float o0;
    float o1;
    float o2;
    float o3;
    float o4;

    float p0;
    float p1;
    float p2;
    float p3;

      /* ---- Group Q: speed search continuity constraint (high packet rate / short interval scenarios) ---- */
    float q0;
    float q1;
} distance_dbg_params_t;

// SDK use
typedef struct __attribute__((packed))
{
    u8 mode;
    u8 channel;
    u8 len;
    StepDataMode2 mode2;
} cs_local_step_mode2_t;

// SDK use
typedef struct __attribute__((packed))
{
    u8 mode;
    StepDataMode2 mode2;
} cs_remote_step_mode2_t;

// SDK use
typedef struct __attribute__((packed))
{
    u8 mode;
    u8 channel;
    u8 len;
    StepDataMode1 mode1;
} cs_local_step_mode1_t;

// SDK use
typedef struct __attribute__((packed))
{
    u8 mode;
    StepDataMode1 mode1;
} cs_remote_step_mode1_t;

void distance_dbg_params_default(distance_dbg_params_t *params);

int distance_dbg_params_set(const distance_dbg_params_t *params);

int distance_dbg_params_get(distance_dbg_params_t *params);

void distance_dbg_params_reset(void);

/**
 * @brief Adjust parameters based on the actual refresh interval (interval-driven
 *        parameters are centralized here; PC and firmware call the same source).
 *
 * Call timing: after distance_dbg_params_default, before distance_dbg_params_set.
 * - a0 / f3 are scaled by interval (consistent with the original PC tool logic);
 * - when the interval is short (<= 75ms, high packet rate), high-packet-rate
 *   optimization parameters such as b3/q0/q1/b2/p0 are additionally enabled;
 * - at the 150ms baseline interval, only a0/f3 behave as before, all others
 *   keep their defaults.
 * @param intvl_ms measurement interval per antenna path (milliseconds)
 */
void distance_dbg_params_adjust_for_interval(distance_dbg_params_t *p, float intvl_ms);

/**
 * @brief Estimate distance based on IQ samples and channel information
 *
 * This function calculates the distance between devices by analyzing IQ samples.
 * It implements a multi-step signal processing pipeline to improve accuracy
 * and robustness under various environmental conditions.
 *
 * @param data - Pointer to the array of valid IQ samples and channels
 * @param len - Length of the `data` array (number of IQ samples)
 * @param ant_path - Antenna path number
 * @param tag_idx - tag_idx(0 to TAG_NUM - 1), default is 0(single tag module)
 * @param subevent_interval_ms - procedure interval in milliseconds
 *
 * @return Estimated distance. Returns zero if the hardware does not support the operation.
 */
float estimate_distance(struct iq_sample_and_channel_raw *data, uint8_t len, uint8_t ant_path,
                        uint8_t tag_idx, float g_d_main, uint16_t missing_count);


/**
 * @brief Inline PCT mode distance estimation (a dedicated entry equivalent to estimate_distance).
 * Mathematically, it is bit-for-bit identical to feeding the generic entry with a
 * synthesized peer = (1,0) and peer_tone_quality = local.
 * @param data inline PCT sample array; all other parameters are the same as estimate_distance.
 */
float estimate_distance_inline(struct iq_pct_and_channel *data, uint8_t len, uint8_t ant_path,
                               uint8_t tag_idx, float g_d_main, uint16_t missing_count);

/**
 * @brief Initialize the distance estimation module
 *
 * This function initializes the internal state and parameters required
 * by the distance estimation algorithm.
 * @param tag_idx - tag_idx(0 to TAG_NUM - 1), default is 0(single tag module), 0XFF to init all tags.
 * @return 0 on success, negative error code on failure.
 */
int estimate_distance_init(uint8_t tag_idx);

/**
 * @brief Injects an external static buffer to initialize the library's storage space
 *
 * Must be called before all other estimate_* calls, and only once.
 * The buffer must remain valid throughout the entire runtime (declared as static or global).
 *
 * Typical usage:
 *   #define MY_TAG_NUM  2
 *   static distance_tag_slot_t tag_buf[MY_TAG_NUM];
 *   estimate_buff_init(MY_TAG_NUM, tag_buf);
 *
 * @param max_tags  Maximum number of tags that can be tracked simultaneously (1~255)
 * @param buffer    Static array of size max_tags of type distance_tag_slot_t
 * @return  0   Success
 *         -1   buffer is NULL
 *         -2   max_tags is 0
 */
int estimate_buff_init(uint8_t max_tags, distance_tag_slot_t *buffer);
int distance_get_antenna_distances(uint8_t tag_idx, float *out_dists);

typedef struct {
  float velocity1;
  float velocity2;
  float velocity3;

  float distance1;
  float distance2;
  float distance3;
  float distance4;
  float distance5;
} distance_intermediate_results_t;

/* Debug data buffer (for main_debug debugging analysis) */
#if !defined(__riscv)
#define DIST_DEBUG_BUF_LEN 800
typedef struct {
  float data[DIST_DEBUG_BUF_LEN]; /* debug data */
} distance_debug_buf_t;
#endif

/**
 * @brief Get the intermediate results of estimate_distance()
 *
 * Automatically filled by estimate_distance() after each procedure completes.
 *
 * @param tag_idx  Tag index (must be < MAX_INTERMEDIATE_TAGS, current upper limit is 8)
 * @param out      Output intermediate results
 * @return  0  Success
 *         -1  Buffer not ready / null pointer / index out of bounds
 */
int distance_get_intermediate_results(uint8_t tag_idx, distance_intermediate_results_t *out);

/**
 * @brief Get the debug data buffer of estimate_distance()
 *
 * Automatically filled during each procedure handling.
 *
 * @param tag_idx  Tag index (must be < MAX_INTERMEDIATE_TAGS, current limit is 8)
 * @param out      Output debug data
 * @return  0      Success
 *         -1      Buffer not ready / Null pointer / Index out of bounds
 */
#if !defined(__riscv)
int distance_get_debug_buf(uint8_t tag_idx, distance_debug_buf_t *out);
#endif
//////////////////////////////////////////// copy from distance.h
////////////////////////////////////////////

// passiveAncorCollectData
int calcIpmPctIK(int pctI[], int pctR[], int pctIK[], int pctRK[], int ipmpctRK[], int steps,
                 int ipmpctIK[]);
int calcIpmPctRK(int pctI[], int pctR[], int pctIK[], int pctRK[], int ipmpctIK[], int steps,
                 int ipmpctRK[]);

int calcIpmPctIKWithDistRK(int pctI[], int pctR[], int pctIK[], int pctRK[], float distRk,
                           unsigned char chanIdx[], int steps, int ipmpctIK[]);
int calcIpmPctRKWithDistIK(int pctI[], int pctR[], int pctIK[], int pctRK[], float distIk,
                           unsigned char chanIdx[], int steps, int ipmpctRK[]);

int calcIpmPctIK_complex(int pctI[], int pctR[], int pctIK[], int pctRK[], int ipmpctRK[],
                         int steps, int ipmpctIK[]);
int calcIpmPctRK_complex(int pctI[], int pctR[], int pctIK[], int pctRK[], int ipmpctIK[],
                         int steps, int ipmpctRK[]);

int calcIpmPctIKWithDistRK_complex(int pctI[], int pctR[], int pctIK[], int pctRK[], float distRk,
                                   unsigned char chanIdx[], int steps, int ipmpctIK[]);
int calcIpmPctRKWithDistIK_complex(int pctI[], int pctR[], int pctIK[], int pctRK[], float distIk,
                                   unsigned char chanIdx[], int steps, int ipmpctRK[]);

int calcIpmPctIK_sqrt(int pctI[], int pctR[], int pctIK[], int pctRK[], int ipmpctRK[], int steps,
                      int ipmpctIK[]);
int calcIpmPctIKWithDistRK_sqrt(int pctI[], int pctR[], int pctIK[], int pctRK[], float distRk,
                                unsigned char chanIdx[], int steps, int ipmpctIK[]);

int calcIpmPctRK_sqrt(int pctI[], int pctR[], int pctIK[], int pctRK[], int ipmpctIK[], int steps,
                      int ipmpctRK[]);
int calcIpmPctRKWithDistIK_sqrt(int pctI[], int pctR[], int pctIK[], int pctRK[], float distIk,
                                unsigned char chanIdx[], int steps, int ipmpctRK[]);

int calcIpmPctIKWithDistRK_phase(int pctI[], int pctR[], int pctIK[], int pctRK[], float distRk,
                                 unsigned char chanIdx[], int steps, int ipmpctIK[]);
int calcIpmPctRKWithDistIK_phase(int pctI[], int pctR[], int pctIK[], int pctRK[], float distIk,
                                 unsigned char chanIdx[], int steps, int ipmpctRK[]);

int calcIpmPctIKWithDistRK_complex2(int pctI[], int pctR[], int pctIK[], int pctRK[], float distRk,
                                    unsigned char chanIdx[], int steps, int ipmpctIK[]);
int calcIpmPctRKWithDistIK_complex2(int pctI[], int pctR[], int pctIK[], int pctRK[], float distIk,
                                    unsigned char chanIdx[], int steps, int ipmpctRK[]);

#endif /* LIBCS_TLK4_H_ */
