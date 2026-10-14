////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <avr/pgmspace.h>
#include <string.h>

#include "synth.h"

#include "scanner.h"
#include "display.h"
#include "sh.h"
#include "potmux.h"
#include "adsr.h"
#include "lfo.h"
#include "tuner.h"
#include "assigner.h"
#include "arp.h"
#include "storage.h"
#include "uart_6850.h"
#include "import.h"
#include "ui.h"
#include "midi.h"
#include "../xnormidi/midi.h"
#include "seq.h"
#include "clock.h"
#include "utils.h"

#define POT_DEAD_ZONE 512

// Dead band is distance from center of pot to end of dead band area,
// in either direction.
#define BEND_DEADBAND 3500  // in bits
#define BEND_GUARDBAND 400 // in value
#define PANEL_DEADBAND 2048 // in bits

// The P600 VCA completely closes before the CV reaches 0, this accounts for it
#define VCA_DEADBAND 771 // GliGli = 768

#define BIT_INTPUT_FOOTSWITCH 0x20
#define BIT_INTPUT_TAPE_IN 0x01

uint8_t tempBuffer[TEMP_BUFFER_SIZE]; // general purpose chunk of RAM

const p600Pot_t continuousParameterToPot[cpCount]=
{
    ppFreqA,ppNone,ppAPW,
    ppFreqB,ppNone,ppBPW,ppFreqBFine,
    ppCutoff,ppResonance,ppFilEnvAmt,
    ppFilRel,ppFilSus,ppFilDec,ppFilAtt,
    ppAmpRel,ppAmpSus,ppAmpDec,ppAmpAtt,
    ppPModFilEnv,ppPModOscB,
    ppLFOFreq,ppLFOAmt,
    ppNone,ppNone,ppNone,ppNone,
    ppNone,ppNone,ppNone,ppNone,ppNone,ppNone,
    ppMixer,ppGlide, // the two cp's are auxiliary, e.g. not stored, just intermediate
    ppNone // cpDrive (not stored)
};

const uint8_t potToCP[32]=
{
    cpMixVolA, cpCutoff,cpResonance,cpFilEnvAmt,cpFilRel,cpFilSus,
	cpFilDec,cpFilAtt,cpAmpRel,cpAmpSus,cpAmpDec,cpAmpAtt,
	cpGlideVolB,cpBPW,
    -1, -1, // ppMVol, ppMTune // not part of preset
    -1, -1, -1, -1, -1, -1, // ppPitchWheel,,,,,, // not part of preset, gap in index (5)
    -1, // ppModWheel, // not part of preset
	-1, // speed dial
    cpAPW,cpPModFilEnv,cpLFOFreq,cpPModOscB,cpLFOAmt,cpFreqB,cpFreqA,cpFreqBFine
};

const uint16_t extClockDividers[16] = {192,168,144,128,96,72,48,36,24,18,12,9,6,4,3,2};

volatile uint32_t currentTick=0; // 500hz

struct synth_s
{
    struct adsr_s filEnvs[SYNTH_VOICE_COUNT];
    struct adsr_s ampEnvs[SYNTH_VOICE_COUNT];

    struct lfo_s lfo,vibrato;

    // store slowly and on event changing partial results so that specific updates can be made faster
    int32_t tunedBenderCVs[pcFil6-pcOsc1A+1];
    int32_t tunedOctaveCVs[pcOsc6B-pcOsc1A+1];
    uint16_t oscABaseCV[SYNTH_VOICE_COUNT];
    uint16_t oscBBaseCV[SYNTH_VOICE_COUNT];
    uint16_t filterBaseCV[SYNTH_VOICE_COUNT];

    uint16_t oscANoteCV[SYNTH_VOICE_COUNT];
    uint16_t oscBNoteCV[SYNTH_VOICE_COUNT];
    uint16_t filterNoteCV[SYNTH_VOICE_COUNT];

    uint16_t oscATargetCV[SYNTH_VOICE_COUNT];
    uint16_t oscBTargetCV[SYNTH_VOICE_COUNT];
    uint16_t filterTargetCV[SYNTH_VOICE_COUNT];

    uint16_t filterMaxCV[SYNTH_VOICE_COUNT];

    uint16_t modwheelAmount; // what the LFO / vibrato use: mod wheel and channel aftertouch added together (saturating)
    // aftertouch. Channel pressure is mapped exactly like the mod wheel (same range and target) and added to it.
    uint16_t mwMapped; // mod wheel after the mapping
    uint16_t atRaw; // channel pressure on the same 0..65535 scale as the mod wheel (MIDI value<<9)
    uint16_t atMapped; // atRaw after the mod wheel mapping, for the setting in atMapKey
    uint8_t atMapKey; // see modMapKey()
    // Poly aftertouch drives the filter cutoff of the voice that plays the note
    uint16_t polyATCV[SYNTH_VOICE_COUNT]; // filter CV offset per voice, added in refreshVoice (0 = no pressure)
    uint16_t filOctaveCV[SYNTH_VOICE_COUNT]; // filter CV span of one octave per voice (from the tuning), the unit for poly aftertouch depth
    int16_t benderAmountInternal;
    int16_t benderAmountExternal;
    uint16_t masterVolume;
    int16_t benderCVs[pcFil6-pcOsc1A+1];
    int16_t benderVolumeCV;
    int16_t benderPW; // the bender as pulse width of oscillator A (bender target pw), added to the pulse width CV in refreshPulseWidth

    uint16_t dlyAmt;

    uint16_t lfoAmt, vibAmt, vibAmp, vibPitch;

    // ensemble vibrato (see ensembleUpdate): only used while ensembleActive is set, otherwise every voice uses the single vibrato LFO
    uint32_t ensPhase[SYNTH_VOICE_COUNT]; // vibrato phase of every voice, a whole cycle is 2^32
    uint32_t ensStep; // phase offset between a voice and the next one, also the phase a voice restarts with (voice number times this)
    int16_t ensPitchA[SYNTH_VOICE_COUNT]; // how much the pitch of oscillator A of a voice differs from the single vibrato LFO (added to the pitch)
    int16_t ensPitchB[SYNTH_VOICE_COUNT]; // the same for oscillator B
    int8_t ensembleActive; // 1 while the voices have their own vibrato phases

    int16_t glideAmount;
    int8_t gliding;

    uint32_t modulationDelayStart;
    uint16_t modulationDelayTickCount;

    uint8_t pendingExtClock;

    int8_t transpose;

    int8_t clockBar;

    uint8_t freqDial;

    // uint32_t updateCounter; // for performance measurement

} synth;

extern void refreshAllPresetButtons(void);
extern const uint16_t attackCurveLookup[] PROGMEM; // for modulation delay (lives in flash)

struct deadband {
    uint16_t middle;
    uint16_t guard;
    uint16_t deadband;
    uint16_t rise; // the rise at the edge of the deadband (zero is strictly "dead")
    uint32_t precalcLow;
    uint32_t precalcHigh;
    uint32_t precalcRise;
};

struct deadband bendDeadband = { HALF_RANGE, BEND_GUARDBAND,  BEND_DEADBAND };
struct deadband panelDeadband = { HALF_RANGE, 0, PANEL_DEADBAND };
struct deadband freqFineDeadband = { HALF_RANGE-1000, 0, 8192, 1024}; // ultra "slow band" in the middle of fine tune

static void addWheelToTunedCVs(void) // this function is specific for the case in which there are only wheel changes. Could also be moved to inside wheel event...
{
    uint16_t cva,cvb,cvf;
    int16_t mTune,fineBFreq;
    static uint16_t mTuneRaw,fineBFreqRaw;
    static uint8_t track;
    int8_t v;

    track=currentPreset.steppedParameters[spTrackingShift];
    mTuneRaw=potmux_getValue(ppMTune);
    mTune=(mTuneRaw>>7)+INT8_MIN*2;
    fineBFreqRaw=currentPreset.continuousParameters[cpFreqBFine];
    fineBFreq=(fineBFreqRaw>>7)+INT8_MIN*2;

    // compute shifs for oscs & filters

    for(v=0; v<SYNTH_VOICE_COUNT; ++v)
    {

        // bender and tune

        cva=satAddU16S32(synth.oscABaseCV[v],(int32_t)synth.benderCVs[pcOsc1A+v]+mTune);
        cvb=satAddU16S32(synth.oscBBaseCV[v],(int32_t)synth.benderCVs[pcOsc1B+v]+mTune+fineBFreq);
        cvf=satAddU16S16(synth.filterBaseCV[v],synth.benderCVs[pcFil1+v]);


        // glide

        synth.oscATargetCV[v]=cva;
        synth.oscBTargetCV[v]=cvb;
        synth.filterTargetCV[v]=cvf;
        if(synth.gliding)
        {
            if(!track)
                synth.filterNoteCV[v]=cvf; // no glide if no tracking for filter
        }
        else
        {
            synth.oscANoteCV[v]=cva;
            synth.oscBNoteCV[v]=cvb;
            synth.filterNoteCV[v]=cvf;
        }

    }
}


static void computeTunedOffsetCVs(void) // this function must always be called after tuning or after the semitones setting of the bender changes (inkl. preset change)
{
    p600CV_t cv;
    for(cv=pcOsc1A; cv<=pcFil6; ++cv)
    {
        synth.tunedBenderCVs[cv]=tuner_computeCVFromNote(currentPreset.steppedParameters[spBenderSemitones]*4,0,cv)-tuner_computeCVFromNote(0,0,cv);
        if (cv<=pcOsc6B) synth.tunedOctaveCVs[cv]=(uint16_t)((tuner_computeCVFromNote(48,0,cv)-tuner_computeCVFromNote(0,0,cv))*2.5f);
        // one octave of filter CV (notes are semitones, 12 of them are an octave) = the unit for the depth of poly aftertouch
        if (cv>=pcFil1) synth.filOctaveCV[cv-pcFil1]=tuner_computeCVFromNote(12,0,cv)-tuner_computeCVFromNote(0,0,cv);
    }
}


