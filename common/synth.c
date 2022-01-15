////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

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

#define POT_DEAD_ZONE 512

// Dead band is distance from center of pot to end of dead band area,
// in either direction.
#define BEND_DEADBAND 3500 //3072 V2.30 increased deadband to clean up noise in some pitchbend wheels when centered (change from J. Sepulveda)
// Guard band is distance from the end of pot travel until we start
// reacting. Compensates for the fact that the bend pot cannot reach
// especially the maximum positive voltage.
#define BEND_GUARDBAND 400

#define PANEL_DEADBAND 2048

// The P600 VCA completely closes before the CV reaches 0, this accounts for it
#define VCA_DEADBAND 768

#define BIT_INTPUT_FOOTSWITCH 0x20
#define BIT_INTPUT_TAPE_IN 0x01

uint8_t tempBuffer[TEMP_BUFFER_SIZE]; // general purpose chunk of RAM

const p600Pot_t continuousParameterToPot[cpCount]=
{
    ppFreqA,ppMixer,ppAPW,
    ppFreqB,ppGlide,ppBPW,ppFreqBFine,
    ppCutoff,ppResonance,ppFilEnvAmt,
    ppFilRel,ppFilSus,ppFilDec,ppFilAtt,
    ppAmpRel,ppAmpSus,ppAmpDec,ppAmpAtt,
    ppPModFilEnv,ppPModOscB,
    ppLFOFreq,ppLFOAmt,
    ppNone,ppNone,ppNone,ppNone,
    ppNone,ppNone,ppNone,ppNone,ppNone,ppNone
};

const uint8_t potToCP[32]=
{
    cpVolA, cpCutoff,cpResonance,cpFilEnvAmt,cpFilRel,cpFilSus,
	cpFilDec,cpFilAtt,cpAmpRel,cpAmpSus,cpAmpDec,cpAmpAtt,
	cpVolB,cpBPW,
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

    // store slowly changing partial results so that specific updates can be made faster
    int32_t tunedBenderCVs[pcOsc6B-pcOsc1A+1];
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

    uint16_t modwheelAmount;
    int16_t benderAmountInternal;
    int16_t benderAmountExternal;
    uint16_t masterVolume;
    int16_t benderCVs[pcFil6-pcOsc1A+1];
    int16_t benderVolumeCV;

    int16_t glideAmount;
    int8_t gliding;

    uint32_t modulationDelayStart;
    uint16_t modulationDelayTickCount;

    uint8_t pendingExtClock;

    int8_t transpose;

    int8_t clockBar;

} synth;

extern void refreshAllPresetButtons(void);
extern const uint16_t attackCurveLookup[]; // for modulation delay

struct deadband {
    uint16_t middle;
    uint16_t guard;
    uint16_t deadband;
    uint32_t precalcLow;
    uint32_t precalcHigh;
};

