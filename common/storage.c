////////////////////////////////////////////////////////////////////////////////
// Presets and settings storage, relies on low level page storage system
////////////////////////////////////////////////////////////////////////////////

#include "storage.h"
#include "uart_6850.h"
#include "synth.h"
#include "ui.h"
#include "math.h"
#include "midi.h"
#include "display.h"

// increment this each time the binary format is changed
#define STORAGE_VERSION 8

#define STORAGE_MAGIC 0x006116a5

#define SETTINGS_PAGE_COUNT 2 // due to the tuning data the settings are too large for one page, with version 8: 305 bytes (one page has 256 bytes)
#define SETTINGS_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-4)

#define STORAGE_MAX_SIZE (SETTINGS_PAGE_COUNT*STORAGE_PAGE_SIZE) // this is the buffer size, which must at least hold the settings data (see above)

const uint8_t steppedParameterRange[spCount] =
{
    /* Osc A Saw */ 2,
    /* Osc A Triangle */ 2,
    /* Osc A Square */ 2,
    /* Osc B Saw */ 2,
    /* Osc B Triangle */ 2,
    /* Osc B Sqr */ 2,
    /* Sync */ 2,
    /* Poly Mod Oscillator A Destination */ 2,
    /* Poly Mod Filter Destination */ 2,
    /* LFO Shape */ 3,
    /* Envelope Routing */ 4,
    /* LFO Mode Destination */ 4,
    /* Keyboard Filter Tracking */ 3,
    /* Filter Envelope Shape */ 2,
    /* Filter Envelope Fast/Slow */ 2,
    /* 2nd Envelope Shape */ 2,
    /* Unison */ 2,
    /* Assigner Priority Mode */ 3,
    /* Pitch bender semitones */ 4,
    /* Pitch bender target */ 5,
    /* LFO Sync */ 8,
    /* Osc pitch mode */ 3,
    /* Modulation wheel target */ 2,
    /* Vibrato target */ 2,
    /* 2nd Envelope Fast/Slow */ 2,
    /* PW Sync Bug */ 2,
    /* spCount */ 0,

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

static void resetPickUps(void)
{
    uint8_t cnt;
    for (cnt=0;cnt<cpCount;++cnt) currentPreset.contParamPotStatus[cnt]=0;
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
		settings.vcfLimit=0;
		settings.seqArpClock=HALF_RANGE;
		settings.presetNumber=1; // if no other info, set selected preset to 1
		settings.benderMiddle=HALF_RANGE;
		settings.presetMode=0; // if no info, default start up is in live mode
		settings.midiReceiveChannel=-1; // default is 'OMNI'
		settings.voiceMask=0x3f; // default is: all on
		settings.midiSendChannel=0; // default is: 1
		settings.syncMode=smInternal; // default is internal clock
		settings.vcfLimit=0; // default is: no limit on the VCF
		settings.midiMode=0; // normal mode
		settings.panelLayout=0; // GliGli layout

		if (storage.version<1)
			return 1;

		// v1

		for(j=0;j<TUNER_OCTAVE_COUNT;++j)
			for(i=0;i<TUNER_CV_COUNT;++i)
				settings.tunes[j][i]=storageRead16();

		settings.presetNumber=storageRead16();
		// ensure that preset channel is valid, default to 0:
		if (settings.presetNumber>99) settings.presetNumber=0;
	    settings.benderMiddle=storageRead16();
		settings.presetMode=storageRead8();
		// ensure that MIDI channel is valid to void array out of bounds problems:
		settings.midiReceiveChannel=storageReadS8();
		if (settings.midiReceiveChannel<-1) settings.midiReceiveChannel=-1;
		if (settings.midiReceiveChannel>15) settings.midiReceiveChannel=15;

		if (storage.version<2)
			return 1;

		// v2

		settings.voiceMask=storageRead8();
		// ensure that MIDI channel is valid to void array out of bounds problems:
		settings.midiSendChannel=storageReadS8();	     
		if (settings.midiSendChannel<0) settings.midiSendChannel=0;
		if (settings.midiSendChannel>15) settings.midiSendChannel=15;
		
		if (storage.version<3)
			return 1;

		// v3
		
		settings.syncMode=storageRead8();
	
		if (storage.version<4)
			return 1;
		
		// v4
		
		i=storageRead8(); // this used to be "Spread" move to patch with V8 - need to read and ignore it

		if (storage.version<5)
			return 1;

		// v5
		
		settings.vcfLimit=storageReadS8();

		if (storage.version<6)
			return 1;

		// v6
		
		settings.seqArpClock=storageRead16();

		if (storage.version<8)
        {
			return 1;
        }

		// v8

		settings.midiMode=storageRead8();
        settings.midiMode=(settings.midiMode>1)?0:settings.midiMode;

		settings.panelLayout=storageRead8();
        settings.panelLayout=(settings.panelLayout>1)?0:settings.panelLayout;

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
		
		storageWrite8(0x00); // this 8bit slot is obsolete
		
		// v5
		
		storageWriteS8(settings.vcfLimit);

		// v6
		
		//settings.seqArpClock=currentPreset.continuousParameters[cpSeqArpClock];
		storageWrite16(settings.seqArpClock);

		// v8

		storageWrite8(settings.midiMode);
		storageWrite8(settings.panelLayout);

		// this must stay last
		storageFinishStore(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);
	}
}