static void computeTunedCVs(int8_t force, int8_t forceVoice)
{

    // with this function the following changes are reflected in the CVs
    // master Tune, fine Tune OSC B
    // filter cut-off, frequency settings of OSC A and B (inkl. mode setting)
    // Tracking setting
    // Unison / detune
    // Bender
    // assigned notes
    //
    // --> all these are slowly changing, therefore the function can be called according to resources
    // --> for bender events there is separate, reduced and faster function, because there can be a lot of these events

    uint16_t cva,cvb,cvf;
    uint8_t note=SCANNER_BASE_NOTE,baseCutoffNote;
    int8_t v, v_aux;

    uint16_t baseAPitch,baseBPitch,baseCutoff;
    int16_t mTune,fineBFreq,detune;

    // We use int16_t here because we want to be able to use negative
    // values for intermediate calculations, while still retaining a
    // maximum value of at least UINT8_T.
    int16_t sNote,baseANote,ANote,baseBNote,BNote,trackingNote;

    static uint16_t baseAPitchRaw,baseBPitchRaw,baseCutoffRaw,mTuneRaw,fineBFreqRaw,detuneRaw,spreadRaw;
    static uint8_t track,chrom;

    // detect change & quit if none

    if(!force &&
            mTuneRaw==potmux_getValue(ppMTune) &&
            fineBFreqRaw==currentPreset.continuousParameters[cpFreqBFine] &&
            baseCutoffRaw==currentPreset.continuousParameters[cpCutoff] &&
            baseAPitchRaw==currentPreset.continuousParameters[cpFreqA] &&
            baseBPitchRaw==currentPreset.continuousParameters[cpFreqB] &&
            detuneRaw==currentPreset.continuousParameters[cpUnisonDetune] &&
            track==currentPreset.steppedParameters[spTrackingShift] &&
            chrom==currentPreset.steppedParameters[spChromaticPitch] &&
            spreadRaw==currentPreset.continuousParameters[cpSpread])
    {
        return;
    }

    mTuneRaw=potmux_getValue(ppMTune);
    fineBFreqRaw=currentPreset.continuousParameters[cpFreqBFine];
    baseCutoffRaw=currentPreset.continuousParameters[cpCutoff];
    baseAPitchRaw=currentPreset.continuousParameters[cpFreqA];
    baseBPitchRaw=currentPreset.continuousParameters[cpFreqB];
    detuneRaw=currentPreset.continuousParameters[cpUnisonDetune];
    track=currentPreset.steppedParameters[spTrackingShift];
    chrom=currentPreset.steppedParameters[spChromaticPitch];
    spreadRaw=currentPreset.continuousParameters[cpSpread];

    // compute for oscs & filters

    mTune=(mTuneRaw>>7)+INT8_MIN*2;
    fineBFreq=(fineBFreqRaw>>7)+INT8_MIN*2;
    baseCutoff=((uint32_t)baseCutoffRaw*5)>>3; // 62.5% of raw cutoff
    baseAPitch=baseAPitchRaw>>2;
    baseBPitch=baseBPitchRaw>>2;

    baseCutoffNote=baseCutoff>>8;
    baseANote=baseAPitch>>8; // 64 semitones
    baseBNote=baseBPitch>>8;

    baseCutoff&=0xff;

    synth.freqDial=0;
    if (chrom==0 || chrom==6)
    {
        synth.freqDial=1;
        baseAPitch&=0xff;
    }
    else
    {
        baseAPitch&=0;
    }

    if (chrom==0 || chrom==5)
    {
        synth.freqDial+=4;
        baseBPitch&=0xff;
    }
    else
    {
        baseBPitch&=0;
    }

    if (chrom==2 || chrom==3 || chrom==5)
    {
        baseANote-=baseANote%12;
        synth.freqDial+=2;
    }
    if (chrom==2 || chrom==4 || chrom==6)
    {
        baseBNote-=baseBNote%12;
        synth.freqDial+=8;
    }

    for(v=0; v<SYNTH_VOICE_COUNT; ++v)
    {
        // When force is set to -1 (and forceVoice too), update
        // all voices, whether assigned or not, in order to get a
        // reasonable filter CV after power on. Otherwise some voices
        // in some synths, which have a rather large filter CV
        // feedthrough, output a fairly large 'thump' when the first
        // note is triggered, as the filter CV jumps from almost 0
        // to its preset value. In this case, use the default
        // value of SCANNER_BASE_NOTE for note (see declaration above),
        // as the assigner does not yet have a valid note for the voice.
        if ((forceVoice>=0 && v!=forceVoice) || (!assigner_getAssignment(v,&note) && force!=-1))
            continue;

        // Subtract bottom C, signed result. Here a value of 0
        // is lowest C on kbd, values below that can arrive via MIDI
        sNote=note-SCANNER_BASE_NOTE;

        // oscs

        ANote=baseANote+sNote;
        if (ANote<0)
            ANote=0;
        // We assume we won't get more than UINT8_MAX here, even
        // if the incoming MIDI note is high and baseANote is large too.
        BNote=baseBNote+sNote;
        if (BNote<0)
            BNote=0;

        detune=0; // this is the pitch part of the vintage spread detune
        if (spreadRaw>1000)
        {
            if (spreadRaw<=HALF_RANGE)
            {
                detune=(spreadRaw>>12);
            }
            else
            {
                detune=(spreadRaw>>10)-24;
            }
        }

        v_aux=v;
        synth.oscABaseCV[v]=satAddU16S16(tuner_computeCVFromNote(ANote,baseAPitch,pcOsc1A+v),(1+(v_aux>>1))*(v_aux&1?-1:1)*detune);
        v_aux=(v+3)%6;
        synth.oscBBaseCV[v]=satAddU16S16(tuner_computeCVFromNote(BNote,baseBPitch,pcOsc1B+v),(1+(v_aux>>1))*(v_aux&1?-1:1)*detune);

        // filter

        trackingNote=baseCutoffNote;
        if(track) {
            // We use / instead of >> because sNote is signed. */
            // Using a constant instead of calculated value
            // for the divisor as it gives the compiler a chance to
            // optimize using shift operations.
            // >> is not guaranteed in C to work properly for
            // signed numbers (implementation-specific). */
            trackingNote+=(track==1?sNote/2:sNote);
            // can only be negative if tracking is enabled. */
            if (trackingNote<0)
                trackingNote=0;
        }

        v_aux=(v+5)%6;
        synth.filterBaseCV[v]=satAddU16S16(tuner_computeCVFromNote(trackingNote,baseCutoff,pcFil1+v),(1+(v_aux>>1))*detune);

        // unison detune

        if(currentPreset.steppedParameters[spUnison])
        {
            detune=(1+(v>>1))*(v&1?-1:1)*(detuneRaw>>8);
            // scale detune with tone, e.g. less pronounced at higher frequencies
            synth.oscABaseCV[v]=satAddU16S16(synth.oscABaseCV[v],(int16_t)(detune*(1.0f-(2.0f*(float)synth.oscABaseCV[v])/131072.0f)));
            synth.oscBBaseCV[v]=satAddU16S16(synth.oscBBaseCV[v],(int16_t)(detune*(1.0f-(2.0f*(float)synth.oscBBaseCV[v])/131072.0f)));
            synth.filterBaseCV[v]=satAddU16S16(synth.filterBaseCV[v],(int16_t)(detune*(1.0f-(2.0f*(float)synth.filterBaseCV[v])/131072.0f)));
        }

        // bender and tune

        cva=satAddU16S32(synth.oscABaseCV[v],(int32_t)synth.benderCVs[pcOsc1A+v]+mTune);

        // compute linear shift fineBFreq add-on to the voltage
        // this function should produce a constant frequency difference across the entire scale (but becomes a bad approximation at low frequencies)
        //fineBFreqAdd=(int16_t)((float)fineBFreq*pow(2.0f, (32768.0f-(float)synth.oscBBaseCV[v])/tuner_computeCVPerOct(BNote,v)));
        //cvb=satAddU16S32(synth.oscBBaseCV[v],(int32_t)synth.benderCVs[pcOsc1B+v]+mTune+fineBFreqAdd);
        cvb=satAddU16S32(synth.oscBBaseCV[v],(int32_t)synth.benderCVs[pcOsc1B+v]+mTune+fineBFreq);

        cvf=satAddU16S16(synth.filterBaseCV[v],synth.benderCVs[pcFil1+v]);

        // glide

        synth.oscATargetCV[v]=cva;
        synth.oscBTargetCV[v]=cvb;
        synth.filterTargetCV[v]=cvf;
        if(synth.gliding)
        {
            if(!track)
                synth.filterNoteCV[v]=cvf; // no glide if no tracking for filter
        }
        else
        {
            synth.oscANoteCV[v]=cva;
            synth.oscBNoteCV[v]=cvb;
            synth.filterNoteCV[v]=cvf;
        }

    }
}

// Precalculate factor for dead band scaling to avoid time consuming
// division operation.
// so instead of doing foo*=32768; foo/=factor; we precalculate
// precalc=32768<<16/factor, and do foo*=precalc; foo>>=16; runtime.
// when "rise" >0 then this is not a strict Deadband but rather a Slowband
static void precalcDeadband(struct deadband *d)
{
    uint16_t middleLow=d->middle-d->deadband;
    uint16_t middleHigh=d->middle+d->deadband;

    d->precalcLow=(HALF_RANGE_L-(((uint32_t)d->rise)<<16))/(middleLow-d->guard);
    d->precalcHigh=(HALF_RANGE_L-(((uint32_t)d->rise)<<16))/(FULL_RANGE-d->guard-middleHigh);
    if (d->deadband>0) d->precalcRise=(((uint32_t)d->rise)<<16)/d->deadband;
}

static inline uint16_t addDeadband(uint16_t value, struct deadband *d)
{
    uint16_t middleLow=d->middle-d->deadband;
    uint16_t middleHigh=d->middle+d->deadband;
    uint32_t amt;

    if(value>FULL_RANGE-d->guard)
        return FULL_RANGE;
    if(value<d->guard)
        return 0;

    amt=value;

    if(value<middleLow) {
        amt-=d->guard;
        amt*=d->precalcLow; // result is 65536 too big now
    } else if(value>middleHigh) {
        amt-=middleHigh;
        amt*=d->precalcHigh; // result is 65536 too big now
        amt+=HALF_RANGE_L;
    } else { // in deadband
        if (d->rise==0) return HALF_RANGE;
        amt-=(middleLow+d->rise);
        amt*=d->precalcRise;
        amt+=HALF_RANGE_L;
    }
    // result of our calculations will be 0..UINT16_MAX<<16
    return amt>>16;
}



static inline int16_t getAdjustedBenderAmount(void)
{
    return addDeadband(potmux_getValue(ppPitchWheel),&bendDeadband)-HALF_RANGE;
}

void synth_updateBender(void) // this function must be called when the bender middle changes, also when bender range changes
{
    bendDeadband.middle=settings.benderMiddle;
    precalcDeadband(&bendDeadband);
    computeTunedOffsetCVs();
    synth_wheelEvent(getAdjustedBenderAmount(),0,1,1,0);
}

void synth_resetClockBar(void) // this function can be called from other places to reset the LFO sync
{
    synth.clockBar=0x9; // LFO clock is built on a 9 loop
}


void synth_updateMasterVolume(void)
{
    sh_setCV(pcMVol,potmux_getValue(ppMVol),SH_FLAG_IMMEDIATE); // adapted from J. Sepulveda's V2.26B
}

static void computeBenderCVs(void) // this function always needs to be called when the bender position changes
{
    int32_t bend;
    p600CV_t cv, cvFrom;

    // compute bends

    // reset old bends

    for(cv=pcOsc1A; cv<=pcFil6; ++cv)
        synth.benderCVs[cv]=0;
    synth.benderVolumeCV=0;
    synth.benderPW=0;

    // compute new
    bend=currentPreset.steppedParameters[spBenderSemitones];
    bend*=satAddS16S16(synth.benderAmountInternal,synth.benderAmountExternal);
    bend*=FULL_RANGE/12;

    switch(currentPreset.steppedParameters[spBenderTarget])
    {
        case modB:
        case modAB:
            cvFrom=currentPreset.steppedParameters[spBenderTarget]==modB?pcOsc1B:pcOsc1A;
            for(cv=cvFrom; cv<=pcOsc6B; ++cv)
            {
                bend=synth.tunedBenderCVs[cv]*satAddS16S16(synth.benderAmountInternal, synth.benderAmountExternal);
                synth.benderCVs[cv]=bend>>16; // /65536
            }
            break;
        case modVCF:
            for(cv=pcFil1; cv<=pcFil6; ++cv)
            {
                synth.benderCVs[cv]=bend>>16; // ... after >>16
            }
            break;
        case modVCA:
            synth.benderVolumeCV=bend>>16; // ... after >>16
            break;
        case modPW: // pulse width of oscillator A: the same depth as the filter (bend semitones * the bend position), see refreshPulseWidth
            synth.benderPW=bend>>16;
            break;
        default:
            ;
    }
}

