#ifndef KORG_SYRO_VOLCASAMPLE_H__
#define KORG_SYRO_VOLCASAMPLE_H__

#include "korg_syro_type.h"

//////////////////////////////////////////////////////////////////
// SYRO VERSION 1.00 

#define SYRO_VOLCASAMPLE_VERSION		0x100		

//////////////////////////////////////////////////////////////////
#define	VOLCASAMPLE_NUM_OF_SAMPLE		100
#define	VOLCASAMPLE_NUM_OF_PATTERN		10

#define VOLCASAMPLE_PATTERN_SIZE		0xA40

typedef enum {
	Status_Success,
	
	//------ Start -------
	Status_IllegalDataType,
	Status_IllegalData,
	Status_IllegalParameter,
	Status_OutOfRange_Number,
	Status_OutOfRange_Quality,
	Status_NotEnoughMemory,
	
	//------ GetSample/End  -------
	Status_InvalidHandle,
	Status_NoData
} SyroStatus;

typedef enum {
	LittleEndian,
	BigEndian
} Endian;

typedef enum {
    DataType_Sample_Liner,
    DataType_Sample_Compress,
    DataType_Sample_Erase,
    DataType_Sample_All,
    DataType_Sample_AllCompress,
    DataType_Pattern
} SyroDataType;

typedef struct {
    SyroDataType DataType;
    uint8_t *pData;
    uint32_t Number;		// Sample:0-99, Pattern:0-9
    uint32_t Size;			// Byte Size (if type=Sample)
    uint32_t Quality;		// specific Sample bit (8-16), if type=LossLess
	uint32_t Fs;
	Endian SampleEndian;
} SyroData;

typedef void* SyroHandle;

/*-------------------------*/
/*------ Functions --------*/
/*-------------------------*/
#ifdef __cplusplus
extern "C"
{
#endif

SyroStatus SyroVolcaSample_Start(SyroHandle *pHandle, SyroData *pData, int NumOfData,
	uint32_t Flags, uint32_t *pNumOfSyroFrame);

SyroStatus SyroVolcaSample_GetSample(SyroHandle Handle, int16_t *pLeft, int16_t *pRight);

SyroStatus SyroVolcaSample_End(SyroHandle Handle);

#ifdef __cplusplus
}
#endif

#endif	// KORG_SYRO_H__

