////////////////////////////////////////////////////////////////////////////////
// Presets and settings storage, relies on low level page storage system
////////////////////////////////////////////////////////////////////////////////

#include "storage.h"
#include "uart_6850.h"
#include "synth.h"
#include "ui.h"
#include "lfo.h" // LFO_SHAPE_COUNT
#include "math.h"
#include "midi.h"
#include "display.h"

// increment this each time the binary format is changed
#define STORAGE_VERSION 16 // 16: added the vibrato shape and vibrato range (spVibShape, spVibRange) after the poly-mod envelope target at the end of the preset (the settings are unchanged). 15: added the poly-mod envelope target (spPolyModEnv) after the ensemble vibrato at the end of the preset (the settings are unchanged). 14: added the boot light show setting at the end of the settings (the presets are unchanged). 13: added the ensemble vibrato setting after the ratchet probability. 12: added the arp ratchet and ratchet probability after the resolution. 11: added the arp resolution after the swing. 10: added the arp probability and swing after the Euclidean rhythm. 9: added the arp Euclidean rhythm (spArpEuclid) at the end of the preset

#define STORAGE_MAGIC 0x006116a5

#define SETTINGS_PAGE_COUNT 2 // due to the tuning data the settings are too large for one page, with version 8: 305 bytes (one page has 256 bytes)
#define SETTINGS_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-4)

#define STORAGE_MAX_SIZE (SETTINGS_PAGE_COUNT*STORAGE_PAGE_SIZE) // this is the buffer size, which must at least hold the settings data (see above)

// Number of values that the MIDI stepped CCs (CC 48 + parameter number) select for each stepped parameter: a CC value 0..127 is scaled to 0..range-1 (see midi.c).
// Every entry is given by the NAME of its parameter. (Until now this table was positional, and from the unison on its entries were one place off: the unison got the
// range of the assigner priority (3, so CC could select an assigner mode that is not a unison), the priority got the range of the bender semitones, the PWM bug got no range at all, and so on.)
// A CC number is only usable when the coarse CC range (CC 16..50) and the fine CC range (CC 80..114) do not cover it, so e.g. the parameters from 32 up (CC 80...) are
// shadowed and have a range here only for completeness; the vibrato shape and range have their own CC numbers (MIDI_CC_VIB_SHAPE and MIDI_CC_VIB_RANGE in midi.c).
const uint8_t steppedParameterRange[spCount] =
{
    [spASaw] = 2, [spATri] = 2, [spASqr] = 2,
    [spBSaw] = 2, [spBTri] = 2, [spBSqr] = 2,
    [spSync] = 2, [spPModFA] = 2, [spPModFil] = 2,
    [spLFOShape] = LFO_SHAPE_COUNT, // all 12 shapes (CC 57), it was 3
    [10] = 0, // unused slot
    [spLFOTargets] = 64, // 6 bits of modulationTarget_t, it was 4
    [spTrackingShift] = 4, // off, half, full, both
    [spFilEnvShape] = 2, [spFilEnvSlow] = 2, [spAmpEnvShape] = 2,
    [holdPedal] = 2, // this slot is the amp envelope fast/slow flag
    [spUnison] = 2,
    [spAssignerPriority] = 3, // last, low, high
    [spBenderSemitones] = 13, // 0..12 semitones
    [spBenderTarget] = modPW+1, // off, ab, vcf, vca, b, pw (CC 68)
    [spModWheelRange] = 4,
    [spChromaticPitch] = 7,
    [spModwheelTarget] = 2,
    [spVibTarget] = VIB_TARGETS, // all 8 vibrato targets (CC 72)
    [spAmpEnvSlow] = 2,
    [spPWMBug] = 2,
    [spAssign] = 2, // first, cycle
    [spEnvRouting] = 4,
    [spLFOSync] = 8,
    [spArpEuclid] = ARP_EUCLID_PATTERNS,
    [spArpProbability] = ARP_PROBABILITY_CHOICES, // (CC 79)
    [spArpSwing] = ARP_SWING_CHOICES, // (shadowed by the fine CC range, so no CC)
    [spArpResolution] = ARP_RESOLUTION_CHOICES, // (no CC either)
    [spArpRatchet] = ARP_RATCHET_CHOICES, // (no CC either)
    [spArpRatchetProb] = ARP_RATCHET_PROB_CHOICES, // (no CC either)
    [spEnsemble] = ENSEMBLE_CHOICES, // (no CC either)
    [spPolyModEnv] = POLYMOD_ENV_TARGETS, // (no CC either)
    [spVibShape] = LFO_SHAPE_COUNT, // CC MIDI_CC_VIB_SHAPE
    [spVibRange] = 2, // CC MIDI_CC_VIB_RANGE
};

