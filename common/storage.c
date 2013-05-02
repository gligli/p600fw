////////////////////////////////////////////////////////////////////////////////
// Presets and settings storage, relies on low level page storage system
////////////////////////////////////////////////////////////////////////////////

#include "storage.h"

// increment this each time the binary format is changed
#define STORAGE_VERSION 1

#define STORAGE_MAGIC 0x611600a5
#define STORAGE_MAX_SIZE 1024

#define SETTINGS_PAGE_COUNT 4
#define SETTINGS_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-SETTINGS_PAGE_COUNT)

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

static void storageLoad(uint16_t pageIdx, uint8_t pageCount)
{
	int i;
	
	for (i=0;i<pageCount;++i)
		storage_read(pageIdx+i,&temp[STORAGE_PAGE_SIZE*i]);
	
	tempPtr=temp;
	tempVersion=0;

	if(storageRead32()!=STORAGE_MAGIC)
	{
		print("Error: bad page !\n"); 
		memset(temp,0,sizeof(temp));
		return;
	}

	tempVersion=storageRead8();
}

static void storagePrepareStore(void)
{
	memset(temp,0,sizeof(temp));
	tempPtr=temp;
	tempVersion=STORAGE_VERSION;
	
	storageWrite32(STORAGE_MAGIC);
	storageWrite8(tempVersion);
}

static void storageFinishStore(uint16_t pageIdx, uint8_t pageCount)
{
	if((tempPtr-temp)>sizeof(temp))
	{
		print("Error: writing too much data to storage !\n"); 
		return;
	}
	
	int i;
	
	for (i=0;i<pageCount;++i)
		storage_write(pageIdx+i,&temp[STORAGE_PAGE_SIZE*i]);
}

int8_t settings_load(void)
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

void settings_save(void)
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

		storageFinishStore(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);
	}
}

int8_t preset_loadCurrent(uint16_t number)
{
	BLOCK_INT
	{
		storageLoad(number,1);

		if (tempVersion<1)
			return 0;

		// v1
		
		currentPreset.bitParameters=storageRead32();
		
		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpGlide;++cp)
			currentPreset.continuousParameters[cp]=storageRead16();

		currentPreset.envFlags[0]=storageRead8();
		currentPreset.envFlags[1]=storageRead8();

		currentPreset.trackingShift=storageRead8();

		currentPreset.assignerMonoMode=storageRead8();

		currentPreset.lfoAltShapes=storageReadS8();
		currentPreset.lfoTargets=storageRead8();
		currentPreset.lfoShift=storageRead8();

		currentPreset.modwheelShift=storageReadS8();

		currentPreset.benderSemitones=storageReadS8();
		currentPreset.benderTarget=storageRead8();
	}
	
	return 1;
}

void preset_saveCurrent(uint16_t number)
{
	BLOCK_INT
	{
		storagePrepareStore();

		// v1
		
		storageWrite32(currentPreset.bitParameters);
		
		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpGlide;++cp)
			storageWrite16(currentPreset.continuousParameters[cp]);

		storageWrite8(currentPreset.envFlags[0]);
		storageWrite8(currentPreset.envFlags[1]);

		storageWrite8(currentPreset.trackingShift);

		storageWrite8(currentPreset.assignerMonoMode);

		storageWriteS8(currentPreset.lfoAltShapes);
		storageWrite8(currentPreset.lfoTargets);
		storageWrite8(currentPreset.lfoShift);

		storageWriteS8(currentPreset.modwheelShift);

		storageWriteS8(currentPreset.benderSemitones);
		storageWrite8(currentPreset.benderTarget);

		storageFinishStore(number,1);
	}
}

