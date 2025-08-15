/************************************************************************
	SYRO for volca sample
 ***********************************************************************/
#include <stdlib.h>
#include <string.h>
#include "korg_syro_type.h"
#include "korg_syro_volcasample.h"
#include "korg_syro_func.h"
#include "korg_syro_comp.h"

#define NUM_OF_DATA_MAX		(VOLCASAMPLE_NUM_OF_PATTERN + VOLCASAMPLE_NUM_OF_SAMPLE)
#define VOLCA_SAMPLE_FS		31250
#define SYRO_MANAGE_HEADER	0x47524F4B
#define	ALL_INFO_SIZE		0x4000

#define BLOCK_SIZE			256
#define BLOCK_PER_SECTOR	256
#define BLOCK_PER_SUBSECTOR	16
#define SUBSECTOR_SIZE		(BLOCK_SIZE * BLOCK_PER_SUBSECTOR)

#define	LPF_FEEDBACK_LEVEL	0x2000

#define NUM_OF_GAP_HEADER_CYCLE	10000
#define NUM_OF_GAP_CYCLE		35
#define NUM_OF_GAP_F_CYCLE		1000
#define NUM_OF_GAP_FOOTER_CYCLE	3000
#define NUM_OF_GAP_3S_CYCLE		15000

#define	NUM_OF_FRAME__GAP_HEADER	(NUM_OF_GAP_HEADER_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_OF_FRAME__GAP			(NUM_OF_GAP_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_OF_FRAME__GAP_F			(NUM_OF_GAP_F_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_OF_FRAME__GAP_3S		(NUM_OF_GAP_3S_CYCLE * KORGSYRO_QAM_CYCLE)
#define	NUM_OF_FRAME__GAP_FOOTER	(NUM_OF_GAP_FOOTER_CYCLE * KORGSYRO_QAM_CYCLE)
#define NUM_OF_FRAME__HEADER		(49 * KORGSYRO_QAM_CYCLE)
#define NUM_OF_FRAME__BLOCK			(352 * KORGSYRO_QAM_CYCLE)

#define TXHEADER_STR_LEN				16
#define	TXHEADER_STR					"KORG SYSTEM FILE"
#define TXHEADER_DEVICE_ID				0xff0033b8	// volca sample
#define TXHEADER_BLOCK_ALL				0x01
#define TXHEADER_BLOCK_ALL_COMPRESS		0x03
#define TXHEADER_BLOCK_SAMPLE_LINER		0x10
#define TXHEADER_BLOCK_PATTERN			0x20
#define TXHEADER_BLOCK_SAMPLE_COMPRESS	0x30

typedef enum {
	TaskStatus_Gap = 0,
	TaskStatus_StartMark,
	TaskStatus_ChannelInfo,
	TaskStatus_Data,
	TaskStatus_Gap_Footer,
	TaskStatus_End = -1
} SyroTaskStatus;

typedef struct {
	uint8_t Header[TXHEADER_STR_LEN];
	uint32_t DeviceID;
	uint8_t BlockCode;
	uint8_t Num;
	
	uint8_t Misc[2];
	uint8_t Size[4];

	uint16_t m_Reserved;
	uint16_t m_Speed;
} SyroTxHeader;

typedef struct {
	uint32_t Header;
	uint32_t Flags;
	SyroTaskStatus TaskStatus;
	int TaskCount;
	
	//---- Manage source data(all) -----
	int NumOfData;
	int CurData;
	
	//---- Manage source data(this) -----
	const uint8_t *pSrcData;
	int DataCount;
	int DataSize;
	uint32_t EraseAlign;
	uint32_t EraseLength;
	uint32_t EraseCount;
	bool IsCompData;
	int CompBlockPos;
	uint32_t BlockLen1st;
	
	Endian SampleEndian;
	
	//---- Manage output data -----
	uint8_t TxBlock[BLOCK_SIZE];
	int TxBlockSize;
	int TxBlockPos;
	
	uint32_t PoolData;
	int PoolDataBit;
	
	bool UseEcc;
	uint32_t EccData;
	bool UseCrc;
	uint32_t CrcData;
	
	SyroChannel Channel[KORGSYRO_NUM_OF_CHANNEL];
	int CyclePos;
	int FrameCountInCycle;

	int LongGapCount;		// Debug Put
} SyroManage;

