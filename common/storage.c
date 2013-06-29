////////////////////////////////////////////////////////////////////////////////
// Presets and settings storage, relies on low level page storage system
////////////////////////////////////////////////////////////////////////////////

#include "storage.h"
#include "uart_6850.h"

// increment this each time the binary format is changed
#define STORAGE_VERSION 1

#define STORAGE_MAGIC 0x006116a5
#define STORAGE_MAX_SIZE 1024

#define SETTINGS_PAGE_COUNT 4
#define SETTINGS_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-SETTINGS_PAGE_COUNT)

const uint8_t steppedParametersBits[spCount] = 
{
	/*ASaw*/1,
	/*ATri*/1,
	/*ASqr*/1,
	/*BSaw*/1,
	/*BTri*/1,
	/*BSqr*/1,
	/*Sync*/1,
	/*PModFA*/1,
	/*PModFil*/1,
	/*LFOShape*/3,
	/*LFOShift*/2,
	/*LFOTargets*/3,
	/*TrackingShift*/2,
	/*FilEnvExpo*/1,
	/*FilEnvSlow*/1,
	/*AmpEnvExpo*/1,
	/*AmpEnvSlow*/1,
	/*Unison*/1,
	/*AssignerMonoMode*/2,
	/*BenderSemitones*/4,
	/*BenderTarget*/2,
	/*ModwheelShift*/3,
	/*ChromaticPitch*/1,
};

struct settings_s settings;
struct preset_s currentPreset;

static uint8_t temp[STORAGE_MAX_SIZE];
static uint8_t * tempPtr;
static uint8_t tempVersion;

static uint32_t storageRead32(void)
{
	uint32_t v;
	v=*(uint32_t*)tempPtr;
	tempPtr+=sizeof(v);
	return v;
}

static uint16_t storageRead16(void)
{
	uint16_t v;
	v=*(uint16_t*)tempPtr;
	tempPtr+=sizeof(v);
	return v;
}

/*
static int16_t storageReadS16(void)
{
	int16_t v;
	v=*(int16_t*)tempPtr;
	tempPtr+=sizeof(v);
	return v;
}
*/

static uint8_t storageRead8(void)
{
	uint8_t v;
	v=*(uint8_t*)tempPtr;
	tempPtr+=sizeof(v);
	return v;
}

static int8_t storageReadS8(void)
{
	int8_t v;
	v=*(int8_t*)tempPtr;
	tempPtr+=sizeof(v);
	return v;
}

static void storageWrite32(uint32_t v)
{
	*(uint32_t*)tempPtr=v;
	tempPtr+=sizeof(v);
}

static void storageWrite16(uint16_t v)
{
	*(uint16_t*)tempPtr=v;
	tempPtr+=sizeof(v);
}

/*
static void storageWriteS16(int16_t v)
{
	*(int16_t*)tempPtr=v;
	tempPtr+=sizeof(v);
}
*/

static void storageWrite8(uint8_t v)
{
	*(uint8_t*)tempPtr=v;
	tempPtr+=sizeof(v);
}

static void storageWriteS8(int8_t v)
{
	*(int8_t*)tempPtr=v;
	tempPtr+=sizeof(v);
}

static LOWERCODESIZE void storageLoad(uint16_t pageIdx, uint8_t pageCount)
{
	uint16_t i;
	
	for (i=0;i<pageCount;++i)
		storage_read(pageIdx+i,&temp[STORAGE_PAGE_SIZE*i]);
	
	tempPtr=temp;
	tempVersion=0;

	if(storageRead32()!=STORAGE_MAGIC)
	{
#ifdef DEBUG
		print("Error: bad page !\n"); 
#endif	
		memset(temp,0,sizeof(temp));
		return;
	}

	tempVersion=storageRead8();
}

static LOWERCODESIZE void storagePrepareStore(void)
{
	memset(temp,0,sizeof(temp));
	tempPtr=temp;
	tempVersion=STORAGE_VERSION;
	
	storageWrite32(STORAGE_MAGIC);
	storageWrite8(tempVersion);
}

static LOWERCODESIZE void storageFinishStore(uint16_t pageIdx, uint8_t pageCount)
{
	if((tempPtr-temp)>sizeof(temp))
	{
#ifdef DEBUG
		print("Error: writing too much data to storage !\n"); 
#endif	
		return;
	}
	
	uint16_t i;
	
	for (i=0;i<pageCount;++i)
		storage_write(pageIdx+i,&temp[STORAGE_PAGE_SIZE*i]);
}

LOWERCODESIZE int8_t settings_load(void)
{
	int8_t i,j;
	
	BLOCK_INT
	{
		storageLoad(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);

		if (tempVersion<1)
			return 0;

		// v1

		for(j=0;j<TUNER_OCTAVE_COUNT;++j)
			for(i=0;i<TUNER_CV_COUNT;++i)
				settings.tunes[j][i]=storageRead16();

		settings.presetNumber=storageRead16();
		settings.benderMiddle=storageRead16();
		settings.presetBank=storageRead8();
		settings.midiReceiveChannel=storageReadS8();
		
		if (tempVersion<2)
			return 1;

		// v2

		// ...
	
	
	}
	
	return 1;
}

LOWERCODESIZE void settings_save(void)
{
	int8_t i,j;
	
	BLOCK_INT
	{
		storagePrepareStore();

		// v1

		for(j=0;j<TUNER_OCTAVE_COUNT;++j)
			for(i=0;i<TUNER_CV_COUNT;++i)
				storageWrite16(settings.tunes[j][i]);

		storageWrite16(settings.presetNumber);
		storageWrite16(settings.benderMiddle);
		storageWrite8(settings.presetBank);
		storageWriteS8(settings.midiReceiveChannel);
		
		// v2 

		// ...

		// this must stay last
		storageFinishStore(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);
	}
}

LOWERCODESIZE int8_t preset_loadCurrent(uint16_t number)
{
	BLOCK_INT
	{
		storageLoad(number,1);

		if (tempVersion<1)
			return 0;

		// v1
		
		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpFilVelocity;++cp)
			currentPreset.continuousParameters[cp]=storageRead16();

		steppedParameter_t sp;
		for(sp=spASaw;sp<=spChromaticPitch;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();
	}
	
	return 1;
}

LOWERCODESIZE void preset_saveCurrent(uint16_t number)
{
	BLOCK_INT
	{
		storagePrepareStore();

		// v1
		
		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpFilVelocity;++cp)
			storageWrite16(currentPreset.continuousParameters[cp]);

		steppedParameter_t sp;
		for(sp=spASaw;sp<=spChromaticPitch;++sp)
			storageWrite8(currentPreset.steppedParameters[sp]);
		
		// this must stay last
		storageFinishStore(number,1);
	}
}

LOWERCODESIZE void storage_export(uint16_t number, uint8_t * buf, int16_t * size)
{
	int16_t actualSize;

	BLOCK_INT
	{
		storageLoad(number,1);

		// don't export trailing zeroes		
		
		actualSize=STORAGE_PAGE_SIZE;
		while(temp[actualSize-1]==0)
			--actualSize;
		
		buf[0]=number;		
		memcpy(&buf[1],temp,actualSize);
		*size=actualSize+1;
	}
}

LOWERCODESIZE void storage_import(uint16_t number, uint8_t * buf, int16_t size)
{
	BLOCK_INT
	{
		memset(temp,0,sizeof(temp));
		memcpy(temp,buf,size);
		tempPtr=temp+size;
		storageFinishStore(number,1);
	}
}
