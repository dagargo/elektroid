/************************************************************************
	SYRO for volca sample
		comprss PCM
 ***********************************************************************/
#include <stdlib.h>
#include <string.h>
#include "korg_syro_type.h"
#include "korg_syro_volcasample.h"
#include "korg_syro_func.h"
#include "korg_syro_comp.h"

typedef struct {
	const uint8_t *ptr;
	uint32_t NumOfSample;
	int bitlen_eff;
	Endian SampleEndian;
	uint16_t sum;
	uint16_t padding;
} ReadSample;

typedef struct {
	uint8_t *ptr;
	int BitCount;
	int ByteCount;
} WriteBit;


/*-----------------------------------------------------------------------------
	Write Bit
	** Update pwp member(ptr, BitCount, ByteCount)
 -----------------------------------------------------------------------------*/
static void SyroComp_WriteBit(WriteBit *pwp, uint32_t dat, int bit)
{
	//-- write bit as big-endian format.(MSB->LSB) --
	
	dat <<= (32-bit);
	dat >>= pwp->BitCount;

	for (;;) {
		if (pwp->BitCount) {
			*pwp->ptr |= (uint8_t)(dat>>24);
		} else {
			*pwp->ptr = (uint8_t)(dat>>24);
		}
		if ((pwp->BitCount + bit) >= 8) {
			bit -= (8 - pwp->BitCount);
			pwp->BitCount=0;
			pwp->ByteCount++;
			pwp->ptr++;
			dat <<= 8;
		} else {
			pwp->BitCount += bit;
			bit = 0;
		}
		if (!bit) {
			break;
		}
	}
}

/*-----------------------------------------------------------------------------
	Read PCM (fixed to 16bit)
 -----------------------------------------------------------------------------*/
static int32_t SyroComp_GetPcm(ReadSample *prp)
{
	int32_t dat;
	
	if (prp->SampleEndian == LittleEndian) {
		dat = (int32_t)((int8_t)(prp->ptr[1]));
		dat <<= 8;
		dat |= (int32_t)(*prp->ptr);
		prp->ptr += 2;
	} else {
		dat = (int32_t)((int8_t)(*prp->ptr++));
		dat <<= 8;
		dat |= (int32_t)(*prp->ptr++);
	}
	
	/*----- convert, 16Bit -> specified bit ----*/
	if (prp->bitlen_eff < 16) {
		dat /= (1 << (16 - prp->bitlen_eff));	//replace from  dat >>= (16 - prp->bitlen_eff);
		prp->sum += (uint16_t)(dat << (16 - prp->bitlen_eff));
	} else {
		prp->sum += (uint16_t)dat;
	}
	
	return dat;
}

/*-----------------------------------------------------------------------------
	Generate bit-map, store to map_buffer
 -----------------------------------------------------------------------------*/
static void SyroComp_MakeMapBuffer(uint8_t *map_buffer, ReadSample *prp, int *pBitBase, int nBitBase, int type)
{
	int i;
	uint32_t mcnt;
	int32_t dat[4];
	int32_t datn;
	int bnum;
	
	memset(map_buffer, 0, VOLCASAMPLE_COMP_BLOCK_LEN);
	
	bnum = 0;
	mcnt = 0;
	map_buffer[mcnt++] = (uint8_t)prp->bitlen_eff;		/* fix to full-bit 1st~3rd */
	map_buffer[mcnt++] = (uint8_t)prp->bitlen_eff;
	map_buffer[mcnt++] = (uint8_t)prp->bitlen_eff;
	if (mcnt >= prp->NumOfSample) {
		return;
	}
	
	dat[3] = SyroComp_GetPcm(prp);
	dat[2] = SyroComp_GetPcm(prp);
	dat[1] = SyroComp_GetPcm(prp);
	for (;;) {
		dat[0] = SyroComp_GetPcm(prp);
		datn = dat[0];
		if (type) {
			datn -= (dat[1]*2 - dat[2]);
		}
		if (datn < 0) {
			datn = -datn;
		}
		
		for (i=0; i<nBitBase; i++) {
			bnum = pBitBase[i];
			if (datn < ( 1 << (bnum-1))) break;
		}
		if (i == nBitBase) {
			bnum = prp->bitlen_eff;
		}
		
		map_buffer[mcnt++] = (uint8_t)bnum;
		if (mcnt == prp->NumOfSample) {
			break;
		}
		dat[3] = dat[2];
		dat[2] = dat[1];
		dat[1] = dat[0];
	}
}

/*-----------------------------------------------------------------------------
	convert bit-map in map_buffer.
	for example, bit=4,4,1,4 -> bit 4,4,4,4
 -----------------------------------------------------------------------------*/