typedef struct {
	SyroData Data;
	uint8_t *comp_buf;
	uint32_t comp_size;
} SyroManageSingle;

/*-----------------------------------------------------------------------
	Setup Next Data
 -----------------------------------------------------------------------*/
static void SyroVolcaSample_SetupNextData(SyroManage *psm)
{
	SyroManageSingle *psms;
	SyroTxHeader *psth;
	uint8_t block = 0;
	
	psms = (SyroManageSingle *)(psm+1);
	psms += psm->CurData;
	
	//----- Setup Tx Header ----
	psth = (SyroTxHeader *)psm->TxBlock;
	
	memset((uint8_t *)psth, 0, sizeof(SyroTxHeader));
	memcpy(psth->Header, TXHEADER_STR, TXHEADER_STR_LEN);
	psth->DeviceID = TXHEADER_DEVICE_ID;
	
	psth->Num = (uint8_t)psms->Data.Number;
	
	psm->SampleEndian = LittleEndian;
	psm->TxBlockSize = sizeof(SyroTxHeader);

	psm->pSrcData = psms->Data.pData;
	psm->DataSize = psms->Data.Size;
	psm->DataCount = 0;
	psm->IsCompData = false;
	psm->CompBlockPos = 0;
	psm->EraseAlign = 0;
	psm->EraseLength = 0;
	
	switch (psms->Data.DataType) {
		case DataType_Sample_All:
		case DataType_Sample_AllCompress:
			if (psms->Data.DataType == DataType_Sample_All) {
				block = TXHEADER_BLOCK_ALL;
				psth->Misc[0] = 0xff;
			} else {
				block = TXHEADER_BLOCK_ALL_COMPRESS;
				psm->pSrcData = psms->comp_buf;
				psm->DataSize = psms->comp_size;
				psm->IsCompData = true;
				psth->Misc[0] = (uint8_t)psms->Data.Quality;
				psm->BlockLen1st = ALL_INFO_SIZE;
			}
			if ((psm->CurData+1) < psm->NumOfData) {
				block++;	//----- Set continue
			}
			
			SyroFunc_SetTxSize(psth->Size, psms->Data.Size, 4);
			psth->Misc[1] = 0xff;
			psth->Num = 0xff;
			psm->EraseAlign = (BLOCK_PER_SECTOR * BLOCK_SIZE);
			psm->EraseLength = NUM_OF_GAP_3S_CYCLE;
			psm->EraseCount = (psms->Data.Size + psm->EraseAlign - 1) / psm->EraseAlign;
			
			break;

		case DataType_Sample_Liner:
		case DataType_Sample_Compress:
			if (psms->Data.DataType == DataType_Sample_Liner) {
				block = TXHEADER_BLOCK_SAMPLE_LINER;
			} else {
				block = TXHEADER_BLOCK_SAMPLE_COMPRESS;
				psm->pSrcData = psms->comp_buf;
				psm->DataSize = psms->comp_size;
				psm->IsCompData = true;
				psth->Misc[0] = (uint8_t)psms->Data.Quality;
				psm->BlockLen1st = 0;
			}
			
			if ((psm->CurData+1) < psm->NumOfData) {
				block |= 1;		//----- Set continue bit
			}
			SyroFunc_SetTxSize(psth->Size, psms->Data.Size, 4);

			psth->m_Reserved = 0xffff;
			psth->m_Speed = (uint16_t)(psms->Data.Fs * 0x4000 / VOLCA_SAMPLE_FS);

			psm->SampleEndian = psms->Data.SampleEndian;

			psm->EraseAlign = (SUBSECTOR_SIZE - 2);
			psm->EraseLength = NUM_OF_GAP_F_CYCLE;
			psm->EraseCount = (psms->Data.Size + psm->EraseAlign - 1) / psm->EraseAlign;
			
			break;

		case DataType_Sample_Erase:
			block = TXHEADER_BLOCK_SAMPLE_LINER;
			if ((psm->CurData+1) < psm->NumOfData) {
				block |= 1;		//----- Set continue bit
			}
			psth->m_Reserved = 0xffff;
			psth->m_Speed = 0x4000;
			
			psm->pSrcData = NULL;
			psm->DataSize = 0;
			break;
			
		case DataType_Pattern:
			block = TXHEADER_BLOCK_PATTERN;
			if ((psm->CurData+1) < psm->NumOfData) {
				block |= 1;		//----- Set continue bit
			}
			SyroFunc_SetTxSize(psth->Size, psm->DataSize, 4);

			break;
		
		default:
			break;
	}
	psth->BlockCode = block;
	
	psm->TaskStatus = TaskStatus_Gap;
	psm->TaskCount = NUM_OF_GAP_HEADER_CYCLE;
}