static inline void computeGlide(uint16_t * out, const uint16_t target, const uint16_t amount)
{
    uint16_t diff;

    if(*out<target)
    {
        diff=target-*out;
        *out+=MIN(amount,diff);
    }
    else if(*out>target)
    {
        diff=*out-target;
        *out-=MIN(amount,diff);
    }
}

static void refreshVibLFO(void)
{
    if(currentPreset.steppedParameters[spModwheelTarget]==0) // targeting lfo?
    {
        lfo_setAmt(&synth.lfo, satAddU16U16(synth.lfoAmt, synth.modwheelAmount));
        lfo_setAmt(&synth.vibrato, scaleU16U16(synth.vibAmt,synth.dlyAmt));
    }
    else // targeting vibrato
    {
        lfo_setAmt(&synth.lfo, scaleU16U16(synth.lfoAmt, synth.dlyAmt));
        lfo_setAmt(&synth.vibrato, satAddU16U16(synth.vibAmt, synth.modwheelAmount));
    }
}


static void refreshModDelayLFORetrigger(int8_t refreshDelayTickCount)
{
    int8_t anyPressed, anyAssigned;
    static int8_t prevAnyPressed=0;

    anyPressed=assigner_getAnyPressed();
    anyAssigned=assigner_getAnyAssigned();

    if(!anyAssigned)
    {
        synth.modulationDelayStart=UINT32_MAX;
    }

    if(anyPressed && !prevAnyPressed) // restart the delay
    {
        synth.modulationDelayStart=currentTick;
        if (currentPreset.continuousParameters[cpModDelay]>=15000) // in order to allow for slowest synth_update cycle don't reset for shortest delay times
        {
            synth.dlyAmt=0;
        }

        if(currentPreset.steppedParameters[spLFOSync]==1) // this is retrigger on keyboard
        {
            lfo_resetPhase(&synth.lfo);
        }

        refreshVibLFO();
    }

    prevAnyPressed=anyPressed;

    if(refreshDelayTickCount)
        synth.modulationDelayTickCount=exponentialCourse(UINT16_MAX-currentPreset.continuousParameters[cpModDelay],12000.0f,2500.0f);
}

static void handleFinishedVoices(void)
{
    int8_t v, waitFlag;


    for(v=0; v<SYNTH_VOICE_COUNT; ++v)
    {
        waitFlag=synth.ampEnvs[v].stage==sWait;

        if (assigner_getAssignment(v,NULL))
        {
            // when amp env finishes, voice is done
            if (waitFlag)
                assigner_voiceDone(v);
        }
        else
        {
            // if voice isn't assigned, silence it (but only if not finished
            if (!waitFlag)
            {
                adsr_reset(&synth.ampEnvs[v]);
                adsr_reset(&synth.filEnvs[v]);
            }
        }
    }
}

static void refreshGates(void)
{
    sh_setGate(pgASaw,currentPreset.steppedParameters[spASaw]);
    sh_setGate(pgBSaw,currentPreset.steppedParameters[spBSaw]);
    sh_setGate(pgATri,currentPreset.steppedParameters[spATri]);
    sh_setGate(pgBTri,currentPreset.steppedParameters[spBTri]);
    sh_setGate(pgSync,currentPreset.steppedParameters[spSync]);
    sh_setGate(pgPModFA,currentPreset.steppedParameters[spPModFA]);
    sh_setGate(pgPModFil,currentPreset.steppedParameters[spPModFil]);
}

// oscEnvAmt is the poly-mod envelope amount (see synth_timerInterrupt, it is 0 unless the Freq A poly-mod switch is on).
// The pulse width CVs exist only once per oscillator (all voices share them), so the poly-mod envelope can not act per voice here.
// With the PW targets of spPolyModEnv (3 = A, 4 = B, 5 = both) the HIGHEST envelope of the six voices is used: the depth does not
// depend on how many notes are held, a new note shows its attack as soon as it is above the others, and there is nothing to accumulate or reset.
static inline void refreshPulseWidth(int8_t pwm, int16_t oscEnvAmt)
{
    int32_t pa,pb;
    int32_t pwEnvA=0,pwEnvB=0; // the poly-mod envelope part of the pulse width, added only to a square wave that is switched on
    uint8_t pmTarget=currentPreset.steppedParameters[spPolyModEnv];

    //	the following is a fix of a bug in 2.0: "fixing wrong OscA pitch when polymod routes OscB to FreqA."
    //	datasheet specifies that pa should default to max and pb should default to min to avoid issues with sync and polymod
    if (currentPreset.steppedParameters[spPWMBug]==0) // this means bug is "switched off"
    {
        pa=UINT16_MAX;
        pb=0;
    }
    else // bug is "switched on" --> compatible with versions <= 2.1 RC3. patches loaded from that version will have the bug set to "ON".
    {
        pa=pb=UINT16_MAX; // in various cases, defaulting this CV to zero made PW still bleed into audio (eg osc A with sync)
    }

    uint8_t sqrA=currentPreset.steppedParameters[spASqr];
    uint8_t sqrB=currentPreset.steppedParameters[spBSqr];

    if(pmTarget>=3 && oscEnvAmt!=0)
    {
        uint16_t top=0,e;
        int8_t v;
        int32_t pw;

        for(v=0; v<SYNTH_VOICE_COUNT; ++v)
        {
            // the same envelope that drives the pitch targets: the filter envelope, or the amp envelope for the other routings (see refreshVoice)
            e=(currentPreset.steppedParameters[spEnvRouting]==0)?synth.filEnvs[v].output:synth.ampEnvs[v].output;
            if(e>top)
                top=e;
        }

        pw=(int32_t)scaleU16S16(top,oscEnvAmt)*2; // at the full amount the pulse width moves by half of its range (the CV is limited at both ends below)
        if(pmTarget!=4) // 3 = A, 5 = both
            pwEnvA=pw;
        if(pmTarget!=3) // 4 = B, 5 = both
            pwEnvB=pw;
    }

    if(sqrA)
    {
        pa=currentPreset.continuousParameters[cpAPW];
        pa+=pwEnvA; // only here: with the square wave off the CV must keep its default or the pulse would leak into the audio
        pa+=synth.benderPW; // the bender target pw, also only with the square wave on (it is 0 for the other bender targets)
    }

    if(sqrB)
    {
        pb=currentPreset.continuousParameters[cpBPW];
        pb+=pwEnvB;
    }

    if(pwm)
    {
        if(sqrA && !(currentPreset.steppedParameters[spLFOTargets]&mtOnlyB))
            pa+=synth.lfo.output;

        if(sqrB && !(currentPreset.steppedParameters[spLFOTargets]&mtOnlyA))
            pb+=synth.lfo.output;
    }

    BLOCK_INT
    {
        sh_setCV32Sat_FastPath(pcAPW,pa);
        sh_setCV32Sat_FastPath(pcBPW,pb);
    }
}

static void refreshAssignerSettings(void)
{
    if(currentPreset.steppedParameters[spUnison])
        assigner_setPattern(currentPreset.voicePattern,1);
    else
        assigner_setPoly();

    assigner_setVoiceMask(settings.voiceMask);
    assigner_setPriority(currentPreset.steppedParameters[spAssignerPriority]);
}

static void refreshEnvSettings(void)
{
    int8_t i, v_aux;
    uint16_t as, fs;
    uint16_t aa,ad,ar,fa,fd,fr;
    float spread;

    as=currentPreset.continuousParameters[cpAmpSus]; // there is no spread on the sustain
    fs=currentPreset.continuousParameters[cpFilSus]; // there is no spread on the sustain

    for(i=0; i<SYNTH_VOICE_COUNT; ++i)
    {
        adsr_setShape(&synth.ampEnvs[i],currentPreset.steppedParameters[spAmpEnvShape]);
        adsr_setShape(&synth.filEnvs[i],currentPreset.steppedParameters[spFilEnvShape]);

        adsr_setSpeedShift(&synth.ampEnvs[i],(currentPreset.steppedParameters[spAmpEnvSlow])?3:1);
        adsr_setSpeedShift(&synth.filEnvs[i],(currentPreset.steppedParameters[spFilEnvSlow])?3:1);

        if (currentPreset.continuousParameters[cpSpread]>1000)
        {
            spread=(int16_t)((currentPreset.continuousParameters[cpSpread]>>4)); // the bit shift determines the overall effect strength of the spread
        }
        else
        {
            spread=0;
        }

        v_aux=(i+1)%6;
        aa=scaleProportionalU16S16(currentPreset.continuousParameters[cpAmpAtt],(1+(v_aux>>1))*(v_aux&1?-1:1)*spread/1.5f);
        v_aux=(i+2)%6;
        ad=scaleProportionalU16S16(currentPreset.continuousParameters[cpAmpDec],(1+(v_aux>>1))*(v_aux&1?-1:1)*spread);
        v_aux=(i+3)%6;
        ar=scaleProportionalU16S16(currentPreset.continuousParameters[cpAmpRel],(1+(v_aux>>1))*(v_aux&1?-1:1)*spread/2.0f);

        v_aux=(i+4)%6;
        fa=scaleProportionalU16S16(currentPreset.continuousParameters[cpFilAtt],(1+(v_aux>>1))*(v_aux&1?-1:1)*spread/1.5f);
        v_aux=(i+5)%6;
        fd=scaleProportionalU16S16(currentPreset.continuousParameters[cpFilDec],(1+(v_aux>>1))*(v_aux&1?-1:1)*spread);
        v_aux=i;
        fr=scaleProportionalU16S16(currentPreset.continuousParameters[cpFilRel],(1+(v_aux>>1))*(v_aux&1?-1:1)*spread/2.0f);

        adsr_setCVs(&synth.ampEnvs[i],aa,ad,as,ar,0,0x0f);
        adsr_setCVs(&synth.filEnvs[i],fa,fd,fs,fr,0,0x0f);

    }
}

static void refreshDlyAmount(void)
{
    uint32_t elapsed;

    // wait modulationDelayTickCount then progressively increase over
    // modulationDelayTickCount time, following an exponential curve
    synth.dlyAmt=0;
    if(synth.modulationDelayStart!=UINT32_MAX)
    {
        //if(currentPreset.continuousParameters[cpModDelay]<POT_DEAD_ZONE)
        if(currentPreset.continuousParameters[cpModDelay]<15000)
        {
            synth.dlyAmt=UINT16_MAX;
        }
        else if(currentTick>=synth.modulationDelayStart+synth.modulationDelayTickCount)
        {
            elapsed=currentTick-(synth.modulationDelayStart+synth.modulationDelayTickCount);
            if(elapsed>=synth.modulationDelayTickCount)
                synth.dlyAmt=UINT16_MAX;
            else
                synth.dlyAmt=pgm_read_word(&attackCurveLookup[(elapsed<<8)/synth.modulationDelayTickCount]); // the table lives in flash
        }
    }
}


static void refreshLfoSettings(void)
{
    lfoShape_t shape;

    shape=currentPreset.steppedParameters[spLFOShape];

    lfo_setShape(&synth.lfo,shape);

    synth.lfoAmt=currentPreset.continuousParameters[cpLFOAmt];
    synth.lfoAmt=(synth.lfoAmt<POT_DEAD_ZONE)?0:(synth.lfoAmt-POT_DEAD_ZONE);
    synth.lfoAmt=((expf(((float)synth.lfoAmt)/15000.0f )-1.0f)*870.0f);

    lfo_setFreq(&synth.lfo,currentPreset.continuousParameters[cpLFOFreq]);

    // the vibrato LFO: its shape (the triangle unless changed) and speed range (normal, or low = 8 times slower)
    lfo_setShape(&synth.vibrato,currentPreset.steppedParameters[spVibShape]);
    lfo_setSlowShift(&synth.vibrato,currentPreset.steppedParameters[spVibRange]?3:0);

    refreshVibLFO();
}