struct deadband bendDeadband = { HALF_RANGE, BEND_GUARDBAND,  BEND_DEADBAND };
struct deadband panelDeadband = { HALF_RANGE, 0, PANEL_DEADBAND };

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

        if(synth.gliding)
        {
            synth.oscATargetCV[v]=cva;
            synth.oscBTargetCV[v]=cvb;
            synth.filterTargetCV[v]=cvf;

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


static void computeTunedBenderCVs(void) // this function must always be called after tuning or after the semitones setting of the bender changes (inkl. preset change)
{
    p600CV_t cv;
    for(cv=pcOsc1A; cv<=pcOsc6B; ++cv)
    {
        synth.tunedBenderCVs[cv]=tuner_computeCVFromNote(currentPreset.steppedParameters[spBenderSemitones]*4,0,cv)-tuner_computeCVFromNote(0,0,cv);
    }
}

static void computeTunedCVs(int8_t force, int8_t forceVoice)
{

    // with this function the folowing changes are reflected in the CVs
    // master Tune, fine Tune OSC B
    // filter cut-off, frequency settings of OSC A and B (inkl. mode setting)
    // Tracking setting
    // Unison / detune
    // Bender
    // assigned notes
    //
    // --> all these are slowly changing, therefore the function can be called according to ressources
    // --> for bender events there is separate, reduced and faster function, because there can be a lot of these events

    uint16_t cva,cvb,cvf;
    uint8_t note=SCANNER_BASE_NOTE,baseCutoffNote;
    int8_t v, v_aux;

    uint16_t baseAPitch,baseBPitch,baseCutoff;
    int16_t mTune,fineBFreq,fineBFreqAdd,detune;

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

    if(chrom>0)
    {
        baseAPitch=0;
        baseBPitch=0;

        if(chrom>1)
        {
            baseANote-=baseANote%12;
            baseBNote-=baseBNote%12;
        }
    }
    else
    {
        baseAPitch&=0xff;
        baseBPitch&=0xff;
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

        detune=0;
        if (spreadRaw>1000) detune=(spreadRaw>>11); // this is spread detune, e.g. analog out of tune whack

        v_aux=v;
        synth.oscABaseCV[v]=satAddU16S16(tuner_computeCVFromNote(ANote,baseAPitch,pcOsc1A+v),(1+(v_aux>>1))*(v_aux&1?-1:1)*detune);
        v_aux=(v+3)&6;
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

        v_aux=(v+5)&6;
        synth.filterBaseCV[v]=satAddU16S16(tuner_computeCVFromNote(trackingNote,baseCutoff,pcFil1+v),(1+(v_aux>>1))*detune);

        // detune

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

        if(synth.gliding)
        {
            synth.oscATargetCV[v]=cva;
            synth.oscBTargetCV[v]=cvb;
            synth.filterTargetCV[v]=cvf;

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
static void precalcDeadband(struct deadband *d)
{
    uint16_t middleLow=d->middle-d->deadband;
    uint16_t middleHigh=d->middle+d->deadband;

    d->precalcLow=HALF_RANGE_L/(middleLow-d->guard);
    d->precalcHigh=HALF_RANGE_L/(FULL_RANGE-d->guard-middleHigh);
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
        return HALF_RANGE;
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
    computeTunedBenderCVs();
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

    // compute new
    bend=currentPreset.steppedParameters[spBenderSemitones];
    bend*=satAddS16S16(synth.benderAmountInternal,synth.benderAmountExternal);
    bend*=FULL_RANGE/12; // Fixed point /12 ...

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
                synth.benderCVs[cv]=bend>>16; // ... after >>16
            break;
        case modVCA:
            synth.benderVolumeCV=bend>>16; // ... after >>16
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


static void refreshModulationDelay(int8_t refreshTickCount)
{
    int8_t anyPressed, anyAssigned;
    static int8_t prevAnyPressed=0;

    anyPressed=assigner_getAnyPressed();
    anyAssigned=assigner_getAnyAssigned();

    if(!anyAssigned)
    {
        synth.modulationDelayStart=UINT32_MAX;
    }

    if(anyPressed && !prevAnyPressed)
    {
        synth.modulationDelayStart=currentTick;
    }

    prevAnyPressed=anyPressed;

    if(refreshTickCount)
        synth.modulationDelayTickCount=exponentialCourse(UINT16_MAX-currentPreset.continuousParameters[cpModDelay],12000.0f,2500.0f);
}

static void handleFinishedVoices(void)
{
    int8_t v;

    for(v=0; v<SYNTH_VOICE_COUNT; ++v)
    {
        // when amp env finishes, voice is done
        if(assigner_getAssignment(v,NULL) && adsr_getStage(&synth.ampEnvs[v])==sWait)
            assigner_voiceDone(v);

        // if voice isn't assigned, silence it
        if(!assigner_getAssignment(v,NULL) && adsr_getStage(&synth.ampEnvs[v])!=sWait)
        {
            adsr_reset(&synth.ampEnvs[v]);
            adsr_reset(&synth.filEnvs[v]);
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

static inline void refreshPulseWidth(int8_t pwm)
{
    int32_t pa,pb;

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

    if(sqrA)
        pa=currentPreset.continuousParameters[cpAPW];

    if(sqrB)
        pb=currentPreset.continuousParameters[cpBPW];

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

static void refreshLfoSettings(void)
{
    lfoShape_t shape;
    uint16_t mwAmt,lfoAmt,vibAmt,dlyAmt;
    uint32_t elapsed;

    shape=currentPreset.steppedParameters[spLFOShape];

    lfo_setShape(&synth.lfo,shape);

    // wait modulationDelayTickCount then progressively increase over
    // modulationDelayTickCount time, following an exponential curve
    dlyAmt=0;
    if(synth.modulationDelayStart!=UINT32_MAX)
    {
        if(currentPreset.continuousParameters[cpModDelay]<POT_DEAD_ZONE)
        {
            dlyAmt=UINT16_MAX;
        }
        else if(currentTick>=synth.modulationDelayStart+synth.modulationDelayTickCount)
        {
            elapsed=currentTick-(synth.modulationDelayStart+synth.modulationDelayTickCount);
            if(elapsed>=synth.modulationDelayTickCount)
                dlyAmt=UINT16_MAX;
            else
                dlyAmt=attackCurveLookup[(elapsed<<8)/synth.modulationDelayTickCount];
        }
    }

    // mod wheel shifts:
    mwAmt=((uint16_t)((expf(((float)synth.modwheelAmount)/14000.0f )-1.0f)*613.12f));

    lfoAmt=currentPreset.continuousParameters[cpLFOAmt];
    lfoAmt=(lfoAmt<POT_DEAD_ZONE)?0:(lfoAmt-POT_DEAD_ZONE);

    // now scale the LFO amount in analogy to mod wheel
    lfoAmt=((expf(((float)lfoAmt)/9000.0f )-1.0f)*45.121f);

    vibAmt=currentPreset.continuousParameters[cpVibAmt]>>2;
    vibAmt=(vibAmt<POT_DEAD_ZONE)?0:(vibAmt-POT_DEAD_ZONE);

    if(currentPreset.steppedParameters[spModwheelTarget]==0) // targeting lfo?
    {
        lfo_setCVs(&synth.lfo,
                   currentPreset.continuousParameters[cpLFOFreq],
                   satAddU16U16(lfoAmt,mwAmt));
        lfo_setCVs(&synth.vibrato,
                   currentPreset.continuousParameters[cpVibFreq],
                   scaleU16U16(vibAmt,dlyAmt));
    }
    else // targeting vibrato
    {
        lfo_setCVs(&synth.lfo,
                   currentPreset.continuousParameters[cpLFOFreq],
                   scaleU16U16(lfoAmt,dlyAmt));
        lfo_setCVs(&synth.vibrato,
                   currentPreset.continuousParameters[cpVibFreq],
                   satAddU16U16(vibAmt,mwAmt));
    }
}

static void refreshSevenSeg(void) // imogen: this function would be more suited for ui.c than synth.c
{
    int8_t seqRec = seq_getMode(0)==smRecording || seq_getMode(1)==smRecording;

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
        if(ui.lastActivePotValue>=0)
        {
            int32_t v;
            int8_t lastPotcP;
            lastPotcP=potToCP[ui.lastActivePot]; // this is -1 for anything not stored in a patch

            if(ui.lastActivePot==ppPitchWheel)
            {
                v=getAdjustedBenderAmount();
                v-=INT16_MIN;
            }
            else
            {
				v=ui.adjustedLastActivePotValue;
            }

            v=(v*100L)>>16; // 0..100 range

            if(potmux_isPotZeroCentered(ui.lastActivePot)) v=abs(v-50);

            if (lastPotcP<0) // it's a value not part of the currentPreset.cp, e.g. not stored in a patch; always show as is
            {
                led_set(plDot,v<0,0); // dot indicates negative
                sevenSeg_setNumber(v);
            }
            else if (currentPreset.contParamPotStatus[lastPotcP]==1) // it is stored in a patch but already picked up
            {
                led_set(plDot,v<0,0); // dot indicates negative
                sevenSeg_setNumber(v);
            }
            else if (currentPreset.contParamPotStatus[lastPotcP]>=2) // it is stored in a patch but not yet picked up
            {
                sevenSeg_setRelative(currentPreset.contParamPotStatus[lastPotcP]);
            }
        }
        else
        {
            led_set(plDot,0,0); // switch off dot in display in case it is on from before
            sevenSeg_setAscii(' ',' ');
        }
    }
    else // this is showing preset number or waiting for first (decade) or second (unit) digit either for preset selection or preset saving
    {
        if(ui.digitInput!=diLoadDecadeDigit)
        {
            if(ui.presetAwaitingNumber>=0)
                sevenSeg_setAscii('0'+ui.presetAwaitingNumber,' ');
            else
                sevenSeg_setAscii(' ',' ');
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
    refreshModulationDelay(1);
    refreshGates();
    refreshAssignerSettings();
    refreshLfoSettings();
    refreshEnvSettings();
    computeTunedBenderCVs();
    computeBenderCVs();
    refreshFilterMaxCV();

    refreshSevenSeg();
}

static void refreshPresetPots(int8_t force) // this only affects current preset parameters
{
    continuousParameter_t cp;

    for(cp=0; cp<cpCount; ++cp)
        if((continuousParameterToPot[cp]!=ppNone) && (force || continuousParameterToPot[cp]==ui.lastActivePot || potmux_hasChanged(continuousParameterToPot[cp])))
        {
            p600Pot_t pp=continuousParameterToPot[cp];
            uint16_t value=potmux_getValue(pp);

            if(potmux_isPotZeroCentered(pp))
                value=addDeadband(value,&panelDeadband);

            if (currentPreset.contParamPotStatus[cp]==1 || !(settings.presetMode && ui.digitInput<diLoadDecadeDigit)) // synth follows pot, picked up or live mode
            {
                currentPreset.continuousParameters[cp]=value;
                currentPreset.contParamPotStatus[cp]=1;
                ui.presetModified=1;
            }
            else // pot is still off
            {
                //if ((currentPreset.continuousParameters[cp]>>8)==(value>>8) || comparePotVal(pp, value, currentPreset.continuousParameters[cp]))
                if ((currentPreset.continuousParameters[cp]>>9)==(value>>9)) // pick up pot when close enough
                {
                    currentPreset.contParamPotStatus[cp]=1;
                    currentPreset.continuousParameters[cp]=value;
                }
                else
                {
                    currentPreset.contParamPotStatus[cp]=currentPreset.continuousParameters[cp]>value?2:3; // 2 means pot is lower, 3 means pot is higher
                }
            }
        }
}

void refreshPresetMode(void)
{
    if(!preset_loadCurrent(settings.presetMode?settings.presetNumber:MANUAL_PRESET_PAGE,0))
        preset_loadDefault(1);

    if(!settings.presetMode)
    {
        refreshAllPresetButtons();
        refreshPresetPots(1);
    }

    ui_setNoActivePot();
    ui.presetModified=0;
    ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;

}

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

        va=pitchALfoVal;
        vb=pitchBLfoVal;

        // osc B

        vb+=synth.oscBNoteCV[v];
        sh_setCV32Sat_FastPath(pcOsc1B+v,vb);

        // osc A

        if (currentPreset.steppedParameters[spEnvRouting]==0) // the normal case
            va+=scaleU16S16(envVal,oscEnvAmt);
        else // all other cases
            va+=scaleU16S16(ampEnvVal,oscEnvAmt);

        va+=synth.oscANoteCV[v];
        sh_setCV32Sat_FastPath(pcOsc1A+v,va);

        // filter

        vf=filterLfoVal;
        vf+=scaleU16S16(envVal,filEnvAmt);
        vf+=synth.filterNoteCV[v];

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
        cur=io_read(0x9);
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

void synth_init(void)
{
    int8_t i;

    // init

    memset(&synth,0,sizeof(synth));

    scanner_init();
    display_init();
    sh_init();
    potmux_init();
    tuner_init();
    assigner_init();
    uart_init();
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

    // go in scaling adjustment mode if needed

    if(io_read(0x9)&16)
        tuner_scalingAdjustment();

    // manual preset

    if(!preset_loadCurrent(MANUAL_PRESET_PAGE,0))
    {
        preset_loadDefault(0);
        preset_saveCurrent(MANUAL_PRESET_PAGE);
    }

    // load settings from storage; tune when they are bad

    if(!settings_load())
    {
        settings_loadDefault();

#ifndef DEBUG
        tuner_tuneSynth();
#endif
    }

    sh_setCV(pcMVol,HALF_RANGE,SH_FLAG_IMMEDIATE);
    settings_save();

    // initial input state

    scanner_update(1);
    //potmux_update(POTMUX_POT_COUNT); // init all
    potmux_update(1); // init all

    // dead band pre calculation
    precalcDeadband(&panelDeadband);
    bendDeadband.middle=settings.benderMiddle;
    precalcDeadband(&bendDeadband);

    // load last preset & do a full refresh

    refreshPresetMode();
    refreshFullState();
    computeTunedCVs(-1,-1); // force init CV's for all voices

    // set the volume to the current pot value
    synth.masterVolume=potmux_getValue(ppMVol);

    // a nice welcome message, and we're ready to go :)

    sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void synth_update(void)
{
    int32_t potVal;
    static uint8_t frc=0;

    // toggle tape out (debug)

    BLOCK_INT
    {
        ++frc;
        io_write(0x0e,((frc&1)<<2)|0b00110001);
    }

    // update pots, detecting change

    potmux_resetChanged();
    //potmux_update(4);
    potmux_update(0);

    // act on pot change

    ui_checkIfDataPotChanged(); // this sets ui.lastActivePot and handles the menu parameters

    refreshPresetPots(!settings.presetMode); // this updates currentPreset.cp[] if "forced" (function parameter), lastActivePot or changed

    if(ui.lastActivePot!=ppNone)
    {
        potVal=potmux_getValue(ui.lastActivePot);
        if(potVal!=ui.lastActivePotValue)
        {
            ui.lastActivePotValue=potVal;
            ui.adjustedLastActivePotValue=potVal;

            if(potmux_isPotZeroCentered(ui.lastActivePot))
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
            else if (ui.lastActivePot==ppMVol)
                synth.masterVolume = potmux_getValue(ppMVol);
        }
    }

    // lfo (for mod delay)
    refreshLfoSettings();
    sh_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],SH_FLAG_IMMEDIATE);
    sh_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SH_FLAG_IMMEDIATE);
    sh_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],SH_FLAG_IMMEDIATE);
    switch(frc&0x03) // 4 phases
    {
        case 0:
            sh_setCV(pcResonance,currentPreset.continuousParameters[cpResonance],SH_FLAG_IMMEDIATE);
            // arp and seq
            clock_setSpeed(settings.seqArpClock);
            break;
        case 1:
            // 'fixed' CVs
            sh_setCV(pcExtFil,currentPreset.continuousParameters[cpExternal],SH_FLAG_IMMEDIATE);
            break;
        case 2:
            // 'fixed' CVs
            sh_setCV(pcMVol,satAddU16S16(synth.masterVolume,synth.benderVolumeCV),SH_FLAG_IMMEDIATE);
            break;
        case 3:
            // gates

            refreshGates();

            // glide
            synth.glideAmount=exponentialCourse(currentPreset.continuousParameters[cpGlide],11000.0f,2100.0f);
            synth.gliding=synth.glideAmount<2000;
            break;
    }

    // tuned CVs

    computeTunedCVs(0,-1);
}

void synth_tuneSynth(void)
{
    tuner_tuneSynth();
    computeTunedBenderCVs();
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
    int32_t va,vf;
    int16_t pitchALfoVal,pitchBLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
    uint16_t ampLfoVal;
    int8_t v,hz63,hz250;

    static uint8_t frc=0;

    // lfo

    lfo_update(&synth.lfo);

    pitchALfoVal=pitchBLfoVal=0;
    filterLfoVal=0;
    ampLfoVal=UINT16_MAX;

    if (currentPreset.steppedParameters[spVibTarget]==0)
    {
        pitchALfoVal=pitchBLfoVal=synth.vibrato.output;
    }
    else
    {
        ampLfoVal+=(synth.vibrato.output<<2)-(synth.vibrato.levelCV<<1);
    }

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
        ampLfoVal+=synth.lfo.output-(synth.lfo.levelCV>>1);



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

    // per voice stuff

    // SYNTH_VOICE_COUNT calls
    refreshVoice(0,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
    refreshVoice(1,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
    refreshVoice(2,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
    refreshVoice(3,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
    refreshVoice(4,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);
    refreshVoice(5,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal,ampLfoVal);

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
        // bit inputs (footswitch / tape in)

        handleBitInputs();

        // sequencer & arpeggiator

        if(settings.syncMode==smInternal || synth.pendingExtClock)
        {
            if(synth.pendingExtClock)
                --synth.pendingExtClock;

            if (clock_update())
            {

                // sync of the LFO using the clockBar counter

                synth.clockBar=(synth.clockBar+1)%0x9; // make sure the counter stays within the counter range, here 0...9
                if((seq_getMode(0)!=smOff || seq_getMode(1)!=smOff || arp_getMode()!=amOff) && currentPreset.steppedParameters[spLFOSync]!=0)
                {
                    if ((synth.clockBar==8 && currentPreset.steppedParameters[spLFOSync]==7) || synth.clockBar==currentPreset.steppedParameters[spLFOSync])
                    {
                        synth.clockBar=0;
                        lfo_resetPhase(&synth.lfo);
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
        refreshPulseWidth(currentPreset.steppedParameters[spLFOTargets]&mtPW);
        break;
    case 3:
        if(hz250)
        {
            scanner_update(hz63);
            display_update(hz63);
            if (hz63)
                ui_update();
        }
        break;
    }

    ++frc;
}

////////////////////////////////////////////////////////////////////////////////
// P600 internal events
////////////////////////////////////////////////////////////////////////////////

void LOWERCODESIZE synth_buttonEvent(p600Button_t button, int pressed)
{
    ui_handleButton(button,pressed);
}

void synth_keyEvent(uint8_t key, int pressed, int sendMidi, int fromKeyboard, uint16_t velocity)
{
    if (ui.isShifted || ui.isDoubleClicked)
    {
        // keyboard transposition
        if(pressed && fromKeyboard) // don't support transpose by MIDI key event
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

        if(arp_getMode()==amOff)
        {
            // sequencer note input
            if(seq_getMode(0)==smRecording || seq_getMode(1)==smRecording)
            {
                seq_inputNote(key, pressed);
                refreshSevenSeg();
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
            if (sendMidi) midi_sendNoteEvent(key+synth.transpose,pressed,HALF_RANGE);
        }
        else
        {
            arp_assignNote(key,pressed);
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

    refreshModulationDelay(0);

    // prepare CVs

    computeTunedCVs(1,voice);

    // set gates (don't retrigger gate, unless we're arpeggiating)

    if(!legato || arp_getMode()!=amOff)
    {
        adsr_setGate(&synth.filEnvs[voice],gate);
        adsr_setGate(&synth.ampEnvs[voice],gate);
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
            computeTunedBenderCVs();
            computeBenderCVs();
            computeTunedCVs(1,-1);
        }
    }
}

void synth_wheelEvent(int16_t bend, uint16_t modulation, uint8_t mask, int8_t isInternal, int8_t outputToMidi)
{
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
            synth.modwheelAmount=modulation;
            refreshLfoSettings();
        }
    }

    // pass to MIDI out
    if(outputToMidi) //  event is sent to MIDI
        midi_sendWheelEvent(bend,modulation,mask);

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