/*-----------------------------------------------------------------------
	Setup by TxBlock
 -----------------------------------------------------------------------*/
static void SyroVolcaSample_SetupBlock(SyroManage *psm)
{
	bool use_ecc;
	
	use_ecc = (psm->TxBlockSize == BLOCK_SIZE) ? true : false;
	
	psm->TxBlockPos = 0;
	psm->TaskCount = psm->TxBlockSize;
	psm->UseEcc = use_ecc;
	psm->UseCrc = true;
	
	psm->CrcData = SyroFunc_CalculateCrc16(psm->TxBlock, psm->TxBlockSize);
	if (use_ecc) {
		psm->EccData = SyroFunc_CalculateEcc(psm->TxBlock, psm->TxBlockSize);
	}
	
	psm->PoolData = 0xa9;		// Block Start Code
	psm->PoolDataBit = 8;
}

/************************************************************************
	Internal Functions (Output Syro Data)
 ***********************************************************************/
/*-----------------------------------------------------------------------
	Generate Data
	 ret : true if block is end.
 -----------------------------------------------------------------------*/
static bool SyroVolcaSample_MakeData(SyroManage *psm, int write_page)
{
	int ch, bit;
	uint32_t dat;
	bool data_end;
	
	data_end = false;
	
	//------ Supply Data/Ecc/Crc ------
	if (psm->PoolDataBit < (3 * KORGSYRO_NUM_OF_CHANNEL)) {
		if (psm->TaskCount) {
			dat = psm->TxBlock[psm->TxBlockPos++];
			bit = 8;
			psm->TaskCount--;
		} else 	if (psm->UseEcc) {
			dat = psm->EccData;
			bit = 24;
			psm->UseEcc = false;
		} else if (psm->UseCrc) {
			dat = psm->CrcData;
			bit = 16;
			psm->UseCrc = false;
		} else {
			dat = 0;
			bit = (3 * KORGSYRO_NUM_OF_CHANNEL) - psm->PoolDataBit;
			data_end = true;
		}
		psm->PoolData |= (dat << psm->PoolDataBit);
		psm->PoolDataBit += bit;
	}

	//------ Make Cycle ------
	for (ch=0; ch<KORGSYRO_NUM_OF_CHANNEL; ch++) {
		SyroFunc_GenerateSingleCycle(&psm->Channel[ch], write_page, (psm->PoolData & 7), true);
		psm->PoolData >>= 3;
		psm->PoolDataBit -= 3;
	}
	
	return data_end;
}

/*-----------------------------------------------------------------------
	Nake Next Cycle
 -----------------------------------------------------------------------*/