static void refreshSevenSeg(void) // imogen: this function would be more suited for ui.c than synth.c
{
    int8_t seqRec = seq_getMode(0)==smRecording || seq_getMode(1)==smRecording;
    uint8_t isPickedUp=0;

    if(seqRec) // sequence record mode and no parameter selection override, e.g. the input and display is sequencer
    {
        int8_t track=(seq_getMode(1)==smRecording)?1:0;
        uint8_t count=seq_getStepCount(track);
        int8_t full=seq_full(track);
        sevenSeg_setNumber(count);
        led_set(plDot,count>=100||full,full); // set blinking when full!
    }
    else if(ui.digitInput==diSynth) // live mode show values of last touched control
    {
        led_set(plDot,0,0);
        if(ui.lastActivePotValue>=0)
        {
            int32_t v;
            int8_t lastPotcP;
            lastPotcP=(ui.lastActivePot!=ppNone)?potToCP[ui.lastActivePot]:-1; // this is -1 for anything not stored in a patch

            if(ui.lastActivePot==ppPitchWheel)
            {
                v=getAdjustedBenderAmount();
                v-=INT16_MIN;
            }
            else
            {
				v=ui.adjustedLastActivePotValue;
            }

            if (lastPotcP>=0)
                if (currentPreset.contParamPotStatus[lastPotcP]==1) isPickedUp=1;

            if (lastPotcP<0 || isPickedUp) // it's a value not part of the currentPreset.cp , e.g. not stored in a patch; (always show as is) or it is pick up (also show)
            {
                if ((ui.lastActivePot==ppFreqA && (synth.freqDial&1)==0) || (ui.lastActivePot==ppFreqB && ((synth.freqDial>>2)&1)==0))
                {
                    if ((ui.lastActivePot==ppFreqA && ((synth.freqDial>>1)&1)==0) || (ui.lastActivePot==ppFreqB && ((synth.freqDial>>3)&1)==0)) // semi
                    {
                        sevenSeg_setNumber(v>>10); // frequency A or B in semitones
                    }
                    else // frequency A or B in octaves (c + octave number)
                    {
                        v=v>>10; // semitones (0...63)
                        v-=v%12; // first note in octave (0, 12, 24, 36, 48, 60)
                        v/=12; // octave number
                        sevenSeg_setAscii('c','0'+v);
                    }
                }
                else
                {
                    v=(v*100L)>>16; // 0..100 range
                    if(potmux_isPotZeroCentered(ui.lastActivePot, settings.panelLayout))
                    {
                        v=abs(v-50);
                        led_set(plDot,ui.adjustedLastActivePotValue<=INT16_MAX,0); // dot indicates negative
                    }
                    sevenSeg_setNumber(v);
                }
            }
            else if (currentPreset.contParamPotStatus[lastPotcP]>=2) // it is stored in a patch but not yet picked up
            {
                sevenSeg_setRelative(currentPreset.contParamPotStatus[lastPotcP]);
            }
        }
        else
        {
            sevenSeg_setAscii(' ',' ');
        }
    }
    else // this is showing preset number or waiting for first (decade) or second (unit) digit either for preset selection or preset saving
    {
        if(ui.digitInput!=diLoadDecadeDigit)
        {
            led_set(plDot,0,0);
            if(ui.presetAwaitingNumber>=0)
                sevenSeg_setAscii('0'+ui.presetAwaitingNumber,' ');
            else
                if (!ui.isInPatchManagement) sevenSeg_setAscii(' ',' '); // keep the previous display (might be a MIDI load or dump message)
        }
        else
        {
            sevenSeg_setNumber(settings.presetNumber);
            led_set(plDot,ui.presetModified,0);
        }
    }

    led_set(plPreset,settings.presetMode || ui.isInPatchManagement, ui.isInPatchManagement);
    led_set(plToTape,ui.digitInput==diSynth && settings.presetMode,0);
    led_set(plSeq1,seq_getMode(0)!=smOff,(seq_getMode(0)==smRecording)?1:0);
    led_set(plSeq2,seq_getMode(1)!=smOff,(seq_getMode(1)==smRecording)?1:0);
    led_set(plArpUD,arp_getMode()==amUpDown,0);
    led_set(plArpAssign,arp_getMode()>=amRandom,arp_getMode()==amRandom);
    led_set(plTune, ui.retuneLastNotePressedMode, ui.retuneLastNotePressedMode);
    led_set(plFromTape,ui.isShifted||ui.isDoubleClicked,ui.isDoubleClicked);

    int8_t storageMode=ui.digitInput==diStoreDecadeDigit || ui.digitInput==diStoreUnitDigit;
    if (seqRec || arp_getHold()) // on but not blinking
    {
        led_set(plRecord,1,0);
    }
    else if (storageMode && !ui.isInPatchManagement) // either storage or MIDI dump awaiting patch number input
    {
        led_set(plRecord,1,1);
    }
    else
    {
        led_set(plRecord,0,0);
    }
}

void refreshFilterMaxCV(void)
{
    // optional VCF limit at around 22Khz max frequency, to avoid harshness due to strange filter behavior in the ultrasound range

    for(int8_t v=0; v<SYNTH_VOICE_COUNT; ++v)
        if(settings.vcfLimit)
            synth.filterMaxCV[v]=tuner_computeCVFromNote(126,0,pcFil1+v);
        else
            synth.filterMaxCV[v]=UINT16_MAX;
}


void refreshFullState(void)
{
    refreshModDelayLFORetrigger(1);
    refreshGates();
    refreshAssignerSettings();
    refreshLfoSettings();
    ui.vibAmountChangePending=1;
    ui.vibFreqChangePending=1;
    refreshEnvSettings();
    computeTunedOffsetCVs();
    computeBenderCVs();
    refreshFilterMaxCV();

    refreshSevenSeg();
}

static void refreshPresetPots(int8_t force) // this only affects current preset parameters
{
    continuousParameter_t cp;
    p600Pot_t pp;

    for(cp=0; cp<cpCount; ++cp)
    {
        pp=continuousParameterToPot[cp];
        if((pp!=ppNone) && (force || pp==ui.lastActivePot || potmux_hasChanged(pp)))
        {
            p600Pot_t pp=continuousParameterToPot[cp];
            uint16_t value=potmux_getValue(pp);

            if(potmux_isPotZeroCentered(pp, settings.panelLayout))
            {
                if (pp==ppFreqBFine)
                {
                    value=addDeadband(value,&freqFineDeadband); // deadband  which is wider and flat but not zero.
                }
                else
                {
                    value=addDeadband(value,&panelDeadband); // deadband  which is zero.
                }
            }

            if (currentPreset.contParamPotStatus[cp]==1 || !(settings.presetMode && ui.digitInput<diLoadDecadeDigit)) // synth follows pot, picked up or live mode
            {
                currentPreset.continuousParameters[cp]=value;
                currentPreset.contParamPotStatus[cp]=1;
                ui.presetModified=1;
            }
            else // pot is still off
            {
                if ((currentPreset.continuousParameters[cp]>>10)==(value>>10)) // pick up pot when close enough
                {
                    currentPreset.contParamPotStatus[cp]=1;
                    currentPreset.continuousParameters[cp]=value;
                    ui.presetModified=1;
                }
                else
                {
                    currentPreset.contParamPotStatus[cp]=currentPreset.continuousParameters[cp]>value?2:3; // 2 means pot is lower, 3 means pot is higher
                }
            }

        }
        if (cp==cpMixVolA || cp==cpGlideVolB)
        {
            if (settings.panelLayout==0) // GliGli
            {
                currentPreset.continuousParameters[cpVolA]=currentPreset.continuousParameters[cpMixVolA];
                currentPreset.continuousParameters[cpVolB]=currentPreset.continuousParameters[cpGlideVolB];
            }
            else // SCI
            {
                currentPreset.continuousParameters[cpGlide]=currentPreset.continuousParameters[cpGlideVolB];
                currentPreset.continuousParameters[cpVolA]=mixer_volumeFromMixAndDrive(currentPreset.continuousParameters[cpMixVolA], currentPreset.continuousParameters[cpDrive]);
                currentPreset.continuousParameters[cpVolB]=mixer_volumeFromMixAndDrive(FULL_RANGE-currentPreset.continuousParameters[cpMixVolA], currentPreset.continuousParameters[cpDrive]);
            }

        }
    }

}

uint16_t mixer_volumeFromMixAndDrive(uint16_t mix, uint16_t drive)
{
    uint32_t vol;
    if (drive<HALF_RANGE)
    {
        vol= scaleU16U16(drive,UINT16_MAX-mix)<<1;
    }
    else
    {
        if (mix<HALF_RANGE)
        {
            vol=FULL_RANGE-(scaleU16U16(mix,FULL_RANGE-drive)<<1);
        }
        else
        {
            vol=scaleU16U16(FULL_RANGE-mix,drive)<<1;
        }
    }
    return vol;
}

uint16_t mixer_mixFromVols(uint16_t volA, uint16_t volB) // this is the inverse of "volumeFromMixAndDrive()"
{
    uint16_t mixVal, denom;
    uint32_t nomin;
    if (volA<=(FULL_RANGE-volB))
    {
        denom=(volA+volB);
        if (denom>0xff) // there is a singularity at drive --> 0
        {
            nomin=((uint32_t)volB<<16);
            mixVal=nomin/denom;
        }
        else
        {
            mixVal=HALF_RANGE; // there is not choice but picking a deliberate mix value as drive tends to zero
        }
    }
    else
    {
        mixVal=(FULL_RANGE>>1)+(volB>>1)-(volA>>1);
    }
    return mixVal;
}

uint16_t mixer_driveFromVols(uint16_t volA, uint16_t volB) // this is the inverse of "volumeFromMixAndDrive()"
{
    uint16_t drive;
    uint32_t nomin;
    if (volA<=(FULL_RANGE-volB))
    {
        drive=(volA>>1)+(volB>>1);
    }
    else
    {
        if (volA==FULL_RANGE || volB==FULL_RANGE)
        {
            drive=FULL_RANGE;
        }
        else if (volA >= volB)
        {
            nomin=((uint32_t)(volB))<<16;
            drive=nomin/(FULL_RANGE+volB-volA);
        }
        else
        {
            nomin=((uint32_t)(volA))<<16;
            drive=nomin/(FULL_RANGE+volA-volB);
        }
    }
    return drive;
}

void refreshPresetMode(void)
{

    if(!preset_loadCurrent(settings.presetMode?settings.presetNumber:MANUAL_PRESET_PAGE,0))
    {
        preset_loadDefault(1);
    }

    if(!settings.presetMode)
    {
        // apply all current panel control setting, buttons, switches and pots
        refreshAllPresetButtons();
        refreshPresetPots(1);
    }

    ui_setNoActivePot(1);
    ui.presetModified=0;
    // trigger application of vib changes which are only conditional to save time in the main update
    ui.vibAmountChangePending=1;
    ui.vibFreqChangePending=1;
    ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;

}

// Ensemble vibrato: the voices are summed to mono, so when every voice gets its own phase of the vibrato LFO, the voices detune
// against each other in a shifting way, which sounds like a chorus (pure pitch modulation).
// The phase offset of a voice is its number times a step: small = 1/48 of a cycle (7.5 degrees), wide = 1/6 of a cycle (60 degrees, so the six voices
// are spread over most of the cycle). A voice restarts at its offset when it starts a new note.
static const uint32_t ensembleStep[ENSEMBLE_CHOICES]={0,0x05555555UL,0x2AAAAAAAUL};