static void SyroComp_MakeMap_BitConv(uint8_t *map_buffer, int num_of_sample, int bitlen)
{
	int i, j;
	int dat, dat1;
	int datlo, dathi, datuse;
	int st;
	int pls, min;
	
	for (i=0; i<bitlen; i++) {
		
		st = -1;
		for (j=0; j<(num_of_sample+1); j++) {
			dat = (j<num_of_sample) ? map_buffer[j] : 0;
			if (dat==i) {
				if (st==-1) {
					st=j;
				}
			} else {
				if (st!=-1) {
					dat1 = st ? map_buffer[st-1] : 0;
					if (dat<dat1) {
						datlo = dat;
						dathi = dat1;
					} else {
						datlo = dat1;
						dathi = dat;
					}
					if (dathi > i) {
						datuse = dathi;
						if (datlo > i) {
							datuse = datlo;
						}
						
						pls = (datuse-i) * (j-st);
						min = 2 + i;
						if (datuse==bitlen) {
							min++;
						}
						if (dathi==datlo) {
							min += 2 + datlo;
							if (datlo==bitlen) {
								min++;
							}
						}
						if (min>=pls) {
							for (; st<j; st++) {
								map_buffer[st] = (uint8_t)datuse;
							}
						}
					}
					st = -1;
				}
			}
		}
	}
}
/*-----------------------------------------------------------------------------
	Get compressed size form map_buffer
 -----------------------------------------------------------------------------*/
static int SyroComp_GetCompSizeFromMap(uint8_t *map_buffer, ReadSample *prp, int type)
{
	int i, pr, bit, prbit;
	int32_t dat, datlim;
	int bitlen;
	int32_t dath[4];

	bitlen = prp->bitlen_eff;
	datlim = -(1<<(bitlen-1));

	dath[0] = 0;
	dath[1] = 0;
	dath[2] = 0;
	dath[3] = 0;

	prbit = map_buffer[0];
	pr = 16 + 2;		// 16=BitLen(4)*4, 2=1st Header
	
	for (i=0; i<(int)prp->NumOfSample; i++) {
		dath[0] = SyroComp_GetPcm(prp);
		bit = map_buffer[i];
		if (bit != prbit) {
			pr += prbit;
			if (prbit==bitlen) {
				pr++;
			}
			pr += 2;
			prbit = bit;
		}
		pr += bit;
		if ((prbit < bitlen) && type) {
			dat = dath[0] - (dath[1]*2 - dath[2]);
		} else {
			dat = dath[0];
		}
		if (bit==bitlen && dat==datlim) {
			pr++;
		}
		dath[3] = dath[2];
		dath[2] = dath[1];
		dath[1] = dath[0];
	}
	pr += prbit;
	if (prbit==bitlen) {
		pr++;
	}
	
	return pr;
}

/*-----------------------------------------------------------------------------
	Make map (single type)
	 (prp is updated)

	memo : comppcm.c-MakeMap2
 -----------------------------------------------------------------------------*/
static int SyroComp_MakeMap_SingleType(uint8_t *map_buffer, ReadSample *prp, int *pBitBase, int type)
{
	ReadSample rp2;
	uint32_t len;
	uint32_t li;
	int i, j;
	int BitBase[16];
	int bitlen;
	
	bitlen = prp->bitlen_eff;
	
	/*------- make map of all bit --------*/
	
	for (i=0; i<(bitlen-1); i++) {
		BitBase[i] = i+1;
	}
	rp2 = *prp;
	SyroComp_MakeMapBuffer(map_buffer, &rp2, BitBase, (bitlen-1), type);
	SyroComp_MakeMap_BitConv(map_buffer, (int)rp2.NumOfSample, bitlen);
	
	/*------- Check maked map and guess bit -------*/
	{
		int BitBaseScore[16];
		int maxbit, maxsc, sc;
		
		for (i=0; i<16; i++) {
			BitBaseScore[i] = 0;
		}
		for (li=0; li<prp->NumOfSample; li++) {
			sc = map_buffer[li];
			if (sc < 16) {
				BitBaseScore[sc]++;
			}
		}

		/*-- select top 4 depth -----*/
		
		for (i=0; i<4; i++) {
			maxsc = -1;
			maxbit = -1;
			for (j=0; j<bitlen; j++) {
				if (BitBaseScore[j] > maxsc) {
					maxsc = BitBaseScore[j];
					maxbit = j;
				}
			}
			BitBase[i] = maxbit;
			BitBaseScore[maxbit] = -1;
		}
		
		/*-- sort selected bit (low->high) ----*/
		
		for (i=0; i<3; i++) {
			for (j=0; j<3; j++) {
				if (BitBase[j] > BitBase[j+1]) {
					sc = BitBase[j];
					BitBase[j] = BitBase[j+1];
					BitBase[j+1] = sc;
				}
			}
		}
	}

	/*-----------------------------------*/
	
	rp2 = *prp;
	SyroComp_MakeMapBuffer(map_buffer, &rp2, BitBase, 4, type);
	SyroComp_MakeMap_BitConv(map_buffer, (int)prp->NumOfSample, bitlen);

	rp2 = *prp;
	len = (uint32_t)SyroComp_GetCompSizeFromMap(map_buffer, &rp2, type);
	for (i=0; i<4; i++) {
		pBitBase[i] = BitBase[i];
	}
	return (int)len;
}