static void SyroVolcaSample_CycleHandler(SyroManage *psm)
{
	int write_page;
	uint32_t comp_len, org_len;
	
	write_page = (psm->CyclePos / KORGSYRO_QAM_CYCLE) ^ 1;
	
	switch (psm->TaskStatus) {
		case TaskStatus_Gap:
			SyroFunc_MakeGap(psm->Channel, write_page);
			if (!(--psm->TaskCount)) {
				psm->TaskStatus = TaskStatus_StartMark;
				SyroVolcaSample_SetupBlock(psm);
			}
			break;
		
		case TaskStatus_StartMark:
			SyroFunc_MakeStartMark(psm->Channel, write_page);
			psm->TaskStatus = TaskStatus_ChannelInfo;
			break;
		
		case TaskStatus_ChannelInfo:
			SyroFunc_MakeChannelInfo(psm->Channel, write_page);
			psm->TaskStatus = TaskStatus_Data;
			break;
		
		case TaskStatus_Data:
			if (SyroVolcaSample_MakeData(psm, write_page)) {
				if (psm->DataCount < psm->DataSize) {
					int pos, size;
					
					size = (psm->DataSize - psm->DataCount);
					if (size >= BLOCK_SIZE) {
						size = BLOCK_SIZE;
					} else {
						memset(psm->TxBlock, 0, BLOCK_SIZE);
					}
					if (psm->SampleEndian == LittleEndian) {
						memcpy(psm->TxBlock, (psm->pSrcData+psm->DataCount), size);
					} else {
						for (pos=0; pos<size; pos+=2) {
							psm->TxBlock[pos] = psm->pSrcData[psm->DataCount+pos+1];
							psm->TxBlock[pos+1] = psm->pSrcData[psm->DataCount+pos];
						}
					}
					psm->TaskStatus = TaskStatus_Gap;
					psm->TaskCount = NUM_OF_GAP_CYCLE;
					
					if (!psm->IsCompData) {
						if (psm->EraseAlign && (!(psm->DataCount % psm->EraseAlign))) {
							psm->TaskCount = psm->EraseLength;
						}
					} else {
						if (psm->EraseCount && (psm->CompBlockPos < (psm->DataCount+size))) {
							
							psm->TaskCount = psm->EraseLength;
							psm->EraseCount--;
							org_len = 0;
							
							for (;;) {
								if (psm->BlockLen1st) {
									psm->CompBlockPos += psm->BlockLen1st;
									org_len += psm->BlockLen1st;
									psm->BlockLen1st = 0;
								} else {
									comp_len = (uint32_t)psm->pSrcData[psm->CompBlockPos+2];
									comp_len <<= 8;
									comp_len |= (uint32_t)psm->pSrcData[psm->CompBlockPos+3];
									psm->CompBlockPos += (comp_len+6);
									org_len += (VOLCASAMPLE_COMP_BLOCK_LEN * 2);
								}
								if ((psm->CompBlockPos >= psm->DataSize) ||
									(org_len >= psm->EraseAlign))
								{
									break;
								}
							}
						}
					}
					
					psm->TxBlockSize = BLOCK_SIZE;
					psm->DataCount += size;

				} else {
					psm->CurData++;
					if (psm->CurData < psm->NumOfData) {
						SyroVolcaSample_SetupNextData(psm);
					} else {
						psm->TaskStatus = TaskStatus_Gap_Footer;
						psm->TaskCount = NUM_OF_GAP_FOOTER_CYCLE;
					}
				}
			}
			break;
		
		case TaskStatus_Gap_Footer:
			SyroFunc_MakeGap(psm->Channel, write_page);
			if (!(--psm->TaskCount)) {
				psm->TaskStatus = TaskStatus_End;
			}
			break;
			
		default:		// case TaskStatus_End:
			return;
	}
	
	psm->FrameCountInCycle += KORGSYRO_QAM_CYCLE;
}

/*-----------------------------------------------------------------------
	Get Ch Sample
 -----------------------------------------------------------------------*/
static int16_t SyroVolcaSample_GetChSample(SyroManage *psm, int ch)
{
	int32_t dat;
	
	dat = (int32_t)psm->Channel[ch].CycleSample[psm->CyclePos];
	
	//----- LPF -----*/
	dat = ((dat * (0x10000 - LPF_FEEDBACK_LEVEL)) + 
		(psm->Channel[ch].Lpf_z * LPF_FEEDBACK_LEVEL));
	dat /= 0x10000;
	psm->Channel[ch].Lpf_z = dat;	
	
	return (int16_t)dat;
}