// saturates to the range of a 16 bit signed value
static inline int16_t satS16(int32_t v)
{
    if(v>INT16_MAX) // too high
        return INT16_MAX;
    if(v<INT16_MIN) // too low
        return INT16_MIN;
    return (int16_t)v;
}

// Vibrato targets that are not the pitch or the amplifier: 4 = noise level (ext volt), 5 = filter resonance, 6 = detune, 7 = poly-mod amount of oscillator B.
// Targets 4, 5 and 7 change a CV that the main loop also writes now and then (to keep the voltages correct). While the vibrato is the target of such a CV
// the timer interrupt writes it (500Hz) and the main loop leaves it alone: if both wrote it, the main loop could write a modulation value that is
// already outdated just after the interrupt wrote a newer one. The main loop writes the plain value again as soon as the target is another one.

// the noise level CV as the main loop writes it. The max voltage on the hardware is reached for about 0.4 of the uint16_t range, this scaling optimizes the parameter travel
#define NOISE_CV_BASE() ((uint16_t)(0.4f*((float)currentPreset.continuousParameters[cpExternal])))

// vibrato target noise level, resonance or poly-mod amount (500Hz, from the timer interrupt): adds the vibrato to the CV.
// The amount is scaled up (noise and resonance x4, poly-mod amount x16, like in the older version of this firmware) because these CVs
// react to far less than a pitch does. The modulation is limited to the range of an int16 (half of the CV range up or down).
static void vibratoCvTarget(uint8_t target)
{
    int32_t mod=synth.vibrato.output;
    uint16_t base;
    p600CV_t cv;

    switch(target)
    {
    case 4:
        mod*=4;
        base=NOISE_CV_BASE();
        cv=pcExtFil;
        break;
    case 5:
        mod*=4;
        base=currentPreset.continuousParameters[cpResonance];
        cv=pcResonance;
        break;
    default: // 7
        mod*=16;
        base=currentPreset.continuousParameters[cpPModOscB];
        cv=pcPModOscB;
        break;
    }

    if(mod>INT16_MAX)
        mod=INT16_MAX;
    else if(mod<INT16_MIN)
        mod=INT16_MIN;

    sh_setCV32Sat_FastPath(cv,(int32_t)base+mod);
}

// vibrato target detune (500Hz): the voices move against each other, like the detune of unison does, but with the vibrato LFO.
// The voice number gives the direction and size, 1,-1,2,-2,3,-3 thirds of the normal vibrato pitch (the outermost voices move as far as a normal vibrato).
// This uses the per voice pitch of the ensemble vibrato (ensPitchA / ensPitchB are added to the pitch), which is zero for the other voices of the LFO.
static void vibratoDetuneUpdate(void)
{
    static const int32_t third[SYNTH_VOICE_COUNT]={21845,-21845,43690,-43690,65535,-65535}; // (1,-1,2,-2,3,-3)/3 as a fraction of 65536
    int16_t d=synth.vibrato.output>>2; // the size of the normal vibrato pitch
    int16_t delta;
    int8_t v;

    synth.ensembleActive=1;
    for(v=0;v<SYNTH_VOICE_COUNT;++v)
    {
        delta=((int32_t)d*third[v])>>16;
        synth.ensPitchA[v]=delta;
        synth.ensPitchB[v]=delta;
    }
}

// called with the vibrato (500Hz), after the single vibrato LFO has been updated.
// The extra LFOs are only computed when all of this is true, otherwise only the single vibrato LFO is used (nothing is computed here):
// - the ensemble setting is not off
// - unison is on and it is real unison: all voices play the same note (a chord memory or a single note pattern is left alone)
// - the vibrato goes to the pitch (VCO, VCO A or VCO B), not to the amplifier or to one of the new targets
// - the vibrato shape is the triangle (the extra LFOs are triangles)
// The vibrato target detune uses the same per voice pitch, see vibratoDetuneUpdate.
static void ensembleUpdate(void)
{
    uint8_t pattern[SYNTH_VOICE_COUNT];
    int8_t mono=0,active=0,v;
    uint32_t inc,p;
    uint16_t raw;
    int16_t out,delta;
    uint8_t target;

    if(currentPreset.steppedParameters[spVibTarget]==6) // detune: its own per voice pitch, the ensemble is not used with it
    {
        vibratoDetuneUpdate();
        return;
    }

    if(currentPreset.steppedParameters[spEnsemble] && currentPreset.steppedParameters[spUnison] &&
       (currentPreset.steppedParameters[spVibTarget]==0 || currentPreset.steppedParameters[spVibTarget]==2 || currentPreset.steppedParameters[spVibTarget]==3) &&
       currentPreset.steppedParameters[spVibShape]==lsTri)
    {
        assigner_getPattern(pattern,&mono); // the note offsets the assigner plays for one key press
        active=mono; // unison (the pattern is only used in unison)
        for(v=0;v<SYNTH_VOICE_COUNT;++v)
            if(pattern[v]!=0) // an offset to the root note or no note at all: a chord or a single note
                active=0;
    }

    if(!active) // not possible or not wanted: every voice uses the single vibrato LFO again
    {
        synth.ensembleActive=0;
        return;
    }

    synth.ensStep=ensembleStep[currentPreset.steppedParameters[spEnsemble]]; // small or wide

    if(!synth.ensembleActive) // just switched on: start with the phases spread out
    {
        synth.ensembleActive=1;
        for(v=0;v<SYNTH_VOICE_COUNT;++v)
            synth.ensPhase[v]=synth.ensStep*(uint8_t)v;
    }

    // the phase of the single vibrato LFO is a 24 bit value that runs up and down, so the speed of a 32 bit cycle is (speed<<7)
    inc=((uint32_t)synth.vibrato.speed)<<7;
    target=currentPreset.steppedParameters[spVibTarget]; // 0 = both oscillators, 2 = A, 3 = B

    for(v=0;v<SYNTH_VOICE_COUNT;++v)
    {
        p=synth.ensPhase[v]; // phase of this voice

        // triangle, the same shape and level as the single vibrato LFO (a triangle wave in 16 bits)
        raw=(uint16_t)(p>>16);
        raw=(raw&0x8000)?((uint16_t)(UINT16_MAX-raw)<<1):(raw<<1);
        out=scaleU16S16(synth.vibrato.levelCV,(int32_t)raw+INT16_MIN);

        delta=(out>>2)-(int16_t)synth.vibPitch; // the single LFO is already added to the pitch, so only the difference is added
        synth.ensPitchA[v]=(target==3)?0:delta;
        synth.ensPitchB[v]=(target==2)?0:delta;

        synth.ensPhase[v]=p+inc; // advance (wraps around after a whole cycle)
    }
}

// Where the poly-mod envelope goes as pitch, decided once per tick in synth_timerInterrupt from spPolyModEnv (0 = VCO A, 1 = VCO B, 2 = both)
static uint8_t pmToA=1,pmToB=0;

static FORCEINLINE void refreshVoice(int8_t v,int16_t oscEnvAmt,int16_t filEnvAmt,int16_t pitchALfoVal,int16_t pitchBLfoVal,int16_t filterLfoVal,uint16_t ampLfoVal)
{
    int32_t va,vb,vf;
    uint16_t envVal;
    uint16_t ampEnvVal;

    BLOCK_INT
    {
        // update envs, compute CVs & apply them

        adsr_update(&synth.filEnvs[v]);
        envVal=synth.filEnvs[v].output;

        adsr_update(&synth.ampEnvs[v]);
        ampEnvVal =synth.ampEnvs[v].output;

        // poly-mod envelope, as pitch (spPolyModEnv: 0 = VCO A, the original behaviour, 1 = VCO B, 2 = both. 3..5 are the pulse widths, see refreshPulseWidth)

        if (currentPreset.steppedParameters[spEnvRouting]==0) // the normal case
            va=scaleU16S16(envVal,oscEnvAmt);
        else // all other cases
            va=scaleU16S16(ampEnvVal,oscEnvAmt);

        // osc B

        vb=pitchBLfoVal;
        if(pmToB)
            vb+=va; // (int16 parameter below, like the VCO A sum)
        vb=scaleU16S16(synth.tunedOctaveCVs[pcOsc1B+v],vb);
        vb+=synth.oscBNoteCV[v];
        sh_setCV32Sat_FastPath(pcOsc1B+v,vb);

        // osc A

        if(!pmToA) // the envelope goes elsewhere
            va=0;

        va=scaleU16S16(synth.tunedOctaveCVs[v],va+pitchALfoVal);
        va+=synth.oscANoteCV[v];
        sh_setCV32Sat_FastPath(pcOsc1A+v,va);

        // filter

        vf=filterLfoVal;
        vf+=scaleU16S16(envVal,filEnvAmt);
        vf+=synth.filterNoteCV[v];
        vf+=synth.polyATCV[v]; // poly aftertouch (0 when not used); the clamp below keeps the result in range

        if(vf>synth.filterMaxCV[v])
            vf=synth.filterMaxCV[v];

        sh_setCV32Sat_FastPath(pcFil1+v,vf);

        // apply amplifier

        if (currentPreset.steppedParameters[spEnvRouting]==2) // the poly case, e.g. amplitude via filter envelope
            va=scaleU16U16(envVal,ampLfoVal);
        else if (currentPreset.steppedParameters[spEnvRouting]==3) // this is the gate case for amplitude
        {
            va=0;
            if (adsr_getStage(&synth.ampEnvs[v])>=sAttack&&adsr_getStage(&synth.ampEnvs[v])<=sSustain)
                va=scaleU16U16(FULL_RANGE,ampLfoVal); // this behaves like a gate shape
        }
        else // standard
            va=scaleU16U16(ampEnvVal,ampLfoVal);

        if(va)
            va+=VCA_DEADBAND;

        sh_setCV32Sat_FastPath(pcAmp1+v,va);
    }
}



static void handleBitInputs(void)
{
    uint8_t cur;
    static uint8_t last=0;

    BLOCK_INT
    {
        cur=io_read(CSI0);
    }

    // control footswitch

    if ((cur&BIT_INTPUT_FOOTSWITCH)!=(last&BIT_INTPUT_FOOTSWITCH))
    {
        if(arp_getMode()!=amOff)
        {
            arp_setMode(arp_getMode(),(cur&BIT_INTPUT_FOOTSWITCH)?0:1);
            refreshSevenSeg();
        }
        else
        {
            synth_holdEvent((cur&BIT_INTPUT_FOOTSWITCH)?0:1, 1, 1);
        }
    }

    // tape in

    if(settings.syncMode==smTape && cur&BIT_INTPUT_TAPE_IN && !(last&BIT_INTPUT_TAPE_IN))
    {
        synth.pendingExtClock+=2;
    }

    // this must stay last

    last=cur;
}

////////////////////////////////////////////////////////////////////////////////
// P600 main code
////////////////////////////////////////////////////////////////////////////////

#ifdef DIAG
// V2.5 diagnostic build (make DIAG=1): shows a step code on the left digit at each step of the start-up, with interrupts off, written straight to the
// display latches like the updater does, shows it for 0.4 s and switches it off again. The last code seen tells where the start-up stops.
void diag_step(uint8_t code)
{
    static const uint8_t seg[16]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f,0x77,0x7c,0x39,0x5e,0x79,0x71}; // 0-9 A b C d E F
    uint8_t i;
    BLOCK_INT
    {
        io_write(0x09,0x00);
        io_write(0x08,0x20); // left digit
        io_write(0x09,seg[code&15]|(code&0x80)); // bit 7 of the code = the dot (sub-steps)
        io_read(0x0a); // completes the write (see display_restoreColumn)
    }
    for(i=0;i<40;++i)
        MDELAY(10);
    // switch the digit off again, so the display/scanner latches are as they would be without this diagnostic (the code stays visible for 0.4 s)
    BLOCK_INT
    {
        io_write(0x09,0x00);
        io_write(0x08,0x00);
        io_read(0x0a);
    }
}
#define DIAG_STEP(c) diag_step(c)
#else
#define DIAG_STEP(c)
#endif

