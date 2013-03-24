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

static uint8_t storageRead8(void)
{
	uint8_t v;
	v=*(uint8_t*)tempPtr;
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

static void storageWrite8(uint8_t v)
{
	*(uint8_t*)tempPtr=v;
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

		settings.currentPresetNumber=storageRead16();
		
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

		storageWrite16(settings.currentPresetNumber);
		
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


		
	}
	
	return 1;
}

void preset_saveCurrent(uint16_t number)
{
	BLOCK_INT
	{
		storagePrepareStore();


		storageFinishStore(number,1);
	}
}