// Highest valid value of every stepped parameter (inclusive), used to sanitize a preset when it is loaded.
// Presets come from F-RAM and from sysex dumps, so the data can not be trusted: a damaged value could index past the end of a
// table (e.g. spModWheelRange selects an entry of a 4 entry table) or select a mode that does not exist.
// A value above its limit is replaced by the default (see validStepped). Each limit is the largest value that the menu,
// the panel buttons or the preset loader itself can produce, so valid presets are never changed.
static const uint8_t steppedParameterMax[spCount] =
{
    [spASaw]=1,[spATri]=1,[spASqr]=1,[spBSaw]=1,[spBTri]=1,[spBSqr]=1,[spSync]=1,[spPModFA]=1,[spPModFil]=1, // panel switches
    [spLFOShape]=LFO_SHAPE_COUNT-1, // 6 pairs of shapes (the menu), times 2 for the shape button (bit 0)
    [10]=0, // unused slot (legacy), never stored
    [spLFOTargets]=63, // bit mask of modulationTarget_t, 6 bits
    [spTrackingShift]=3, // bit 0 = half tracking, bit 1 = full tracking (both buttons pressed gives 3)
    [spFilEnvShape]=1,[spFilEnvSlow]=1,[spAmpEnvShape]=1,
    [holdPedal]=1, // this slot is stored as the amp envelope fast/slow flag
    [spUnison]=1,
    [spAssignerPriority]=2, // last, low, high
    [spBenderSemitones]=12, // the number of semitones (the menu stores 2, 4, 7 or 12, the default is 5)
    [spBenderTarget]=modPW, // modulation_t: off, ab, vcf, vca, b, pw (the pulse width of oscillator A)
    [spModWheelRange]=3, // index into a 4 entry table
    [spChromaticPitch]=6, // 7 modes
    [spModwheelTarget]=1,
    [spVibTarget]=VIB_TARGETS-1, // VCO, VCA, VCO A, VCO B, noise level, resonance, detune, poly-mod amount
    [spAmpEnvSlow]=1, // never stored directly, copied from the holdPedal slot
    [spPWMBug]=1,
    [spAssign]=1, // first, cycle
    [spEnvRouting]=3,
    [spLFOSync]=7, // 8 choices in the menu
    [spArpEuclid]=ARP_EUCLID_PATTERNS-1,
    [spArpProbability]=ARP_PROBABILITY_CHOICES-1,
    [spArpSwing]=ARP_SWING_CHOICES-1,
    [spArpResolution]=ARP_RESOLUTION_CHOICES-1, // 4 resolutions
    [spArpRatchet]=ARP_RATCHET_CHOICES-1, // off, 2, 3, 4
    [spArpRatchetProb]=ARP_RATCHET_PROB_CHOICES-1, // 8 probabilities
    [spEnsemble]=ENSEMBLE_CHOICES-1, // off, small, wide
    [spPolyModEnv]=POLYMOD_ENV_TARGETS-1, // 6 targets
    [spVibShape]=LFO_SHAPE_COUNT-1, // any LFO shape
    [spVibRange]=1, // normal, low
};

// returns the value that was read from storage if it is valid for this stepped parameter, otherwise the parameter keeps its
// current value, which is the default because preset_loadDefault ran at the start of preset_loadCurrent
static uint8_t validStepped(steppedParameter_t sp, uint8_t value)
{
    return (value>steppedParameterMax[sp])?currentPreset.steppedParameters[sp]:value;
}

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

////////////////////////////////////////////////////////////////////////////////
// V2.5: presets of the Teensy firmware V2.20..V2.34 (JS/JHHL/JAM, "V2.34 format")
////////////////////////////////////////////////////////////////////////////////
// V2.34 and this firmware share the preset layout up to the end of the per-note tuning (offset 120, the GliGli 2.1 "version 7" layout),
// and both use the same magic number. After that they differ, and their version numbers 8..12 mean different things:
//   V2.34 (versions 8..12): offset 120: continuous parameters 25..30 (mod delay, vib speed, vib amount, detune, arp clock, noise level),
//                          i.e. 25..29 a SECOND time; version 11 adds vib shape, vib range, vib targets (offset 132);
//                          version 12 writes these three AGAIN plus the poly-mod envelope target (offset 135). Version 12 ends at 139.
//   this firmware (8..16): offset 120: PW bug, spread, external, env routing, assign, LFO sync, then the 16 byte patch name, then the arp etc.
// A V2.34 preset is recognised by the data it repeats, and by zeros after its end (where this firmware keeps the patch name and more).
// Only a preset of this firmware's format whose mod delay, vibrato speed/amount, detune, PW bug, spread, external level, routing,
// assign mode, LFO sync and patch name are all zero could look like a V2.34 one.