/*-----------------------------------------------------------------------------
	make map, get size
	 -- keep prp->ptr
 -----------------------------------------------------------------------------*/
static int SyroComp_MakeMap(uint8_t *map_buffer, ReadSample *prp, int *pBitBase, int *ptype)
{
	int i;
	int besttype;
	int len, bestlen;
	int BitBase[4];
	
	bestlen = 0;
	besttype = 0;
	
	for (i=0; i<2; i++) {
		len = SyroComp_MakeMap_SingleType(map_buffer, prp, BitBase, (i*2));	// type=0 or 2
		
		if ((!bestlen) || (len < bestlen)) {
			bestlen = len;
			besttype = i;
		}
	}
	
	if (pBitBase && ptype) {
		bestlen = SyroComp_MakeMap_SingleType(map_buffer, prp, pBitBase, (besttype*2));
		*ptype = (besttype ? 2 : 0);
	}

	return bestlen;
}

/*-----------------------------------------------------------------------------
	Compress 1 block 
	 ** Update prp
 -----------------------------------------------------------------------------*/
static int SyroComp_CompBlock(uint8_t *map_buffer, uint8_t *dest, ReadSample *prp, int *pBitBase, int type)
{
	int i, j, bit, prbit;
	int bitlen;
	int32_t dat, datlim;
	int32_t dath[4];
	uint8_t hd;
	WriteBit wp;
	int BitHead[16];
	
	wp.ptr = dest;
	wp.BitCount = 0;
	wp.ByteCount = 0;
	
	dath[0] = 0;
	dath[1] = 0;
	dath[2] = 0;
	dath[3] = 0;

	/*----- wrtie bit-base ------*/
	j = 0;
	for (i=0; i<16; i++) {
		if (pBitBase[j]==i) {
			BitHead[i] = j++;
			SyroComp_WriteBit(&wp, (uint32_t)(i-1), 4);
		} else {
			BitHead[i] = -1;
		}
	}
	
	bitlen = prp->bitlen_eff;
	datlim = -(1<<(bitlen-1));
	
	prbit = bitlen;
	SyroComp_WriteBit(&wp, 3, 2);
	
	for (i=0; i<(int)prp->NumOfSample; i++) {
		dath[0] = SyroComp_GetPcm(prp);
		bit = map_buffer[i];
		if (bit != prbit) {
			/*--- write end mark ---*/
			SyroComp_WriteBit(&wp, (1<<(prbit-1)), prbit);
			if (prbit==bitlen) {
				SyroComp_WriteBit(&wp, 1, 1);		/* add 1 bit when full-bit */
			}
			/*--- write this header ----*/
			if (bit < bitlen) {
				hd = (uint8_t)BitHead[bit];
				if (bit > prbit) {
					hd--;
				}
			} else {
				hd = 3;
			}
			SyroComp_WriteBit(&wp, hd, 2);
			prbit = bit;
		}
		if ((prbit < bitlen) && type) {
			dat = dath[0] - (dath[1]*2 - dath[2]);
		} else {
			dat = dath[0];
		}
		SyroComp_WriteBit(&wp, (uint32_t)dat, prbit);
		if ((prbit == bitlen) && (dat == datlim)) {
			SyroComp_WriteBit(&wp, 0, 1);
		}
		dath[3] = dath[2];
		dath[2] = dath[1];
		dath[1] = dath[0];
	}

	SyroComp_WriteBit(&wp, (1<<(prbit-1)), prbit);	/* EndMark */
	if (prbit == bitlen) {
		SyroComp_WriteBit(&wp, 1, 1);				/* add 1 bit when full-bit */
	}
	
	if (wp.BitCount) {
		SyroComp_WriteBit(&wp, 0, (8 - wp.BitCount));
	}
	
	return wp.ByteCount;
}


/*======================================================================
	Syro Get Sample
 ======================================================================*/