LOWERCODESIZE int8_t preset_checkPage(uint16_t number)
{
	BLOCK_INT
	{
		if(!storageLoad(number,1))
		{
			return 0;
		}
	}
	return 1;
}

LOWERCODESIZE int8_t preset_loadCurrent(uint16_t number, uint8_t loadFromBuffer)
{
	uint8_t i;
	int8_t readVar;
	int16_t readVarLong;
	
	BLOCK_INT
	{

		// defaults
		preset_loadDefault(0);

        if (!loadFromBuffer)
        {
            if(!storageLoad(number,1))
                return 0;
        }
        else
        {
            // check the storage MAGIC
            storage.bufPtr=storage.buffer;

            if(storageRead32()!=STORAGE_MAGIC)
            {
                memset(storage.buffer,0,sizeof(storage.buffer));
                return 0;
            }
            storage.version=storageRead8();
            resetPickUps();
        }

        // compatibility with previous versions require the ""Pulse Width Sync Bug""
        // --> for loading from old storage versions also override the default patch value "off"""
        currentPreset.steppedParameters[spPWMBug]=1; // == bug "on"" for compatibility

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			currentPreset.voicePattern[i]=(i==0)?0:ASSIGNER_NO_NOTE;
		
		if (storage.version<1)
			return 1;

		// v1
		
		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpFilVelocity;++cp)
			currentPreset.continuousParameters[cp]=storageRead16();

		steppedParameter_t sp;
		for(sp=spASaw;sp<=spLFOShape;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();

        readVar=storageRead8(); // this is legacy spLFOShift (see rescaling below, re-designated LFO Sync
        if (storage.version<8)
        {
            // in this case readVar contains the legacy LFO speed range, where value 1 was "fast"
            // rescale the LFO speed (the speed switch parameter was omitted from version 8 after)
            // the exponential factor (ratio) was changed from 13000 to 8000
            currentPreset.continuousParameters[cpLFOFreq]=(uint16_t)(0.615385f*(float)currentPreset.continuousParameters[cpLFOFreq])+25205;
            // The slow LFO variant in version 7 / 2.1RC3 was made a factor of 8 slower comapred to the fast setting, so:
            if (readVar==0) currentPreset.continuousParameters[cpLFOFreq]-=16635; // =0 used to be the slow setting

        }

		for(sp=spLFOTargets;sp<=spChromaticPitch;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();

		// remap of the sp values prior to version 8
        if (storage.version<8)
        {
            // rescale the LFO amount as of version 8
            // this is the inverse of the scaling functions applied to the LFO and vib amounts to make it smoother (small difference to stay within uint16_t here)
            currentPreset.continuousParameters[cpLFOAmt]=(currentPreset.continuousParameters[cpLFOAmt]<=512)?0:(512+(uint16_t)(15000.0f*log((((float)(currentPreset.continuousParameters[cpLFOAmt]-512))/870.0f)+1)));

            // remap the exponential release and decay times after the phase lookup was updated (made longer mapping theorectial 285 to new 256)
            if (currentPreset.steppedParameters[spAmpEnvShape]==1) // exponential
            {
                currentPreset.continuousParameters[cpAmpRel]=(uint16_t)(currentPreset.continuousParameters[cpAmpRel]*0.895f);
                currentPreset.continuousParameters[cpAmpDec]=(uint16_t)(currentPreset.continuousParameters[cpAmpDec]*0.895f);
            }
            currentPreset.continuousParameters[cpAmpAtt]=(uint16_t)(currentPreset.continuousParameters[cpAmpAtt]*0.895f);
            if (currentPreset.steppedParameters[spFilEnvShape]==1) // exponential
            {
                currentPreset.continuousParameters[cpFilRel]=(uint16_t)(currentPreset.continuousParameters[cpFilRel]*0.895f);
                currentPreset.continuousParameters[cpFilDec]=(uint16_t)(currentPreset.continuousParameters[cpFilDec]*0.895f);
            }
            currentPreset.continuousParameters[cpFilAtt]=(uint16_t)(currentPreset.continuousParameters[cpFilAtt]*0.895f);
        }
		
		currentPreset.steppedParameters[spAmpEnvSlow]=currentPreset.steppedParameters[holdPedal];

        // update mixer variables depending on panel layout
        mixer_updatePanelLayout(settings.panelLayout);

		if (storage.version<2)
			return 1;

		// v2

		for(cp=cpModDelay;cp<=cpSeqArpClock;++cp) // need to read out speed (last param, cpSeqArpClock) but not use, see below. speed is part of settings
			currentPreset.continuousParameters[cp]=storageRead16();

		currentPreset.continuousParameters[cpSeqArpClock]=settings.seqArpClock; // display uses current preset param, so replicate it from settings

		for(sp=spModwheelTarget;sp<=spVibTarget;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			currentPreset.voicePattern[i]=storageRead8();

        // remap of the sp values prior to version 8
        if (storage.version<8)
        {
            // rescale the vib amount as of version 8
            // this is the inverse of the scaling functions applied to the LFO and vib amounts to make it smoother (small difference to stay within uint16_t here)
			if (currentPreset.continuousParameters[cpVibAmt]<=2048)
			{
				currentPreset.continuousParameters[cpVibAmt]=0;
			}
			else
			{
				currentPreset.continuousParameters[cpVibAmt]=(uint16_t)(15000.0f*log((((float)(currentPreset.continuousParameters[cpVibAmt]-2048))/3480.0f)+1))+512;
			}
            // rescale the vib frequency; the exponential factor (ratio) was changed from 13000 to 8000.
            currentPreset.continuousParameters[cpVibFreq]=(uint16_t)(0.615385f*(float)currentPreset.continuousParameters[cpVibFreq])+25205;
        }

		if (storage.version<7)
			return 1;

		// v7
		
        for (i=0; i<TUNER_NOTE_COUNT; i++)
        {
            readVarLong=storageRead16();
            if (number!=MANUAL_PRESET_PAGE || loadFromBuffer) currentPreset.perNoteTuning[i]=readVarLong; // always reset equal tempered tuning for manual mode: keep defaults
        }
			
		if (storage.version<8)
        {
			return 1;
        }
	
		// V8

		readVar=storageRead8();
		if (readVar<=1) // only accept valid values, otherwise default stays
			currentPreset.steppedParameters[spPWMBug]=readVar;

        currentPreset.continuousParameters[cpSpread]=storageRead16();
        currentPreset.continuousParameters[cpExternal]=storageRead16();

        readVar=storageRead8(); // this is legacy mod wheel strength re-designated LFO Sync
        if (readVar<=3) currentPreset.steppedParameters[spEnvRouting]=readVar;

        readVar=storageRead8();
        currentPreset.steppedParameters[spAssign]=(readVar>1)?0:readVar;

		readVar=storageRead8();
		currentPreset.steppedParameters[spLFOSync]=(readVar>7)?0:readVar;

        for (i=0;i < 16; i++)
            currentPreset.patchName[i]=storageRead8();

	}
	
	return 1;
}

LOWERCODESIZE void preset_saveCurrent(uint16_t number)
{
	uint8_t i;
	
	BLOCK_INT
	{
		storagePrepareStore();

		// v1

		continuousParameter_t cp;
		for(cp=cpFreqA;cp<=cpFilVelocity;++cp)
			storageWrite16(currentPreset.continuousParameters[cp]);

		currentPreset.steppedParameters[holdPedal]=currentPreset.steppedParameters[spAmpEnvSlow];

		steppedParameter_t sp;
		for(sp=spASaw;sp<=spChromaticPitch;++sp)
			storageWrite8(currentPreset.steppedParameters[sp]);
		
		// v2
		
		for(cp=cpModDelay;cp<cpSeqArpClock;++cp) // skip arp/seq clock
			storageWrite16(currentPreset.continuousParameters[cp]);

        // to avoid confusion, write zero to the arp/seq clock slot
        storageWrite16(0);

		for(sp=spModwheelTarget;sp<=spVibTarget;++sp)
			storageWrite8(currentPreset.steppedParameters[sp]);

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			storageWrite8(currentPreset.voicePattern[i]);


        for (i=0; i<TUNER_NOTE_COUNT; i++)
			storageWrite16(currentPreset.perNoteTuning[i]);
			
		// v8
		
		storageWrite8(currentPreset.steppedParameters[spPWMBug]);
		storageWrite16(currentPreset.continuousParameters[cpSpread]);
		storageWrite16(currentPreset.continuousParameters[cpExternal]);
        storageWrite8(currentPreset.steppedParameters[spEnvRouting]);
        storageWrite8(currentPreset.steppedParameters[spAssign]);
        storageWrite8(currentPreset.steppedParameters[spLFOSync]);

        for (i=0;i < 16; i++)
            storageWrite8(currentPreset.patchName[i]);


		// this must stay last
		storageFinishStore(number,1); // yes, one page is enough
	}
}

LOWERCODESIZE int8_t storage_loadSequencer(int8_t track, uint8_t * data, uint8_t size)
{
	BLOCK_INT
	{
		if (!storageLoad(SEQUENCER_START_PAGE+track,1))
			return 0;
		
		while(size--)
			*data++=storageRead8();
	}
	
	return 1;
}

LOWERCODESIZE void storage_saveSequencer(int8_t track, uint8_t * data, uint8_t size)
{
	BLOCK_INT
	{
		storagePrepareStore();

		while(size--)
			storageWrite8(*data++);
		
		// this must stay last
		storageFinishStore(SEQUENCER_START_PAGE+track,1);
	}
}

LOWERCODESIZE void storage_export(uint16_t number, uint8_t * buf, int16_t * loadedSize)
{
    // this function can only export from storage, therefore a patch needs to be stored first before exporting
    // the function loads from storage into the buf, truncates all unwanted data from the end and ads the number at the beginning
	int16_t actualSize;

	BLOCK_INT
	{
		storageLoad(number,1); // load one page at position of the patch number

		// don't export trailing zeroes		
		
		actualSize=STORAGE_PAGE_SIZE;
		while(actualSize>0 && storage.buffer[actualSize-1]==0)
			--actualSize;
		
		buf[0]=number;		
		memcpy(&buf[1],storage.buffer,actualSize);
		*loadedSize=actualSize+1;
	}
}

LOWERCODESIZE void storage_import(uint16_t number, uint8_t * buf, int16_t size)
{
	BLOCK_INT
	{
        memset(storage.buffer,0,sizeof(storage.buffer));
		memcpy(storage.buffer,buf,size);
        // here we distinguish between MIDI to storage an MIDI to controls
        if (ui.isInPatchManagement)
        {
            //  check the STORAGE_MAGIC
            storage.bufPtr=storage.buffer;
            if(storageRead32()!=STORAGE_MAGIC)
            {
                memset(storage.buffer,0,sizeof(storage.buffer));
                return;
            }
            storage.bufPtr=storage.buffer+size;
            storageFinishStore(number,1);
            // update the current selected preset
            if (settings.presetMode && settings.presetNumber == number) refreshPresetMode();
        }
        else if (settings.presetMode)
        {
            // load into the current present
            preset_loadCurrent(0,1);
            ui.presetModified=1;
            resetPickUps();
        }
	}

    ui.presetAwaitingNumber=-1;
    if (ui.isInPatchManagement) ui.digitInput=diStoreDecadeDigit;
    sevenSeg_setNumber(number);
}

LOWERCODESIZE void preset_loadDefault(int8_t makeSound)
{
	uint8_t i;
    
	BLOCK_INT
	{
		memset(&currentPreset,0,sizeof(currentPreset));

		currentPreset.continuousParameters[cpAPW]=HALF_RANGE;
		currentPreset.continuousParameters[cpBPW]=HALF_RANGE;
		currentPreset.continuousParameters[cpCutoff]=UINT16_MAX;
		currentPreset.continuousParameters[cpPModFilEnv]=HALF_RANGE;
		currentPreset.continuousParameters[cpFreqBFine]=HALF_RANGE;
		currentPreset.continuousParameters[cpFilEnvAmt]=HALF_RANGE;
		currentPreset.continuousParameters[cpFreqBFine]=HALF_RANGE;
		currentPreset.continuousParameters[cpLFOFreq]=HALF_RANGE;
		currentPreset.continuousParameters[cpAmpSus]=UINT16_MAX;
		currentPreset.continuousParameters[cpAmpVelocity]=HALF_RANGE;
		currentPreset.continuousParameters[cpVibFreq]=HALF_RANGE;
		if (settings.panelLayout==1) currentPreset.continuousParameters[cpDrive]=HALF_RANGE;
        if (settings.panelLayout==0) currentPreset.continuousParameters[cpMixVolA]=UINT16_MAX;

		currentPreset.steppedParameters[spBenderSemitones]=5;
		currentPreset.steppedParameters[spModWheelRange]=1;
		currentPreset.steppedParameters[spBenderTarget]=modAB;
		currentPreset.steppedParameters[spChromaticPitch]=2; // octave
        currentPreset.continuousParameters[cpSeqArpClock]=settings.seqArpClock;

		memset(currentPreset.voicePattern,ASSIGNER_NO_NOTE,sizeof(currentPreset.voicePattern));

		// Default tuning is equal tempered
		for (i=0; i<TUNER_NOTE_COUNT; i++)
			currentPreset.perNoteTuning[i] = i * TUNING_UNITS_PER_SEMITONE;

		if(makeSound)
			currentPreset.steppedParameters[spASaw]=1;

        storage.version=STORAGE_VERSION;

        resetPickUps();

    }
}

LOWERCODESIZE void settings_loadDefault(void)
{
	BLOCK_INT
	{
		memset(&settings,0,sizeof(settings));
		
		settings.benderMiddle=HALF_RANGE;
		settings.midiReceiveChannel=-1; // OMNI
		settings.voiceMask=0x3f;
		settings.seqArpClock=HALF_RANGE;
        settings.panelLayout=0; // GliGli layout
		
		tuner_init(); // use theoretical tuning
	}
}