#define V234_TAIL 120 // where the V2.34 data after the per-note tuning starts (magic 4, version 1, 25 cont. 50, 23 stepped, 5 cont. 10, 2 stepped, voice pattern 6, tuning 24)

static int8_t isV234Preset(void)
{
	uint8_t v=storage.buffer[4]; // the version byte, right after the magic number
	uint8_t i,end;

	if (v<8 || v>12)
		return 0;

	end=(v>=12)?139:((v>=11)?135:132); // the end of the V2.34 data for this version

	// V2.34 writes the continuous parameters 25..29 twice: at offset 78 (version 2 part) and at offset 120 (version 8 part)
	if (memcmp(&storage.buffer[78],&storage.buffer[V234_TAIL],10))
		return 0;

	// version 12 writes the vibrato shape, range and targets twice (offset 132 and 135)
	if (v>=12 && memcmp(&storage.buffer[132],&storage.buffer[135],3))
		return 0;

	// nothing after the V2.34 data (the buffer is cleared beyond the received/stored bytes)
	for (i=end;i<160;++i)
		if (storage.buffer[i])
			return 0;

	return 1;
}

// Reads the V2.34 data after the per-note tuning (storage.bufPtr is at offset V234_TAIL) and translates the stepped parameters whose
// numbers differ between V2.34 and this firmware. The common part was loaded as a version 7 (GliGli 2.1) preset, which also rescales
// the LFO speed and amount, the vibrato speed and amount and the envelope times exactly as for GliGli presets (V2.34 kept GliGli's curves).
static void loadV234Tail(uint8_t v)
{
	// V2.34 vibrato target order: ab, a, b, vca, noise, reso, detune, pmod-vcob. This firmware: VCO (ab), VCA, VCO A, VCO B, noise, reso, detune, pm amt
	static const uint8_t vibTargetMap[8]={0,2,3,1,4,5,6,7};
	uint8_t raw,shape,range,target;

	// the stepped parameters of the common part, read raw (validStepped already replaced the V2.34 values that are out of range here)
	raw=storage.buffer[5+50+spBenderTarget]; // V2.34: off, vco, vcf, vca, pw, vco-b. Here: off, ab, vcf, vca, b, pw
	currentPreset.steppedParameters[spBenderTarget]=(raw==4)?modPW:((raw==5)?modB:((raw<=3)?raw:modAB));

	raw=storage.buffer[5+50+spModWheelRange]; // V2.34 stores the shift itself (menu: min 5, low 3, high 1, full 0), here the index of {5,3,1,0}
	currentPreset.steppedParameters[spModWheelRange]=(raw>=5)?0:((raw>=3)?1:((raw>=1)?2:3));

	raw=storage.buffer[5+50+spChromaticPitch]; // V2.34: free, semi, oct (3, only reachable by CC, works like oct there)
	currentPreset.steppedParameters[spChromaticPitch]=(raw>2)?2:raw;

	currentPreset.steppedParameters[spPWMBug]=0; // V2.34 sets PW A to max and PW B to min when the square is off (the "fixed" behaviour, pulse bug off)

	// version 8 part: continuous parameters 25..29 again (same values, skip them), then the noise level (30)
	storage.bufPtr+=10;
	// V2.34 sends noise/3 to the external/noise CV, this firmware 0.4*external, so the same voltage is external = noise*5/6
	currentPreset.continuousParameters[cpExternal]=(uint16_t)(((uint32_t)storageRead16()*5)/6);

	// vibrato: presets made before V2.25 (version 8..10) have no vibrato shape, range or targets, they keep the defaults of this firmware
	// (triangle, normal range, VCO), which is the vibrato that GliGli's firmware had (V2.34 itself would play them with pulse / low range)
	shape=VIB_SHAPE_DEFAULT;
	range=1; // V2.34 range: 0 = low (8 times slower), 1 = high (normal), 2,3 (CC only) = faster
	target=0;

	if (v>=11)
	{
		shape=storageRead8();
		range=storageRead8();
		target=storageRead8();
	}

	if (v>=12)
	{
		// the same three again, then the poly-mod envelope target: vco-a, vco-b, vco, pw-a, pw-b (same numbers here, this firmware adds pw = 5)
		shape=storageRead8();
		range=storageRead8();
		target=storageRead8();
		currentPreset.steppedParameters[spPolyModEnv]=validStepped(spPolyModEnv,storageRead8());
	}

	currentPreset.steppedParameters[spVibShape]=validStepped(spVibShape,shape); // the 12 shapes have the same numbers in V2.34
	currentPreset.steppedParameters[spVibRange]=(range==0)?1:0; // here: 0 = normal, 1 = low
	currentPreset.steppedParameters[spVibTarget]=vibTargetMap[target&7];
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
		settings.bootLightShow=1; // default is: light show on (this is also what settings saved by older versions get)

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

		if (storage.version<14)
			return 1; // older data has no light show field, the default set above stays

		// v14

		settings.bootLightShow=storageRead8();
		settings.bootLightShow=(settings.bootLightShow>1)?1:settings.bootLightShow; // anything but 0 or 1 is invalid, use the default (on)

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

		// v14

		storageWrite8(settings.bootLightShow);

		// this must stay last
		storageFinishStore(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);
	}
}