void synth_init(void)
{
    int8_t i;
    int8_t settingsWereLoaded; // V2.5

    // init

    memset(&synth,0,sizeof(synth));
    DIAG_STEP(1);

    scanner_init();
    display_init();
    sh_init();
    potmux_init();
    DIAG_STEP(2);
    tuner_init();
    assigner_init();
    DIAG_STEP(3);
    uart_init();
    DIAG_STEP(4);
    seq_init();
    arp_init();
    ui_init();
    midi_init();

    for(i=0; i<SYNTH_VOICE_COUNT; ++i)
    {
        adsr_init(&synth.ampEnvs[i]);
        adsr_init(&synth.filEnvs[i]);
    }

    lfo_init(&synth.lfo);
    lfo_init(&synth.vibrato);
    lfo_setShape(&synth.vibrato,lsTri);

    //for NOISE stop Noise waveform
    sh_setCV(pcExtFil,0,SH_FLAG_IMMEDIATE);

    DIAG_STEP(5);
    // go in scaling adjustment mode if needed

    if(io_read(CSI0)&16)
    {
        DIAG_STEP(15); // F = the scaling adjustment mode was entered (TP301 read as set)
        tuner_scalingAdjustment();
    }

    DIAG_STEP(6);
    // load settings from storage; tune when they are bad

    settingsWereLoaded=settings_load(); // V2.5: remembered, see the settings_save at the end
    if(!settingsWereLoaded)
    {
        settings_loadDefault(); // first startup: panel mode, GilGi panel layout, basic init tuning, MIDI OMNI receive, bend middle position

#ifndef DEBUG
        tuner_tuneSynth();
#endif
    }

    DIAG_STEP(7);
    // power-up light show (about 3.2 seconds, interrupts are still off here); it can be switched off in the From Tape menu (keypad 7).
    // This is after the settings are loaded because the choice is stored there.
    if(settings.bootLightShow)
        display_bootShow();

    DIAG_STEP(8);
    // initialize manual preset page if needed

    if(!preset_loadCurrent(MANUAL_PRESET_PAGE,0))
    {
        DIAG_STEP(0x85); // "5." the manual preset page is empty or damaged: a default is saved next (flash write)
        preset_loadDefault(1);
        preset_saveCurrent(MANUAL_PRESET_PAGE);
        DIAG_STEP(0x86); // "6." flash write done
    }
    else
        DIAG_STEP(0x84); // "4." the manual preset loaded

    DIAG_STEP(9);
    sh_setCV(pcMVol,HALF_RANGE,SH_FLAG_IMMEDIATE);
    DIAG_STEP(0x81); // "1." volume CV set

    // dead band pre calculation
    precalcDeadband(&panelDeadband);
    bendDeadband.middle=settings.benderMiddle;
    precalcDeadband(&bendDeadband);
    precalcDeadband(&freqFineDeadband);
    DIAG_STEP(0x82); // "2." dead bands done, the first full keyboard/button scan is next

    // initial input state

    scanner_update(1);
    DIAG_STEP(0x83); // "3." keyboard/button scan done, reading all pots is next
    potmux_update(1); // init all
    DIAG_STEP(10);

    // load last preset & do a full refresh
    refreshPresetMode();
    refreshFullState();
    computeTunedCVs(-1,-1); // force init CV's for all voices
    DIAG_STEP(11);
    ui_setNoActivePot(1);
    DIAG_STEP(0x87); // "7." the settings are saved next (flash write)
    // V2.5: save the settings at start-up only when they had to be made new (V2.34 never saved at start-up). A flash write at every
    // start-up wears the flash and, on the Teensy, goes through the bootloader each time the synth is switched on.
    if(!settingsWereLoaded)
        settings_save();

    DIAG_STEP(12);
    // set the volume to the current pot value
    synth.masterVolume=potmux_getValue(ppMVol);

    // a nice welcome message, and we're ready to go :)

    //sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
    //sevenSeg_scrollText("P600 MoGliFied "VERSION,1); // (Imogen's welcome text)
    sevenSeg_scrollText("P600 Matrix edition "VERSION,1); // V2.5: "P600 Matrix edition Ver 2.5" (V2.34 showed "GliGli's P600 upgrade Noise mod Ver 2.34 jrs")
}

// Keyboard -> MIDI out is queued here by the timer interrupt and sent from the main loop.
// Sending directly from the interrupt (on top of scanner + voice allocation) froze the synth.
#define KBD_MIDI_Q 8 // must be a power of 2
static volatile uint8_t kbdMidiBuf[KBD_MIDI_Q]; // bit 7 = gate, bits 0..6 = note
static volatile uint8_t kbdMidiHead, kbdMidiTail;

static void kbdMidiPush(uint8_t note, int8_t gate) // called from the timer interrupt only
{
    uint8_t t=kbdMidiTail;
    if (((t+1)&(KBD_MIDI_Q-1))==kbdMidiHead) return; // queue full, drop
    kbdMidiBuf[t]=(note&0x7f)|(gate?0x80:0);
    kbdMidiTail=(t+1)&(KBD_MIDI_Q-1);
}

static void kbdMidiFlush(void) // called from the main loop only
{
    while (kbdMidiHead!=kbdMidiTail)
    {
        uint8_t h=kbdMidiHead, e=kbdMidiBuf[h];
        kbdMidiHead=(h+1)&(KBD_MIDI_Q-1);
        midi_sendNoteEvent(e&0x7f,(e&0x80)!=0,HALF_RANGE);
    }
}

void synth_update(void)
{
    int32_t potVal;
    static uint8_t frc=0;
    //synth.updateCounter++;

    kbdMidiFlush(); // send queued keyboard notes to MIDI out

    // toggle tape out (debug)

    BLOCK_INT
    {
        ++frc;
        io_write(CS06,((frc&1)<<2)|0b00110001);
    }

    // update pots, detecting change

    potmux_resetChanged();
    potmux_update(0);

    // act on pot change

    ui_checkIfDataPotChanged(); // this sets ui.lastActivePot and handles the menu parameters
    refreshPresetPots(0); // this updates currentPreset.cp[] if "forced" (function parameter), lastActivePot or changed

    if(ui.lastActivePot!=ppNone)
    {
        potVal=potmux_getValue(ui.lastActivePot);
        if(potVal!=ui.lastActivePotValue)
        {
            ui.lastActivePotValue=potVal;
            ui.adjustedLastActivePotValue=potVal;

            if(potmux_isPotZeroCentered(ui.lastActivePot, settings.panelLayout) && ui.lastActivePot!=ppFreqBFine)
                ui.adjustedLastActivePotValue=addDeadband(potVal,&panelDeadband);

            refreshSevenSeg();

            // update CVs

            if(ui.lastActivePot==ppModWheel)
                synth_wheelEvent(0,potmux_getValue(ppModWheel),2,1,1);
            else if(ui.lastActivePot==ppPitchWheel)
            {
                synth_wheelEvent(getAdjustedBenderAmount(),0,1,1,1);
                sh_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),synth.benderVolumeCV),SH_FLAG_IMMEDIATE);
            }
            else if (ui.lastActivePot==ppAmpAtt || ui.lastActivePot==ppAmpDec ||
                     ui.lastActivePot==ppAmpSus || ui.lastActivePot==ppAmpRel ||
                     ui.lastActivePot==ppFilAtt || ui.lastActivePot==ppFilDec ||
                     ui.lastActivePot==ppFilSus || ui.lastActivePot==ppFilRel)
                refreshEnvSettings();
        }
    }

    // immediate response the value changes

    if (potmux_hasChanged(ppMVol))
    {
        synth.masterVolume = potmux_getValue(ppMVol);
        sh_setCV(pcMVol,satAddU16S16(synth.masterVolume,synth.benderVolumeCV),SH_FLAG_IMMEDIATE);
    }

    if (potmux_hasChanged(ppPModOscB) && currentPreset.steppedParameters[spVibTarget]!=7) // (with the vibrato as its target the timer interrupt writes it, see vibratoCvTarget)
        sh_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],SH_FLAG_IMMEDIATE);

    if (potmux_hasChanged(ppMixer))
    {
        sh_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],SH_FLAG_IMMEDIATE);
        if (settings.panelLayout==1)
            sh_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SH_FLAG_IMMEDIATE);
    }

    if (potmux_hasChanged(ppGlide))
    {
        if (settings.panelLayout==0)
        {
            sh_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SH_FLAG_IMMEDIATE);
        }
        else
        {
            synth.glideAmount=exponentialCourse(currentPreset.continuousParameters[cpGlide],11000.0f,2100.0f);
            synth.gliding=synth.glideAmount<2000;
        }
    }

    if (ui.vibAmountChangePending==1)
    {
        synth.vibAmt=currentPreset.continuousParameters[cpVibAmt];
        synth.vibAmt=(synth.vibAmt<POT_DEAD_ZONE)?0:(synth.vibAmt-POT_DEAD_ZONE);
        synth.vibAmt=((expf(((float)synth.vibAmt)/15000.0f )-1.0f)*870.0f);
        ui.vibAmountChangePending=0;
    }

    if (ui.vibFreqChangePending==1)
    {
        lfo_setFreq(&synth.vibrato,currentPreset.continuousParameters[cpVibFreq]);
        ui.vibFreqChangePending=0;
    }

    // lfo: for amount and frequency changes the change should be immediate
    if (potmux_hasChanged(ppLFOAmt) || potmux_hasChanged(ppLFOFreq))
        refreshLfoSettings();

    // regular updates to keep correct voltages

    switch((frc)&0x07) // 8 phases
    {
        case 0:
            if (currentPreset.steppedParameters[spVibTarget]!=7) // (not while the vibrato modulates it, see vibratoCvTarget)
                sh_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],SH_FLAG_IMMEDIATE);
            break;
        case 1:
            if (currentPreset.steppedParameters[spVibTarget]!=5) // (not while the vibrato modulates it)
                sh_setCV(pcResonance,currentPreset.continuousParameters[cpResonance],SH_FLAG_IMMEDIATE);
            break;
        case 2:
            sh_setCV(pcMVol,satAddU16S16(synth.masterVolume,synth.benderVolumeCV),SH_FLAG_IMMEDIATE);
            break;
        case 3:
            if (currentPreset.steppedParameters[spVibTarget]!=4) // (not while the vibrato modulates it)
                sh_setCV(pcExtFil,NOISE_CV_BASE(),SH_FLAG_IMMEDIATE); // the scaling is explained at NOISE_CV_BASE
            break;
        case 4:
            sh_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],SH_FLAG_IMMEDIATE);
            break;
        case 5:
            sh_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SH_FLAG_IMMEDIATE);
            break;
        case 6:
            refreshLfoSettings();
            // modulation delay
            refreshDlyAmount();
            break;
        case 7:
            refreshGates();
            synth.glideAmount=exponentialCourse(currentPreset.continuousParameters[cpGlide],11000.0f,2100.0f);
            synth.gliding=synth.glideAmount<2000;
            // arp and seq
            clock_setSpeed(settings.seqArpClock);
            break;
    }

    // tuned CVs

    computeTunedCVs(0,-1);
}

