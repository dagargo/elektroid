#ifndef KORG_SYRO_COMP_H__
#define KORG_SYRO_COMP_H__

#include "korg_syro_type.h"

#define VOLCASAMPLE_COMP_BLOCK_LEN	0x800

#ifdef __cplusplus
extern "C"
{
#endif

uint32_t SyroComp_GetCompSize(const uint8_t *psrc, uint32_t num_of_sample,
	uint32_t quality, Endian sample_endian);

uint32_t SyroComp_Comp(const uint8_t *psrc, uint8_t *pdest, int num_of_sample, 
	int quality, Endian sample_endian);

#ifdef __cplusplus
}
#endif

#endif