uint32_t SyroComp_GetCompSize(const uint8_t *psrc, uint32_t num_of_sample,
	uint32_t quality, Endian sample_endian)
{
	ReadSample rp;
	uint32_t num_of_thissample;
	uint32_t allsize_byte;
	uint32_t thissize_bit;
	uint8_t *map_buffer;
	
	map_buffer = malloc(VOLCASAMPLE_COMP_BLOCK_LEN);
	if (!map_buffer) {
		return 0;
	}
	
	rp.ptr = psrc;
	rp.bitlen_eff = (int)quality;
	rp.SampleEndian = sample_endian;
	
	allsize_byte = 0;
	
	for (;;) {
		num_of_thissample = VOLCASAMPLE_COMP_BLOCK_LEN;
		if (num_of_thissample > num_of_sample) {
			num_of_thissample = num_of_sample;
		}
		rp.NumOfSample = num_of_thissample;
		thissize_bit = (uint32_t)SyroComp_MakeMap(map_buffer, &rp, NULL, NULL);
		
		if ((!thissize_bit) || (thissize_bit >= (quality * num_of_thissample))) {
			//----- use liner ----
			thissize_bit = (quality * num_of_thissample);
		}
		allsize_byte += ((thissize_bit + 7) / 8);
		
		allsize_byte += 6;		//--- for Header & CRC -----
		
		rp.ptr += (num_of_thissample * 2);
		num_of_sample -= num_of_thissample;
		
		if (!num_of_sample) {
			break;
		}
	}
	
	free(map_buffer);
	
	return allsize_byte;
}


/*=============================================================================
	Compress Block
	  psrc = pointer to source sample.
	  pdest = pointer to store compressed data.
	  num_of_sample = number of sample.
	  quality = number of effective bit(8~16).
	  sample_endian = specific endian of source sample(LittleEndian or BigEndian).
 =============================================================================*/
uint32_t SyroComp_Comp(const uint8_t *psrc, uint8_t *pdest, int num_of_sample, 
	int quality, Endian sample_endian) 
{
	ReadSample rp;
	int BitBase[4];
	int i;
	int srccount, count;
	int num_of_thissample;
	int prlen;
	int type;
	int32_t dat;
	uint8_t *map_buffer;

	map_buffer = malloc(VOLCASAMPLE_COMP_BLOCK_LEN);
	if (!map_buffer) {
		return 0;
	}	

	rp.bitlen_eff = quality;
	rp.SampleEndian = sample_endian;
	rp.ptr = psrc;

	count = 0;
	srccount = 0;
	
	for (;;) {
		/*------- decide block length ------*/
		num_of_thissample = VOLCASAMPLE_COMP_BLOCK_LEN;
		if (num_of_thissample > num_of_sample) {
			num_of_thissample = num_of_sample;
		}
		rp.NumOfSample = (uint32_t)num_of_thissample;
		rp.sum = 0;
		
		prlen = SyroComp_MakeMap(map_buffer, &rp, BitBase, &type);
		
		if (prlen && (prlen < (num_of_thissample*quality))) {
			/*----- compressible ------*/
			*pdest++ = (uint8_t)(num_of_thissample>>8) | (uint8_t)(type<<5);
			*pdest++ = (uint8_t)num_of_thissample;
			prlen = SyroComp_CompBlock(map_buffer, pdest+4, &rp, BitBase, type);
			*pdest++ = (uint8_t)(prlen>>8);
			*pdest++ = (uint8_t)prlen;			
			*pdest++ = (uint8_t)(rp.sum >> 8);
			*pdest++ = (uint8_t)rp.sum;
			count += (prlen+6);
			pdest += prlen;
		} else {
			/*----- copy without compression ------*/
			*pdest++ = (uint8_t)(0xe0 | (num_of_thissample>>8));
			*pdest++ = (uint8_t)num_of_thissample;
			*pdest++ = (uint8_t)(num_of_thissample>>7);
			*pdest++ = (uint8_t)(num_of_thissample<<1);
			{
				WriteBit wb;
				wb.ptr = (pdest+2);
				wb.BitCount = 0;
				wb.ByteCount = 0;
				
				for (i=0; i<num_of_thissample; i++) {
					dat = SyroComp_GetPcm(&rp);
					SyroComp_WriteBit(&wb, (uint32_t)dat, quality);
				}
				if (wb.BitCount) {
					SyroComp_WriteBit(&wb, 0, (8-wb.BitCount));
				}
				*pdest++ = (uint8_t)(rp.sum >> 8);
				*pdest++ = (uint8_t)rp.sum;

				prlen = wb.ByteCount;
				pdest += prlen;
				count += (prlen+6);
			}
		}
		num_of_sample -= num_of_thissample;
		srccount += num_of_thissample;
		if (!num_of_sample) {
			break;
		}
	}

	free(map_buffer);
	
	return (uint32_t)count;
}





