////////////////////////////////////////////////////////////////////////////////
// Presets and settings storage, relies on low level page storage system
////////////////////////////////////////////////////////////////////////////////

#include "storage.h"
#include "uart_6850.h"

// increment this each time the binary format is changed
#define STORAGE_VERSION 4

#define STORAGE_MAGIC 0x006116a5
#define STORAGE_MAX_SIZE 512

#define SETTINGS_PAGE_COUNT 2
#define SETTINGS_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-4)

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
	/*LFOTargets*/6,
	/*TrackingShift*/2,
	/*FilEnvExpo*/1,
	/*FilEnvSlow*/1,
	/*AmpEnvExpo*/1,
	/*AmpEnvSlow*/1,
	/*Unison*/1,
	/*AssignerPriority*/2,
	/*BenderSemitones*/4,
	/*BenderTarget*/2,
	/*ModwheelShift*/3,
	/*ChromaticPitch*/2,
	/*ModwheelTarget*/1,
	/*VibTarget*/2,
};

struct settings_s settings;
struct preset_s currentPreset;

static struct
{
	uint8_t buffer[STORAGE_MAX_SIZE];
	uint8_t * bufPtr;
	uint8_t version;
} storage;

static uint32_t storageRead32(void)
{
	uint32_t v;
	v=*(uint32_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}

static uint16_t storageRead16(void)
{
	uint16_t v;
	v=*(uint16_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}

/*
static int16_t storageReadS16(void)
{
	int16_t v;
	v=*(int16_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}
*/

static uint8_t storageRead8(void)
{
	uint8_t v;
	v=*(uint8_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}

static int8_t storageReadS8(void)
{
	int8_t v;
	v=*(int8_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}

static void storageWrite32(uint32_t v)
{
	*(uint32_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}

static void storageWrite16(uint16_t v)
{
	*(uint16_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}

/*
static void storageWriteS16(int16_t v)
{
	*(int16_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}
*/

static void storageWrite8(uint8_t v)
{
	*(uint8_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}

static void storageWriteS8(int8_t v)
{
	*(int8_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}

static LOWERCODESIZE int8_t storageLoad(uint16_t pageIdx, uint8_t pageCount)
{
	uint16_t i;
	
	for (i=0;i<pageCount;++i)
		storage_read(pageIdx+i,&storage.buffer[STORAGE_PAGE_SIZE*i]);
	
	storage.bufPtr=storage.buffer;
	storage.version=0;

	if(storageRead32()!=STORAGE_MAGIC)
	{
#ifdef DEBUG
		print("Error: bad page: "); 
		phex(pageIdx);
		print("\n");
#endif	
		memset(storage.buffer,0,sizeof(storage.buffer));
		return 0;
	}

	storage.version=storageRead8();
	
	return 1;
}

static LOWERCODESIZE void storagePrepareStore(void)
{
	memset(storage.buffer,0,sizeof(storage.buffer));
	storage.bufPtr=storage.buffer;
	storage.version=STORAGE_VERSION;
	
	storageWrite32(STORAGE_MAGIC);
	storageWrite8(storage.version);
}

static LOWERCODESIZE void storageFinishStore(uint16_t pageIdx, uint8_t pageCount)
{
	if((storage.bufPtr-storage.buffer)>sizeof(storage.buffer))
	{
#ifdef DEBUG
		print("Error: writing too much data to storage !\n"); 
#endif	
		return;
	}
	
	uint16_t i;
	
	for (i=0;i<pageCount;++i)
		storage_write(pageIdx+i,&storage.buffer[STORAGE_PAGE_SIZE*i]);
}

LOWERCODESIZE int8_t settings_load(void)
{
	int8_t i,j;
	
	BLOCK_INT
	{
		if (!storageLoad(SETTINGS_PAGE,SETTINGS_PAGE_COUNT))
			return 0;

		// defaults

		settings.voiceMask=0x3f;
		settings.spread=0;

		if (storage.version<1)
			return 1;

		// v1

		for(j=0;j<TUNER_OCTAVE_COUNT;++j)
			for(i=0;i<TUNER_CV_COUNT;++i)
				settings.tunes[j][i]=storageRead16();

		settings.presetNumber=storageRead16();
		settings.benderMiddle=storageRead16();
		settings.presetMode=storageRead8();
		settings.midiReceiveChannel=storageReadS8();
		
		if (storage.version<2)
			return 1;

		// v2

		settings.voiceMask=storageRead8();
		settings.midiSendChannel=storageReadS8();
		
		if (storage.version<3)
			return 1;

		// v3
		
		settings.syncMode=storageRead8();
	
		if (storage.version<4)
			return 1;
		
		// v4
		
		settings.spread=storageReadS8();

		if (storage.version<5)
			return 1;

		// v5
		
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
		storageWrite8(settings.presetMode);
		storageWriteS8(settings.midiReceiveChannel);
		
		// v2
		
		storageWrite8(settings.voiceMask);
		storageWriteS8(settings.midiSendChannel);

		// v3
		
		storageWrite8(settings.syncMode);
		
		// v4
		
		storageWriteS8(settings.spread);
		
		// v5
		
		// ...

		// this must stay last
		storageFinishStore(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);
	}
}

LOWERCODESIZE int8_t preset_loadCurrent(uint16_t number)
{
	int8_t i;
	
	BLOCK_INT
	{
		if(!storageLoad(number,1))
			return 0;

		// defaults
		
		currentPreset.steppedParameters[spAssignerPriority]=apLast;
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			currentPreset.voicePattern[i]=(i==0)?0:ASSIGNER_NO_NOTE;
		
		if (storage.version<1)
			return 1;

		// v1
		
		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpFilVelocity;++cp)
			currentPreset.continuousParameters[cp]=storageRead16();

		steppedParameter_t sp;
		for(sp=spASaw;sp<=spChromaticPitch;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();

		if (storage.version<2)
			return 1;

		// v2

		for(cp=cpModDelay;cp<=cpSeqArpClock;++cp)
			currentPreset.continuousParameters[cp]=storageRead16();

		for(sp=spModwheelTarget;sp<=spVibTarget;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			currentPreset.voicePattern[i]=storageRead8();
	}
	
	return 1;
}

LOWERCODESIZE void preset_saveCurrent(uint16_t number)
{
	int8_t i;
	
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
		
		// v2
		
		for(cp=cpModDelay;cp<=cpSeqArpClock;++cp)
			storageWrite16(currentPreset.continuousParameters[cp]);

		for(sp=spModwheelTarget;sp<=spVibTarget;++sp)
			storageWrite8(currentPreset.steppedParameters[sp]);

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			storageWrite8(currentPreset.voicePattern[i]);
		
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
		while(storage.buffer[actualSize-1]==0)
			--actualSize;
		
		buf[0]=number;		
		memcpy(&buf[1],storage.buffer,actualSize);
		*size=actualSize+1;
	}
}

LOWERCODESIZE void storage_import(uint16_t number, uint8_t * buf, int16_t size)
{
	BLOCK_INT
	{
		memset(storage.buffer,0,sizeof(storage.buffer));
		memcpy(storage.buffer,buf,size);
		storage.bufPtr=storage.buffer+size;
		storageFinishStore(number,1);
	}
}

LOWERCODESIZE void preset_loadDefault(int8_t makeSound)
{
	BLOCK_INT
	{
		memset(&currentPreset,0,sizeof(currentPreset));

		currentPreset.continuousParameters[cpAPW]=HALF_RANGE;
		currentPreset.continuousParameters[cpBPW]=HALF_RANGE;
		currentPreset.continuousParameters[cpCutoff]=UINT16_MAX;
		currentPreset.continuousParameters[cpFilEnvAmt]=HALF_RANGE;
		currentPreset.continuousParameters[cpAmpSus]=UINT16_MAX;
		currentPreset.continuousParameters[cpVolA]=UINT16_MAX;
		currentPreset.continuousParameters[cpAmpVelocity]=HALF_RANGE;
		currentPreset.continuousParameters[cpSeqArpClock]=HALF_RANGE;
		currentPreset.continuousParameters[cpVibFreq]=HALF_RANGE;

		currentPreset.steppedParameters[spBenderSemitones]=5;
		currentPreset.steppedParameters[spBenderTarget]=modVCO;
		currentPreset.steppedParameters[spFilEnvExpo]=1;
		currentPreset.steppedParameters[spAmpEnvExpo]=1;
		currentPreset.steppedParameters[spModwheelShift]=1;
		currentPreset.steppedParameters[spChromaticPitch]=2; // octave
		
		memset(currentPreset.voicePattern,ASSIGNER_NO_NOTE,sizeof(currentPreset.voicePattern));

		if(makeSound)
			currentPreset.steppedParameters[spASaw]=1;
	}
}

LOWERCODESIZE void settings_loadDefault(void)
{
	BLOCK_INT
	{
		memset(&settings,0,sizeof(settings));
		
		settings.benderMiddle=HALF_RANGE;
		settings.midiReceiveChannel=-1;
		settings.voiceMask=0x3f;
		
		tuner_init(); // use theoretical tuning
	}
}