/*-----------------------------------------------------------------------
	Get Frame Size (union)
 -----------------------------------------------------------------------*/
static uint32_t SyroVolcaSample_GetFrameSize(int num_of_block)
{
	uint32_t size;
	
	size = NUM_OF_FRAME__GAP_HEADER;
	size += NUM_OF_FRAME__HEADER;
	
	size += (NUM_OF_FRAME__GAP + NUM_OF_FRAME__BLOCK) * num_of_block;
	
	return size;
}

/*-----------------------------------------------------------------------
	Get Frame Size (Pattern)
 -----------------------------------------------------------------------*/
static uint32_t SyroVolcaSample_GetFrameSize_Pattern(void)
{
	return SyroVolcaSample_GetFrameSize((VOLCASAMPLE_PATTERN_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE);
}

/*-----------------------------------------------------------------------
	Get Frame Size (Sample)
 -----------------------------------------------------------------------*/
static uint32_t SyroVolcaSample_GetFrameSize_Sample(uint32_t byte_size)
{
	uint32_t size;
	uint32_t num_of_block;
	
	num_of_block = (byte_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	size = SyroVolcaSample_GetFrameSize(num_of_block);
	
	num_of_block = (byte_size + SUBSECTOR_SIZE - 3) / (SUBSECTOR_SIZE - 2);
	size += (NUM_OF_FRAME__GAP_F - NUM_OF_FRAME__GAP) * num_of_block;
	
	return size;
}

/*-----------------------------------------------------------------------
	Get Frame Size (Sample, Compress)
 -----------------------------------------------------------------------*/
static uint32_t SyroVolcaSample_GetFrameSize_Sample_Comp(SyroData *pdata)
{
	uint32_t size, comp_size;
	uint32_t num_of_block;
	
	comp_size = SyroComp_GetCompSize(
		pdata->pData, 
		(pdata->Size / 2), 
		pdata->Quality,
		pdata->SampleEndian
	);
	
	//----- get frame size from compressed size.
	num_of_block = (comp_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	size = SyroVolcaSample_GetFrameSize(num_of_block);
	
	//----- get gap size from original size.
	num_of_block = (pdata->Size + SUBSECTOR_SIZE - 3) / (SUBSECTOR_SIZE - 2);
	size += (NUM_OF_FRAME__GAP_F - NUM_OF_FRAME__GAP) * num_of_block;
	
	return size;
}

/*-----------------------------------------------------------------------
	Get Frame Size (All)
 -----------------------------------------------------------------------*/
static uint32_t SyroVolcaSample_GetFrameSize_All(uint32_t byte_size)
{
	uint32_t size;
	uint32_t num_of_block;
	
	num_of_block = (byte_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	size = SyroVolcaSample_GetFrameSize(num_of_block);
	
	num_of_block = (num_of_block + BLOCK_PER_SECTOR - 1) / BLOCK_PER_SECTOR;
	size += (NUM_OF_FRAME__GAP_3S - NUM_OF_FRAME__GAP) * num_of_block;
	
	return size;
}

/*-----------------------------------------------------------------------
	Get Frame Size (All, Comp)
 -----------------------------------------------------------------------*/
static uint32_t SyroVolcaSample_GetFrameSize_AllComp(SyroData *pdata)
{
	uint32_t size, comp_size;
	uint32_t num_of_block;
	
	if (pdata->Size == ALL_INFO_SIZE) {
		return SyroVolcaSample_GetFrameSize_All(pdata->Size);
	}
	
	comp_size = SyroComp_GetCompSize(
		(pdata->pData + ALL_INFO_SIZE),  
		((pdata->Size - ALL_INFO_SIZE) / 2), 
		pdata->Quality,
		LittleEndian
	);

	comp_size += ALL_INFO_SIZE; 
	num_of_block = (comp_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	size = SyroVolcaSample_GetFrameSize(num_of_block);
	
	num_of_block = (pdata->Size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	num_of_block = (num_of_block + BLOCK_PER_SECTOR - 1) / BLOCK_PER_SECTOR;
	size += (NUM_OF_FRAME__GAP_3S - NUM_OF_FRAME__GAP) * num_of_block;
	
	return size;
}

/*-----------------------------------------------------------------------
	free compress memory
 -----------------------------------------------------------------------*/
static void SyroVolcaSample_FreeCompressMemory(SyroManage *psm)
{
	int i;
	SyroManageSingle *psms;
	
	psms = (SyroManageSingle *)(psm+1);
	
	for (i=0; i<psm->NumOfData; i++) {
		if (psms->comp_buf) {
			free(psms->comp_buf);
			psms->comp_buf = NULL;
		}
		psms++;
	}
}

/************************************************************************
	Exteral Functions
 ***********************************************************************/
/*======================================================================
	Syro Start
 ======================================================================*/
SyroStatus SyroVolcaSample_Start(SyroHandle *pHandle, SyroData *pData, int NumOfData,
	uint32_t Flags, uint32_t *pNumOfSyroFrame)
{
	int i;
	uint32_t handle_size;
	uint32_t frame_size;
	uint32_t comp_org_size, comp_dest_size, comp_ofs;
	uint8_t *comp_src_adr;
	Endian comp_endian;
	SyroManage *psm;
	SyroManageSingle *psms;
	
	//--------------------------------
	//------- Parameter check --------
	//--------------------------------
	if ((!NumOfData) || (NumOfData >= NUM_OF_DATA_MAX)) {
		return Status_IllegalParameter;
	}
	
	frame_size = 0;
	
	for (i=0; i<NumOfData; i++) {
		switch (pData[i].DataType) {
			case DataType_Sample_All:
				if (pData[i].Size < ALL_INFO_SIZE) {
					return Status_IllegalData;
				}
				frame_size += SyroVolcaSample_GetFrameSize_All(pData[i].Size);
				break;

			case DataType_Sample_AllCompress:
				if (pData[i].Size < ALL_INFO_SIZE) {
					return Status_IllegalData;
				}
				if ((pData[i].Quality < 8) || (pData[i].Quality > 16)) {
					return Status_OutOfRange_Quality;
				}
				frame_size += SyroVolcaSample_GetFrameSize_AllComp(&pData[i]);
				break;

			case DataType_Pattern:
				if (pData[i].Number >= VOLCASAMPLE_NUM_OF_PATTERN) {
					return Status_OutOfRange_Number;
				}
				frame_size += SyroVolcaSample_GetFrameSize_Pattern();
				break;

			case DataType_Sample_Compress:
				if (pData[i].Number >= VOLCASAMPLE_NUM_OF_SAMPLE) {
					return Status_OutOfRange_Number;
				}
				if ((pData[i].Quality < 8) || (pData[i].Quality > 16)) {
					return Status_OutOfRange_Quality;
				}
				frame_size += SyroVolcaSample_GetFrameSize_Sample_Comp(&pData[i]);
				break;

			case DataType_Sample_Erase:
				if (pData[i].Number >= VOLCASAMPLE_NUM_OF_SAMPLE) {
					return Status_OutOfRange_Number;
				}
				frame_size += SyroVolcaSample_GetFrameSize_Sample(0);
				break;

			case DataType_Sample_Liner:
				if (pData[i].Number >= VOLCASAMPLE_NUM_OF_SAMPLE) {
					return Status_OutOfRange_Number;
				}
				frame_size += SyroVolcaSample_GetFrameSize_Sample(pData[i].Size);
				break;
			
			default:
				return Status_IllegalDataType;
		
		}
	}
	frame_size += NUM_OF_FRAME__GAP_FOOTER;
	
	//-----------------------------
	//------- Alloc Memory --------
	//-----------------------------
	
	handle_size = sizeof(SyroManage) + (sizeof(SyroManageSingle) * NumOfData);
	psm = (SyroManage *)malloc(handle_size);
	if (!psm) {
		return Status_NotEnoughMemory;
	}
	psms = (SyroManageSingle *)(psm+1);
	
	//----------------------
	//------- Setup --------
	//----------------------
	
	memset((uint8_t *)psm, 0, handle_size);
	psm->Header = SYRO_MANAGE_HEADER;
	psm->Flags = Flags;
	
	psm->NumOfData = NumOfData;
	for (i=0; i<NumOfData; i++) {
		psms[i].Data = pData[i];
		
		comp_org_size = 0;
		comp_ofs = 0;
		comp_src_adr = 0;
		comp_endian = LittleEndian;
		
		switch (pData[i].DataType) {
			case DataType_Sample_Compress:
				comp_src_adr = pData[i].pData;
				comp_org_size = (pData[i].Size / 2);
				comp_endian = pData[i].SampleEndian;
				break;
			
			case DataType_Sample_AllCompress:
				if (psms[i].Data.Size == ALL_INFO_SIZE) {
					psms[i].Data.DataType = DataType_Sample_All;
					break;
				}
				comp_ofs = ALL_INFO_SIZE;
				comp_src_adr = pData[i].pData + ALL_INFO_SIZE;
				comp_org_size = ((pData[i].Size - ALL_INFO_SIZE) / 2);
				comp_endian = LittleEndian;
				
			
			default:
				break;
		}
		
		if (comp_org_size) {
			comp_dest_size = SyroComp_GetCompSize(
				comp_src_adr,
				comp_org_size,
				pData[i].Quality,
				comp_endian
			);

			comp_dest_size = (comp_dest_size + BLOCK_SIZE - 1) & (~(BLOCK_SIZE-1));
			psms[i].comp_size = (comp_dest_size + comp_ofs);
			psms[i].comp_buf = malloc(comp_dest_size + comp_ofs);
			if (!psms[i].comp_buf) {
				SyroVolcaSample_FreeCompressMemory(psm);
				free((uint8_t *)psm);
				return Status_NotEnoughMemory;
			};
			memset(psms[i].comp_buf, 0, comp_dest_size);
			if (comp_ofs) {
				memcpy(psms[i].comp_buf, pData[i].pData, comp_ofs);
			}
			SyroComp_Comp(comp_src_adr, (psms[i].comp_buf+comp_ofs), comp_org_size, 
				pData[i].Quality, comp_endian);
		}
	}

	SyroVolcaSample_SetupNextData(psm);
	
	for (i=0; i<KORGSYRO_NUM_OF_CYCLE; i++) {
		SyroVolcaSample_CycleHandler(psm);
		psm->CyclePos += KORGSYRO_QAM_CYCLE;
	}
	psm->CyclePos = 0;
	
	*pHandle = (SyroHandle)psm;
	*pNumOfSyroFrame = frame_size;
	
	return Status_Success;
}

/*======================================================================
	Syro Get Sample
 ======================================================================*/	
SyroStatus SyroVolcaSample_GetSample(SyroHandle Handle, int16_t *pLeft, int16_t *pRight)
{
	SyroManage *psm;
	
	psm = (SyroManage *)Handle;
	if (psm->Header != SYRO_MANAGE_HEADER) {
		return Status_InvalidHandle;
	}
	
	if (!psm->FrameCountInCycle) {
		return Status_NoData;
	}
	
	*pLeft = SyroVolcaSample_GetChSample(psm, 0);
	*pRight = SyroVolcaSample_GetChSample(psm, 1);
	
	psm->FrameCountInCycle--;
	if ((++psm->CyclePos) == KORGSYRO_NUM_OF_CYCLE_BUF) {
		psm->CyclePos = 0;
	}
	
	if (!(psm->CyclePos % KORGSYRO_QAM_CYCLE)) {
		SyroVolcaSample_CycleHandler(psm);
	}
	
	return Status_Success;
}

/*======================================================================
	Syro End
 ======================================================================*/	
SyroStatus SyroVolcaSample_End(SyroHandle Handle)
{
	SyroManage *psm;
	
	psm = (SyroManage *)Handle;
	if (psm->Header != SYRO_MANAGE_HEADER) {
		return Status_InvalidHandle;
	}

	SyroVolcaSample_FreeCompressMemory(psm);

	free((uint8_t *)psm);
	
	return Status_Success;
}

