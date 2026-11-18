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

#ifndef LIBCS_TLK3_H_
#define LIBCS_TLK3_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#if 0//__riscv
#include <nds_intrinsic.h>
#include "riscv_dsp_statistics_math.h"
#include "riscv_dsp_complex_math.h"
#include "riscv_dsp_utils_math.h"
#endif



#define CS_VERSION_INT(major, minor, micro) (((major) << 16) | ((minor) << 8) | (micro))
#define CS_VERSION CS_VERSION_INT(0, 5, 99)

typedef unsigned char      u8;
typedef unsigned int      u32;

#define CS_PI 3.1415926535897932354626

#define CS_MAX_CH_NUM 80
#define CS_MAX_DIST 150
#define CS_DPK_LEN 751

typedef struct
{
    float I;
    float Q;
}compx;

#define CS_ANT_NUM 4

typedef struct
{
    float cs_pb;
    float cs_thd;
    float cs_thd_2;

    float cs_sn;
    float cs_sn_s;

    float dist_fine_range;
    float cs_distest;
    int cs_pre_idx;

    compx cs_comp[CS_MAX_CH_NUM * CS_ANT_NUM];
    compx cs_comp_ud[CS_MAX_CH_NUM * CS_ANT_NUM];

    float cs_dist_max;
    float v_buf[5];

    float th;
    float th_co;

    u8 cs_init_num;
    u8 cs_init_cnt;

    u8 cs_dg;
    u8 v_st;

    unsigned int ts;
} tlka_cs_su_t;

typedef struct
{
    compx cs_ho[CS_MAX_CH_NUM * CS_ANT_NUM];
    compx cs_h[CS_MAX_CH_NUM * CS_ANT_NUM];
    compx cs_phi_norm[CS_MAX_CH_NUM * CS_ANT_NUM];

    u8 cs_nd[CS_MAX_CH_NUM];
    u8 cs_chvalid[CS_MAX_CH_NUM];
    u8 cs_seg_ch[CS_MAX_CH_NUM];

    compx dpk_est_norm[CS_DPK_LEN * CS_ANT_NUM];
    float dpk_est[CS_DPK_LEN];
} tlka_cs_com_t;


typedef struct
{
    tlka_cs_su_t cs_su_st_0;
//    tlka_cs_su_t cs_su_st_1;

    tlka_cs_com_t cs_com_st;
}tlka_cs_t;

int tlka_cs_get_version(void);
//void tlka_cs_init(tlka_cs_t* cs_st);
//float tlka_cs_proc(tlka_cs_t* cs_st, u8* pct_pdu_init, u8* pct_pdu_refl, u8 ch_num, u8* ch_valid, u8 ant_num);
void tlka_cs_su_init(tlka_cs_su_t* cs_su_st);
void tlka_cs_com_init(tlka_cs_com_t* cs_com_st);
float tlka_cs_proc(tlka_cs_com_t* cs_com_st, tlka_cs_su_t* cs_su_st, u8* pct_pdu_init, u8* pct_pdu_refl, u8 ch_num, u8* ch_valid, u8 ant_num, unsigned int timestamp);

#endif /* LIBCS_TLK3_H_ */