void synth_tuneSynth(void)
{
    tuner_tuneSynth();
    computeTunedOffsetCVs();
    synth_updateMasterVolume();
}


////////////////////////////////////////////////////////////////////////////////
// P600 interrupts
////////////////////////////////////////////////////////////////////////////////

void synth_uartInterrupt(void)
{
    uart_update();
}

// 2Khz
void synth_timerInterrupt(void)
{
    uint32_t va, vf;
    int16_t pitchALfoVal,pitchBLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
    uint16_t ampLfoVal;
    int8_t v,hz63,hz250;

    static uint8_t frc=0;

    sh_nextTick(); // advances the periodic sample-and-hold refresh (see sh.c)
    // performance
	// static uint16_t frc2=0; // for performance measurement

    // lfo

    lfo_update(&synth.lfo);


    pitchALfoVal=pitchBLfoVal=0;
    if (currentPreset.steppedParameters[spVibTarget]==2) // VCO A
    {
        pitchALfoVal=synth.vibPitch;
    }
    if (currentPreset.steppedParameters[spVibTarget]==3) // VCO B
    {
        pitchBLfoVal=synth.vibPitch;
    }
    if (currentPreset.steppedParameters[spVibTarget]==0) // VCO A & B
    {
        pitchALfoVal=pitchBLfoVal=synth.vibPitch;
    }
    ampLfoVal=synth.vibAmp;
    filterLfoVal=0;

    if(currentPreset.steppedParameters[spLFOTargets]&mtVCO)
    {
        if(!(currentPreset.steppedParameters[spLFOTargets]&mtOnlyB))
            pitchALfoVal+=synth.lfo.output>>1;
        if(!(currentPreset.steppedParameters[spLFOTargets]&mtOnlyA))
            pitchBLfoVal+=synth.lfo.output>>1;
    }

    if(currentPreset.steppedParameters[spLFOTargets]&mtVCF)
        filterLfoVal=synth.lfo.output;

    if(currentPreset.steppedParameters[spLFOTargets]&mtVCA)
    {
        ampLfoVal=scaleU16U16(ampLfoVal, synth.lfo.output+(UINT16_MAX-(synth.lfo.levelCV>>1)));
    }


    // global env computations
    vf=currentPreset.continuousParameters[cpFilEnvAmt];
    vf+=INT16_MIN;
    filEnvAmt=vf;
    oscEnvAmt=0;
    if(currentPreset.steppedParameters[spPModFA])
    {
        va=currentPreset.continuousParameters[cpPModFilEnv];
        va+=INT16_MIN;
        va/=2; // half strength
        oscEnvAmt=va;
    }

    // where the poly-mod envelope goes as pitch (the pulse width targets 3..5 are handled in refreshPulseWidth)
    pmToA=(currentPreset.steppedParameters[spPolyModEnv]==0 || currentPreset.steppedParameters[spPolyModEnv]==2);
    pmToB=(currentPreset.steppedParameters[spPolyModEnv]==1 || currentPreset.steppedParameters[spPolyModEnv]==2);

    // per voice stuff

    if(synth.ensembleActive)
    {
        // ensemble vibrato: every voice has its own vibrato pitch on top of the common pitch modulation.
        // The calls are written out like the ones below (and not in a loop): with a constant voice number the compiler can work out the
        // addresses and the bit masks of the voice at compile time, a loop would cost a lot of time in this interrupt.
        refreshVoice(0,oscEnvAmt,filEnvAmt,satS16((int32_t)pitchALfoVal+synth.ensPitchA[0]),satS16((int32_t)pitchBLfoVal+synth.ensPitchB[0]),filterLfoVal,ampLfoVal);
        refreshVoice(1,oscEnvAmt,filEnvAmt,satS16((int32_t)pitchALfoVal+synth.ensPitchA[1]),satS16((int32_t)pitchBLfoVal+synth.ensPitchB[1]),filterLfoVal,ampLfoVal);
        refreshVoice(2,oscEnvAmt,filEnvAmt,satS16((int32_t)pitchALfoVal+synth.ensPitchA[2]),satS16((int32_t)pitchBLfoVal+synth.ensPitchB[2]),filterLfoVal,ampLfoVal);
        refreshVoice(3,oscEnvAmt,filEnvAmt,satS16((int32_t)pitchALfoVal+synth.ensPitchA[3]),satS16((int32_t)pitchBLfoVal+synth.ensPitchB[3]),filterLfoVal,ampLfoVal);
        refreshVoice(4,oscEnvAmt,filEnvAmt,satS16((int32_t)pitchALfoVal+synth.ensPitchA[4]),satS16((int32_t)pitchBLfoVal+synth.ensPitchB[4]),filterLfoVal,ampLfoVal);
        refreshVoice(5,oscEnvAmt,filEnvAmt,satS16((int32_t)pitchALfoVal+synth.ensPitchA[5]),satS16((int32_t)pitchBLfoVal+synth.ensPitchB[5]),filterLfoVal,ampLfoVal);
    }
    else
    {
        // SYNTH_VOICE_COUNT calls (the normal case, nothing changed here)
        refreshVoice(0,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
        refreshVoice(1,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
        refreshVoice(2,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
        refreshVoice(3,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
        refreshVoice(4,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
        refreshVoice(5,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
    }

    // bit inputs (footswitch / tape in)

    handleBitInputs();

    // slower updates

    hz63=(frc&0x1c)==0;
    hz250=(frc&0x04)==0;

    switch(frc&0x03) // 4 phases, each 500hz
    {
    case 0:
        if(hz63)
            handleFinishedVoices();

        // MIDI processing
        midi_update(0);

        // ticker inc
        ++currentTick;

        break;
    case 1:

        // sequencer & arpeggiator

        if(settings.syncMode==smInternal || synth.pendingExtClock)
        {
            if(synth.pendingExtClock)
                --synth.pendingExtClock;

            arp_clockTick(); // plays an arp step that swing has delayed, once per clock unit

            if (clock_update())
            {

                // sync of the LFO using the clockBar counter

                synth.clockBar=(synth.clockBar+1)%0x9; // make sure the counter stays within the counter range, here 0...9
                if (currentPreset.steppedParameters[spLFOSync]>1)
                {
                    if(seq_getMode(0)!=smOff || seq_getMode(1)!=smOff || arp_getMode()!=amOff)
                    {
                        if ((synth.clockBar==8 && currentPreset.steppedParameters[spLFOSync]==8) || synth.clockBar+1==currentPreset.steppedParameters[spLFOSync])
                        {
                            synth.clockBar=0;
                            lfo_resetPhase(&synth.lfo);
                        }
                    }
                }

                // sequencer

                if(seq_getMode(0)!=smOff || seq_getMode(1)!=smOff)
                    seq_update();

                // arpeggiator

                if(arp_getMode()!=amOff)
                    arp_update();
            }
        }

        // glide

        if(synth.gliding)
        {
            for(v=0; v<SYNTH_VOICE_COUNT; ++v)
            {
                computeGlide(&synth.oscANoteCV[v],synth.oscATargetCV[v],synth.glideAmount);
                computeGlide(&synth.oscBNoteCV[v],synth.oscBTargetCV[v],synth.glideAmount);
                computeGlide(&synth.filterNoteCV[v],synth.filterTargetCV[v],synth.glideAmount);
            }
        }

        break;
    case 2:
        lfo_update(&synth.vibrato);
        if (currentPreset.steppedParameters[spVibTarget]==1) // VCA
        {
            synth.vibAmp=synth.vibrato.output+(UINT16_MAX-(synth.vibrato.levelCV>>1));
            synth.vibPitch=0;
        }
        else // VCO, A, B (and the new targets, they do not use the pitch)
        {
            synth.vibPitch=synth.vibrato.output>>2;
            synth.vibAmp=UINT16_MAX;
            if(currentPreset.steppedParameters[spVibTarget]>=4 && currentPreset.steppedParameters[spVibTarget]!=6) // noise level, resonance, poly-mod amount
                vibratoCvTarget(currentPreset.steppedParameters[spVibTarget]);
        }
        ensembleUpdate(); // the ensemble vibrato (does nothing unless it is selected and possible, see there)
        refreshPulseWidth(currentPreset.steppedParameters[spLFOTargets]&mtPW,oscEnvAmt);
        break;
    case 3:
        if(hz250)
        {
            // V2.5 (Teensy): keys/buttons and display as in V2.34 (scan, then display, at 250Hz; the buttons at 63Hz). The Flat-Labs
            // build scanned 2 bytes on every tick and switched the display column on again after each scan (scanner_updateSlice and
            // display_restoreColumn); on the Teensy the display then stayed dark and the buttons did not react.
            scanner_update(hz63);
            display_update(hz63);
            // V2.5: complete the last display write now (a write reaches its latch only when the next bus access starts, see
            // display_restoreColumn). The light show only became visible on the Teensy with this read after each refresh.
            BLOCK_INT
            {
                io_read(CSI1);
            }
            if (hz63)
                ui_update();
        }
        break;
    }

    ++frc;

    // for performance measurement
    // frc2=(frc2+1)%0x07d0; // 1 second
    //frc2=(frc2+1)%0x03e8; // 1/2 second
    //frc2=(frc2+1)%0x07d; // 1/16 second

    /*if(frc2==0)
    {
        midi_sendThreeBytes(0,(uint16_t)synth.updateCounter);
        synth.updateCounter=0;
    }*/
}

////////////////////////////////////////////////////////////////////////////////
// P600 internal events
////////////////////////////////////////////////////////////////////////////////

void LOWERCODESIZE synth_buttonEvent(p600Button_t button, int pressed)
{
    ui_handleButton(button,pressed);
}

void synth_keyEvent(uint8_t key, int pressed, int fromKeyboard, uint16_t velocity)
{

    if (ui.isShifted || ui.isDoubleClicked)
    {
        // keyboard transposition
        if(pressed && (fromKeyboard || settings.midiMode==1)) // support transpose by MIDI key event only for local off
        {
            char s[16]="trn = ";

            synth.transpose=(int8_t)key-SCANNER_C2;
            seq_setTranspose(synth.transpose);
            arp_setTranspose(synth.transpose);

            itoa(synth.transpose,&s[6],10);
            sevenSeg_scrollText(s,1);

            // Disable double-click transpose if transpose is
            // set using FROM TAPE as shift.
            // The point of this is to allow user an easy way
            // out of the toggled double click mode.
            if (ui.isShifted)
                ui.isDoubleClicked=0;
        }
    }
    else
    {
        // sequencer start
        if(pressed)
            for(int8_t track=0; track<SEQ_TRACK_COUNT; ++track)
                if(seq_getMode(track)==smWaiting)
                {
                    seq_setMode(track,smPlaying);
                    refreshSevenSeg();
                }

        if(arp_getMode()==amOff || (!fromKeyboard && settings.midiMode==0)) // in local on mode external MIDI always plays synth, not arp
        {
            // sequencer note input
            if(seq_getMode(0)==smRecording || seq_getMode(1)==smRecording)
            {
                if (!fromKeyboard || settings.midiMode==0) // don't input to sequencer directly from keyboard in local off mode
                {
                    seq_inputNote(key, pressed);
                    refreshSevenSeg();
                }
            }

            // set velocity to half (corresponding to MIDI value 64)
            if (settings.midiMode==0 && fromKeyboard) // only play from keyboard if not in local off mode
            {
                assigner_assignNote(key+synth.transpose,pressed,HALF_RANGE,1);
            }
            else if (!fromKeyboard) // it comes from MIDI with velocity and it will be played in all modes
            {
                assigner_assignNote(key,pressed,velocity,0);
            }
            // pass to MIDI out
            if (fromKeyboard) kbdMidiPush(key+synth.transpose,pressed);
        }
        else if ((settings.midiMode==0 && fromKeyboard) || (settings.midiMode==1 && !fromKeyboard))
        {
            arp_assignNote(key,pressed);
        }
        else if (fromKeyboard && settings.midiMode==1) // this is the substitute for keyboard into arp in local off mode (can be received as MIDI in)
        {
            kbdMidiPush(key+synth.transpose,pressed);
        }
    }
}

void synth_resetForLocalOffMode(void)
{
    assigner_allVoicesDone();
    synth_wheelEvent(0, 0, 1, 0, 0);
    synth_wheelEvent(0, 0, 1, 1, 0);
    synth_wheelEvent(0, 0, 2, 0, 0);
}

void synth_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity, int8_t legato)
{
    uint16_t velAmt;

    // mod delay

    refreshModDelayLFORetrigger(0);

    // prepare CVs

    computeTunedCVs(1,voice);

    // a new assignment or a release ends the poly aftertouch of the voice (the next A0 message of the new note starts it again)
    if(voice>=0)
    {
        BLOCK_INT
        {
            synth.polyATCV[voice]=0;
        }
    }

    // set gates (don't retrigger gate, unless we're arpeggiating)

    if(!legato || arp_getMode()!=amOff)
    {
        adsr_setGate(&synth.filEnvs[voice],gate);
        adsr_setGate(&synth.ampEnvs[voice],gate);

        // ensemble vibrato: a new note restarts the vibrato of its voice at the offset of that voice
        if(gate && synth.ensembleActive)
            synth.ensPhase[voice]=synth.ensStep*(uint8_t)voice;
    }

    if(gate)
    {
        // handle velocity

        velAmt=currentPreset.continuousParameters[cpFilVelocity];
        adsr_setCVs(&synth.filEnvs[voice],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
        velAmt=currentPreset.continuousParameters[cpAmpVelocity];
        adsr_setCVs(&synth.ampEnvs[voice],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
    }

#ifdef DEBUG
    print("assign note ");
    phex(note);
    print("  gate ");
    phex(gate);
    print(" voice ");
    phex(voice);
    print(" velocity ");
    phex16(velocity);
    print("\n");
#endif
}

void synth_uartEvent(uint8_t data)
{
    midi_newData(data);
}

static void retuneLastNotePressed(int16_t bend, uint16_t modulation, uint8_t mask)
{
    uint8_t note = 0;

    if (assigner_getLatestNotePressed(&note))
    {
        uint8_t scaleDegree = note % TUNER_NOTE_COUNT;
        double numSemitones = scaleDegree;

        if(mask&1)
        {
            // TODO: pitch wheel / bend should set coarse tuning
            return;
        }

        if(mask&2)
        {
            // TODO: mod wheel should provide a last-position-relative 'nudge' fine tuning
            // but currently it works like the pitch-wheel will in the future:
            // absolute adjusts +/- 1 semitone from Equal Tempered

            numSemitones = (modulation * (1.0f / UINT16_MAX)) + (((double)scaleDegree)-0.5f);
            tuner_setNoteTuning(scaleDegree, numSemitones);
            computeTunedOffsetCVs();
            computeBenderCVs();
            computeTunedCVs(1,-1);
        }
    }
}

// Mod wheel response curve. This is the original mapping from synth_wheelEvent(), moved here unchanged so that mod wheel (MIDI CC1 and the
// panel wheel) and channel aftertouch always follow the same range and target settings. modulation is the raw 0..65535 position.
static uint16_t mapModAmount(uint16_t modulation)
{
    static const int8_t mr[]={5,3,1,0}; // bits shifts right on full range
    uint8_t modBitShift;

    if (currentPreset.steppedParameters[spModwheelTarget]==1 && currentPreset.steppedParameters[spVibTarget]==1)
    {
        // full strength for vib VCA modulation
        return ((uint16_t)((expf(((float)modulation)/30000.0f )-1.0f)*8310.08f));
    }

    // half strength for VCO modulation
    modBitShift=mr[currentPreset.steppedParameters[spModWheelRange]];
    if (currentPreset.steppedParameters[spModWheelRange]<=1)
    {
        return (((uint16_t)((expf(((float)modulation)/30000.0f )-1.0f)*8310.08f))>>modBitShift);
    }
    else if(currentPreset.steppedParameters[spModWheelRange]==2)
    {
        return (((uint16_t)((expf(((float)modulation)/17000.0f )-1.0f)*1417.6f))>>modBitShift);
    }
    return ((uint16_t)((expf(((float)modulation)/14000.0f )-1.0f)*613.12f));
}

// The settings that change the mod wheel mapping, packed in a byte (range 0..3, then target==1 and vib target==1). Used to notice
// when the cached aftertouch mapping (synth.atMapped) is out of date.
static uint8_t modMapKey(void)
{
    return currentPreset.steppedParameters[spModWheelRange]
           |((currentPreset.steppedParameters[spModwheelTarget]==1)?4:0)
           |((currentPreset.steppedParameters[spVibTarget]==1)?8:0);
}

void synth_wheelEvent(int16_t bend, uint16_t modulation, uint8_t mask, int8_t isInternal, int8_t outputToMidi)
{
    // (the mod wheel range table and shift now live in mapModAmount())

    if (ui.retuneLastNotePressedMode) // all MIDI to bend and mod wheel are disabled in this mode
    {
        if (isInternal)
            retuneLastNotePressed(bend, modulation, mask);
        return;
    }

    if(mask&1)
    {
        if (isInternal && settings.midiMode==0) // only apply if not in local on mode)
        {
            synth.benderAmountInternal=bend>>1;
        }
        else if (!isInternal)
        {
            synth.benderAmountExternal=bend>>1;
        }
        computeBenderCVs();
        addWheelToTunedCVs();
    }

    if(mask&2)
    {
        if (settings.midiMode==0 || !isInternal)
        {
            //synth.modwheelAmount=modulation;
            // The mapping (range and target dependent curve) now lives in mapModAmount() so channel aftertouch can use exactly the same one.
            // The mod wheel result is kept apart (mwMapped) and added to the aftertouch part (atMapped), so one never cancels the other.
            synth.mwMapped=mapModAmount(modulation);
            if(synth.atMapKey!=modMapKey()) // range or target changed since aftertouch was mapped: map it again with the new setting
            {
                synth.atMapped=mapModAmount(synth.atRaw);
                synth.atMapKey=modMapKey();
            }
            synth.modwheelAmount=satAddU16U16(synth.mwMapped,synth.atMapped);
            refreshLfoSettings();
        }
    }

    // pass to MIDI out
    if(outputToMidi) //  event is sent to MIDI
        midi_sendWheelEvent(bend,modulation,mask);

}

// Channel aftertouch (MIDI D0). It does what the mod wheel does: same range, same target, same curve (mapModAmount), and it adds to
// the mod wheel instead of replacing it. value is the MIDI value 0..127, scaled like CC1 (value<<9).
void synth_aftertouchEvent(uint8_t value)
{
    uint16_t raw=((uint16_t)value)<<9;

    if (ui.retuneLastNotePressedMode) // like the wheels, aftertouch does nothing in this mode
        return;

    if (raw==synth.atRaw && synth.atMapKey==modMapKey()) // nothing changed (pressure often repeats), skip the expensive mapping
        return;

    synth.atRaw=raw;
    synth.atMapped=mapModAmount(raw);
    synth.atMapKey=modMapKey();
    synth.modwheelAmount=satAddU16U16(synth.mwMapped,synth.atMapped);
    refreshLfoSettings();
}

// Poly aftertouch (MIDI A0): the pressure of one note opens the filter of the voice that plays it. Full pressure (127) is +3 octaves
// of cutoff on top of everything else (the sum is limited by filterMaxCV in refreshVoice). Only a voice that is currently holding the
// note (gate on) takes it, the pressure goes back to 0 whenever the voice gets a new assignment or is released (synth_assignerEvent).
void synth_polyAftertouchEvent(uint8_t note, uint8_t value)
{
    int8_t v;
    uint8_t n;
    uint32_t cv;
    int8_t stage;

    if (ui.retuneLastNotePressedMode)
        return;

    for(v=0; v<SYNTH_VOICE_COUNT; ++v)
    {
        // in unison several voices play the same note, they all get the pressure
        if(!assigner_getAssignment(v,&n) || n!=note)
            continue;

        stage=adsr_getStage(&synth.ampEnvs[v]);
        if(stage<sAttack || stage>sSustain) // note was released already (a late message), ignore it
            continue;

        cv=((uint32_t)synth.filOctaveCV[v]*3*value)/127;
        if(cv>UINT16_MAX)
            cv=UINT16_MAX;

        BLOCK_INT // refreshVoice reads this in the timer interrupt, a 16 bit store is not atomic on the AVR
        {
            synth.polyATCV[v]=cv;
        }
    }
}

void synth_realtimeEvent(uint8_t midiEvent)
{
    if(settings.syncMode!=smMIDI)
        return;

    switch(midiEvent)
    {
    case MIDI_CLOCK:
        ++synth.pendingExtClock;
        break;
    case MIDI_START:
        seq_resetCounter(0,0);
        seq_resetCounter(1,0);
        arp_resetCounter(0);
        clock_reset(); // always do a beat reset on MIDI START
        synth.pendingExtClock=0;
        break;
    case MIDI_STOP:
        seq_silence(0);
        seq_silence(1);
        break;
    }
}

void synth_holdEvent(int8_t hold, int8_t sendMidi, uint8_t isInternal)
{
    if (currentPreset.steppedParameters[spUnison])
    {
        // this is a hold / latch event
        if (hold && isInternal) // latching in unison mode is only done on hold change to "on". This is never sent to MIDI but it is applied in local off mode
        {

            assigner_latchPattern(1);
            assigner_getPattern(currentPreset.voicePattern,NULL); // this is stored in the patch
            // never send MIDI
        }
    }
    else
    {
        if (settings.midiMode==0 || !isInternal) assigner_holdEvent(hold); // only applied in local on mode
        if (sendMidi) midi_sendSustainEvent(hold); // add this here for Midi sustain output
    }
}

void synth_volEvent(uint16_t value) // Added for MIDI Volume, this overrides the MVol value set by the panel pot (and vice versa)
{
    synth.masterVolume=value;
}


void mixer_updatePanelLayout(uint8_t layout)
{
    if (layout==0) // switch from SCI layout (mix and drive) to GliGli (vol A & B)
    {
        // copy over the volumes to the pot parameters for pick-me-up logic
        currentPreset.continuousParameters[cpMixVolA]=currentPreset.continuousParameters[cpVolA];
        currentPreset.continuousParameters[cpGlideVolB]=currentPreset.continuousParameters[cpVolB];
    }
    else // switch from GliGli (vol A & B) layout to SCI (mix and drive)
    {
        // copy over the volumes to the pot parameters for pick-me-up logic
        currentPreset.continuousParameters[cpMixVolA]=mixer_mixFromVols(currentPreset.continuousParameters[cpVolA], currentPreset.continuousParameters[cpVolB]);
        currentPreset.continuousParameters[cpGlideVolB]=currentPreset.continuousParameters[cpGlide];
        currentPreset.continuousParameters[cpDrive]=mixer_driveFromVols(currentPreset.continuousParameters[cpVolA], currentPreset.continuousParameters[cpVolB]);
    }
}
