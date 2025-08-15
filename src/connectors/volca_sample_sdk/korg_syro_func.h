/*
 * korg_syro_func.h - KORG SYRO SDK
 * Copyright (C) 2014, KORG Inc. All rights reserved.
 *
 * This file is part of SYRO SDK.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license. See the COPYING file for details.
 */
#ifndef KORG_SYRO_FUNC_H__
#define KORG_SYRO_FUNC_H__

#include "korg_syro_type.h"

#define KORGSYRO_NUM_OF_CHANNEL		2

#define KORGSYRO_QAM_CYCLE			8
#define KORGSYRO_NUM_OF_CYCLE		2
#define KORGSYRO_NUM_OF_CYCLE_BUF	(KORGSYRO_QAM_CYCLE * KORGSYRO_NUM_OF_CYCLE)

typedef struct {
	int16_t CycleSample[KORGSYRO_NUM_OF_CYCLE_BUF];
	int LastPhase;
	int32_t Lpf_z;
} SyroChannel;

#ifdef __cplusplus
extern "C"
{
#endif

uint16_t SyroFunc_CalculateCrc16(uint8_t *pSrc, int size);
uint32_t SyroFunc_CalculateEcc(uint8_t *pSrc, int size);
void SyroFunc_SetTxSize(uint8_t *ptr, uint32_t size, int num_of_bytes);

void SyroFunc_GenerateSingleCycle(SyroChannel *psc, int write_page, uint8_t dat, bool block);
void SyroFunc_MakeGap(SyroChannel *psc, int write_page);
void SyroFunc_MakeStartMark(SyroChannel *psc, int write_page);
void SyroFunc_MakeChannelInfo(SyroChannel *psc, int write_page);

#ifdef __cplusplus
}
#endif

#endif