// Flat - Resets the settings to default and re-tune the synth, which resaves the settings
void settings_reset(void)
{
	settings.presetNumber=1; // if no other info, set selected preset to 1
	settings.presetMode=0; // if no info, default start up is in live mode
	settings.seqArpClock=HALF_RANGE;
	settings.benderMiddle=HALF_RANGE;
	settings.midiReceiveChannel=-1; // default is 'OMNI'
	settings.voiceMask=0x3f; // default is: all on
	settings.midiSendChannel=0; // default is: 1
	settings.syncMode=smInternal; // default is internal clock
	settings.vcfLimit=0; // default is: no limit on the VCF
	settings.midiMode=0; // normal mode
	settings.panelLayout=0; // GliGli layout
	settings.bootLightShow=1; // light show on

	tuner_tuneSynth();
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
	uint8_t v234Version; // V2.5: the storage version of a V2.34 format preset, 0 for this firmware's own format
	
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

        // V2.5: a preset of V2.20..V2.34 (the Teensy firmware before V2.5, from flash or from a sysex dump): load its common part as a
        // GliGli 2.1 (version 7) preset, the rest is read by loadV234Tail() after the per-note tuning
        v234Version=isV234Preset()?storage.version:0;
        if (v234Version)
            storage.version=7;

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
			currentPreset.steppedParameters[sp]=validStepped(sp,storageRead8()); // never trust stored data

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
			currentPreset.steppedParameters[sp]=validStepped(sp,storageRead8()); // never trust stored data

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
			currentPreset.steppedParameters[sp]=validStepped(sp,storageRead8()); // never trust stored data

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
		{
			currentPreset.voicePattern[i]=storageRead8();
			// a note offset is at most 127. Anything else except the end marker is damaged data: treat it as the end of the pattern
			if(currentPreset.voicePattern[i]>127 && currentPreset.voicePattern[i]!=ASSIGNER_NO_NOTE)
				currentPreset.voicePattern[i]=ASSIGNER_NO_NOTE;
		}

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
			
		// V2.5: the rest of a V2.34 preset
		if (v234Version)
		{
			loadV234Tail(v234Version);
			return 1;
		}

		if (storage.version<8)
        {
			return 1;
        }
	
		// V8

		currentPreset.steppedParameters[spPWMBug]=validStepped(spPWMBug,storageRead8()); // invalid values: the default stays

        currentPreset.continuousParameters[cpSpread]=storageRead16();
        currentPreset.continuousParameters[cpExternal]=storageRead16();

        currentPreset.steppedParameters[spEnvRouting]=validStepped(spEnvRouting,storageRead8()); // (was a signed comparison, which let negative values through)

        currentPreset.steppedParameters[spAssign]=validStepped(spAssign,storageRead8());

		currentPreset.steppedParameters[spLFOSync]=validStepped(spLFOSync,storageRead8()); // LFO sync: 8 choices in the menu, a larger value is invalid and falls back to off

        for (i=0;i < 16; i++)
            currentPreset.patchName[i]=storageRead8();

		// presets saved by older versions end here: the arp Euclidean rhythm keeps its default (0 = off)
		if (storage.version<9)
			return 1;

		// V9

		currentPreset.steppedParameters[spArpEuclid]=validStepped(spArpEuclid,storageRead8()); // ignore out of range values, e.g. from corrupted data

		// presets saved by version 9 end here: the arp probability and swing keep their defaults (0 = off)
		if (storage.version<10)
			return 1;

		// V10

		currentPreset.steppedParameters[spArpProbability]=validStepped(spArpProbability,storageRead8()); // invalid values: the default stays
		currentPreset.steppedParameters[spArpSwing]=validStepped(spArpSwing,storageRead8());

		// presets saved by version 10 end here: the arp resolution keeps its default (0 = 1/16)
		if (storage.version<11)
			return 1;

		// V11

		currentPreset.steppedParameters[spArpResolution]=validStepped(spArpResolution,storageRead8()); // invalid values: the default stays

		// presets saved by version 11 end here: the arp ratchet and its probability keep their defaults (0 = off, 100%)
		if (storage.version<12)
			return 1;

		// V12

		currentPreset.steppedParameters[spArpRatchet]=validStepped(spArpRatchet,storageRead8()); // invalid values: the default stays
		currentPreset.steppedParameters[spArpRatchetProb]=validStepped(spArpRatchetProb,storageRead8());

		// presets saved by version 12 end here: the ensemble vibrato keeps its default (0 = off)
		if (storage.version<13)
			return 1;

		// V13

		currentPreset.steppedParameters[spEnsemble]=validStepped(spEnsemble,storageRead8()); // invalid values: the default stays

		// presets saved by version 13 and 14 end here: the poly-mod envelope target keeps its default (0 = VCO A, the behaviour before this setting existed)
		if (storage.version<15)
			return 1;

		// V15

		currentPreset.steppedParameters[spPolyModEnv]=validStepped(spPolyModEnv,storageRead8()); // invalid values: the default stays

		// presets saved by version 15 end here: the vibrato shape and range keep their defaults (triangle, normal range = the vibrato as it was)
		if (storage.version<16)
			return 1;

		// V16

		currentPreset.steppedParameters[spVibShape]=validStepped(spVibShape,storageRead8()); // invalid values: the default stays
		currentPreset.steppedParameters[spVibRange]=validStepped(spVibRange,storageRead8());

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

		// v9: the arp Euclidean rhythm goes last, so older firmware (which stops reading after the patch name) can still load the preset

		storageWrite8(currentPreset.steppedParameters[spArpEuclid]);

		// v10: the arp probability and swing follow the Euclidean rhythm; still after the patch name, so older firmware can load the preset

		storageWrite8(currentPreset.steppedParameters[spArpProbability]);
		storageWrite8(currentPreset.steppedParameters[spArpSwing]);

		// v11: the arp resolution follows the swing, still after the patch name so older firmware can load the preset

		storageWrite8(currentPreset.steppedParameters[spArpResolution]);

		// v12: the arp ratchet and its probability follow the resolution, still after the patch name so older firmware can load the preset

		storageWrite8(currentPreset.steppedParameters[spArpRatchet]);
		storageWrite8(currentPreset.steppedParameters[spArpRatchetProb]);

		// v13: the ensemble vibrato follows the ratchet probability, still after the patch name so older firmware can load the preset

		storageWrite8(currentPreset.steppedParameters[spEnsemble]);

		// v15: the poly-mod envelope target follows the ensemble vibrato, still after the patch name so older firmware can load the preset

		storageWrite8(currentPreset.steppedParameters[spPolyModEnv]);

		// v16: the vibrato shape and range follow the poly-mod envelope target, still after the patch name so older firmware can load the preset

		storageWrite8(currentPreset.steppedParameters[spVibShape]);
		storageWrite8(currentPreset.steppedParameters[spVibRange]);


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
	// V2.5: a patch dump is one page at most (a longer message would have overrun storage.buffer)
	if (size>(int16_t)STORAGE_PAGE_SIZE)
		size=STORAGE_PAGE_SIZE;
	if (size<0)
		size=0;

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
            // V2.5: only the preset pages 0..99. On the Teensy the pages above 99 are the sequencer, the manual preset and the settings.
            if (number>99)
            {
                memset(storage.buffer,0,sizeof(storage.buffer));
                return;
            }
            // V2.5: store the patch in this firmware's format: load it like a preset (which converts V2.34 presets and brings older formats
            // up to date) and save it again. The page then always has the current version, so presets in flash with versions 8..12 are only
            // ever V2.34 ones. currentPreset is kept in tempBuffer meanwhile (the sysex is no longer needed there, it was copied above).
            _Static_assert(sizeof(currentPreset)<=TEMP_BUFFER_SIZE,"tempBuffer too small to keep the current preset");
            memcpy(tempBuffer,&currentPreset,sizeof(currentPreset));
            preset_loadCurrent(number,1);
            preset_saveCurrent(number);
            memcpy(&currentPreset,tempBuffer,sizeof(currentPreset));
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
		currentPreset.steppedParameters[spVibShape]=VIB_SHAPE_DEFAULT; // the vibrato is a triangle (a stored preset that has a vibrato shape overwrites this when it is loaded)
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
		settings.bootLightShow=1; // light show on
		
		tuner_init(); // use theoretical tuning
	}
}
