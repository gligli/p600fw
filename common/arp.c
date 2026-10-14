////////////////////////////////////////////////////////////////////////////////
// Arpeggiator
////////////////////////////////////////////////////////////////////////////////

#include "arp.h"

#include "scanner.h"
#include "assigner.h"
#include "storage.h"
#include "midi.h"
#include "clock.h"
#include "seq.h"

#define ARP_NOTE_MEMORY 128 // must stay>=128 for up/down mode

// this flag is used to indicate which of the current arp notes are latched so that these can be
// removed when latch mode is deactivated while the currently held keys continue to be played:
#define ARP_NOTE_HELD_FLAG 0x80

#define ARP_LAST_NOTE (ARP_NOTE_MEMORY-1)

// Euclidean rhythms for the arpeggiator (menu: button 0, second press; stored in the preset as spArpEuclid).
// The arp normally plays a note on every clock step. With a Euclidean rhythm selected, only the steps whose bit is set
// in the mask play; the other steps are rests. A rest ends the previous note, plays nothing and does NOT advance the
// note sequence, so the arp carries on with the next note at the next hit.
// Bit n of a mask is step n of the rhythm (step 0 is the least significant bit). Every rhythm starts on a hit.
// Entry 0 means off: every step plays, exactly as before this feature existed, so older patches sound the same.
// Rhythms (x = hit), generated with Bjorklund's algorithm so the hits are spread as evenly as possible:
//   1: E(3,8)  x..x..x.            2: E(5,8)  x.xx.xx.            3: E(7,8)  xxxxxxx.
//   4: E(5,16) x..x..x..x..x...    5: E(7,16) x..x.x.x..x.x.x.
//   6: E(9,16) x.xx.x.x.xx.x.x.    7: E(11,16) x.xx.xx.xx.xx.xx
static const uint16_t euclidMask[ARP_EUCLID_PATTERNS]={0x0000,0x0049,0x006D,0x007F,0x1249,0x54A9,0x56AD,0xDB6D};
static const uint8_t euclidLength[ARP_EUCLID_PATTERNS]={0,8,8,8,16,16,16,16}; // number of steps in each rhythm

// Probability (spArpProbability; menu: button 0, third press). A step that is a hit only plays with this probability, otherwise it
// is a rest, exactly like a rest of the Euclidean rhythm (the note sequence does not advance). Choice 0 = 100%: always plays, and no
// random number is used, so the arp behaves as before. The step plays if a random byte is below the threshold (threshold/256):
//   choices: 100%, 90%, 80%, 70%, 60%, 50%, 35%, 20%
static const uint8_t probabilityThreshold[ARP_PROBABILITY_CHOICES]={0,230,205,179,154,128,90,51};

// Swing (spArpSwing; menu: button 0, fourth press). Every second step of the arp is delayed. The menu shows the swing as the usual
// percentage (50% = straight, 66% = triplet shuffle, 75% = very hard). A swing of s% starts the second step of each pair at s% of the
// pair, which is a delay of (s-50)*2 percent of one step. Choice 0 = off, the arp behaves as before.
//   choices: off, 54%, 58%, 62%, 66%, 70%, 75%, 80%
static const uint8_t swingDelayPercent[ARP_SWING_CHOICES]={0,8,16,24,32,40,50,60}; // delay in percent of a step

// Resolution (spArpResolution; menu: button 0, fifth press). The speed knob sets the clock step, which is a 16th note. The arp
// plays p steps in the time of q clock steps, so each choice is a fraction p/q of the clock speed:
//   choices: 1/16 (p=1,q=1: the clock step itself), 1/16 triplet (3 steps in 2 clock steps), 1/32 (2 in 1), 1/32 triplet (3 in 1)
// Choice 0 is driven by the clock steps exactly as before this feature existed. The other choices run a timer in arp_clockTick:
// every clock unit adds p to a counter, and a step is due when the counter reaches q times the clock step length. That spreads the
// steps evenly (no drift), even when the clock step is not a multiple of p units, and a step always lands on the clock grid after q clock steps.
// The arp never steps faster than ARP_MIN_STEP_UNITS (internal clock) or once per clock unit (MIDI clock), so the top of
// the speed knob saturates for the fast resolutions.
static const uint8_t resolutionP[ARP_RESOLUTION_CHOICES]={1,3,2,3};
static const uint8_t resolutionQ[ARP_RESOLUTION_CHOICES]={1,2,1,1};
#define ARP_MIN_STEP_UNITS 12 // 12 units of 2ms = 24ms: the fastest arp step with the internal clock
#define ARP_FIRE_NOW UINT32_MAX // arp.acc value: the next clock unit plays a step (the arp has just started and the clock was reset to match)
#define ARP_WAIT_CLOCK (UINT32_MAX-1) // arp.acc value: the arp has just started but the clock was not reset (external clock, or the sequencer is playing): the first step waits for the next clock step, so the arp stays on the clock grid

// the selected resolution, 0 if the stored value is not valid
static uint8_t arpResolution(void)
{
	uint8_t r=currentPreset.steppedParameters[spArpResolution];
	return (r<ARP_RESOLUTION_CHOICES)?r:0;
}

// length of one arp step multiplied by p, in clock units (500Hz ticks, or MIDI clock pulses when synced), for a resolution other than 0
static uint32_t arpInterval(uint8_t res)
{
	uint32_t interval=(uint32_t)resolutionQ[res]*clock_getSpeed();
	uint32_t minimum=(uint32_t)resolutionP[res]*((settings.syncMode==smInternal)?ARP_MIN_STEP_UNITS:1); // saturate: not faster than the minimum step
	return (interval<minimum)?minimum:interval;
}

// length of one arp step in clock units. Resolution 0 is the clock step itself (never limited, as before)
static uint16_t arpStepUnits(uint8_t res)
{
	if(res==0)
		return clock_getSpeed();

	return (uint16_t)(arpInterval(res)/resolutionP[res]);
}

// Ratchets (spArpRatchet and spArpRatchetProb; menu: button 0, sixth and seventh press). A ratcheted step plays its note n times
// (2, 3 or 4) evenly spread over the length of the step: the note is played, then ended and played again. Repeats use the same
// note and velocity (they are not quieter). Only steps that really play can ratchet: rests of a Euclidean rhythm and steps dropped
// by the probability stay silent. Choice 0 of spArpRatchet = off, the arp behaves as before.
// Not every step has to ratchet: the chance that a step is ratcheted is spArpRatchetProb, choice 0 = 100% (every step, and no random
// number is used). The step plays if a random byte is below the threshold (threshold/256):
//   choices: 100%, 80%, 65%, 50%, 40%, 30%, 20%, 10%
static const uint8_t ratchetProbThreshold[ARP_RATCHET_PROB_CHOICES]={0,205,166,128,102,77,51,26};

static struct
{
	uint8_t notes[ARP_NOTE_MEMORY];
	int16_t noteIndex;
	int16_t previousIndex; // this is used to avoid multiple successive playing of notes (or indices) in random arp
	uint8_t previousNote;
	int8_t transpose,previousTranspose;

	int8_t hold;
	arpMode_t mode;
	uint8_t euclidStep; // position inside the Euclidean rhythm, restarts with the arp (see arp_resetCounter)
	uint8_t swingOdd; // 1 when the next step is the second (delayed) step of a pair, restarts with the arp
	uint16_t swingCountdown; // clock units until a delayed step plays, 0 = no step is waiting
	uint32_t acc; // resolution timer: counts p for every clock unit, a step is due at q clock steps (ARP_FIRE_NOW = step at the next unit)
	uint8_t lastResolution; // the resolution that acc belongs to
	uint16_t pendingWindow; // for a step that swing has delayed: the clock units until the next step (see ratchets)
	uint8_t ratchetCount; // hits in the ratcheted step that is playing (2 to 4)
	uint8_t ratchetIndex; // the next repeat of that step (the first hit is 0)
	uint16_t ratchetWindow; // length of that step in clock units: the repeats are spread over it
	uint16_t ratchetCountdown; // clock units until the next repeat, 0 = no repeat is waiting
} arp;

static int8_t isEmpty(void)
{
	int16_t i;
	
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		if(arp.notes[i]!=ASSIGNER_NO_NOTE)
			return 0;

	return 1;
}

static void finishPreviousNote(void)
{
	if(arp.previousNote!=ASSIGNER_NO_NOTE)
	{
		uint8_t n=arp.previousNote&~ARP_NOTE_HELD_FLAG; // remove the HELD bit
		
		assigner_assignNote(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0,0);
		
		// pass to MIDI out
		if (settings.midiMode==0) midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0);
	}
}

static void killAllNotes(void)
{
	finishPreviousNote();

	arp.noteIndex=-1;
	arp.previousIndex=-1;
	arp.previousNote=ASSIGNER_NO_NOTE;
	arp.ratchetCountdown=0; // no repeats of notes that were just ended

	memset(arp.notes,ASSIGNER_NO_NOTE,ARP_NOTE_MEMORY);
	assigner_allKeysOff();
}

static void killHeldNotes(void)
{
	int16_t i;
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		if(arp.notes[i]&ARP_NOTE_HELD_FLAG)
			arp.notes[i]=ASSIGNER_NO_NOTE;
	
	// gate off for last note

	if(isEmpty())
		finishPreviousNote();
}

inline void arp_setMode(arpMode_t mode, int8_t hold)
{
	// stop previous assigned notes

	if(mode!=arp.mode)
	{
		if ((mode==amRandom && arp.mode==amAssign) || (arp.mode==amRandom && mode==amAssign))
		{
			// any action here?
		}
		else
		{
			killAllNotes();
			if (mode!=amOff)
				arp_resetCounter(settings.syncMode==smInternal);
		}
	}

	if(!hold && arp.hold)
		killHeldNotes();

	arp.mode=mode;
	arp.hold=(mode==amOff)?0:hold;
}

FORCEINLINE void arp_setTranspose(int8_t transpose)
{
	arp.transpose=transpose;
}

FORCEINLINE void arp_resetCounter(int8_t beatReset)
{
	arp.noteIndex=-1; // reinit
	arp.euclidStep=0; // the Euclidean rhythm restarts together with the note sequence
	arp.swingOdd=0; // the swing pattern restarts too, and a step that was waiting for its delay is dropped
	arp.swingCountdown=0;
	arp.ratchetCountdown=0; // a restart also drops the repeats that were waiting
	arp.acc=ARP_WAIT_CLOCK; // a resolution with its own timer plays the first step at the next clock step, like resolution 0 does...
	arp.lastResolution=arpResolution();
	if (beatReset&&seq_getMode(0)!=smPlaying&&seq_getMode(1)!=smPlaying)
    {
		arp.acc=ARP_FIRE_NOW; // ...or at the next clock unit when the clock is reset to start immediately (just below)
		clock_reset(); // start immediately
		synth_resetClockBar(); // reset the LFO sync counter
    }
}

FORCEINLINE arpMode_t arp_getMode(void)
{
	return arp.mode;
}

FORCEINLINE int8_t arp_getHold(void)
{
	return arp.hold;
}

void arp_assignNote(uint8_t note, int8_t on)
{
	int16_t i;
	
	if(arp.mode==amOff)
		return;
	
	// We only arpeggiate from the internal keyboard, so we can keep the
	// note memory size at 128 if we set the keyboard range to 0 and up.
	note-=SCANNER_BASE_NOTE;
	if(on)
	{

		// if this is the first note, make sure the arp will start on it as as soon as we update
		if(isEmpty()) arp_resetCounter(settings.syncMode==smInternal);

		// assign note			
		
		if(arp.mode!=amUpDown)
		{
			for(i=0;i<ARP_NOTE_MEMORY;++i)
				if(arp.notes[i]==ASSIGNER_NO_NOTE)
				{
					arp.notes[i]=note;
					break;
				}
		}
		else
		{
			arp.notes[note]=note;
			arp.notes[ARP_LAST_NOTE-note]=note;
		}
	}
	else
	{
		if(arp.hold)
		{
			// mark deassigned notes as held

			if(arp.mode!=amUpDown)
			{
				for(i=0;i<ARP_NOTE_MEMORY;++i)
					if(arp.notes[i]==note)
					{
						arp.notes[i]|=ARP_NOTE_HELD_FLAG;
						break;
					}
			}
			else
			{
				arp.notes[note]|=ARP_NOTE_HELD_FLAG;
				arp.notes[ARP_LAST_NOTE-note]|=ARP_NOTE_HELD_FLAG;
			}
		}
		else
		{
			// deassign note if not in hold mode

			if(arp.mode!=amUpDown)
			{
				for(i=0;i<ARP_NOTE_MEMORY;++i)
					if(arp.notes[i]==note)
					{
						arp.notes[i]=ASSIGNER_NO_NOTE;
						break;
					}
			}
			else
			{
				arp.notes[note]=ASSIGNER_NO_NOTE;
				arp.notes[ARP_LAST_NOTE-note]=ASSIGNER_NO_NOTE;
			}

			// gate off for last note

			if(isEmpty())
				finishPreviousNote();
		}
	}
}

// A step has just been played: decide whether it ratchets and, if so, schedule its first repeat. window is the time in clock units
// until the next step (swing makes it longer or shorter than the step length).
static void ratchetStart(uint16_t window)
{
	uint8_t choice,n,pc;
	uint16_t minSpacing;

	arp.ratchetCountdown=0;

	choice=currentPreset.steppedParameters[spArpRatchet];
	if(choice==0 || choice>=ARP_RATCHET_CHOICES) // off (or an invalid value)
		return;

	// choice 1..3 = 2..4 hits. The hits can not be closer together than the fastest step, so a fast step gets fewer hits (or none): the speed saturates
	n=choice+1;
	minSpacing=(settings.syncMode==smInternal)?ARP_MIN_STEP_UNITS:1;
	while(n>1 && window/n<minSpacing)
		--n;
	if(n<2)
		return;

	// the chance that this step ratchets. Choice 0 (100%) does not touch the random number generator.
	pc=currentPreset.steppedParameters[spArpRatchetProb];
	if(pc>0 && pc<ARP_RATCHET_PROB_CHOICES && (uint8_t)(random()>>16)>=ratchetProbThreshold[pc]) // use high bits, the low bits of the generator are not very random
		return;

	arp.ratchetCount=n;
	arp.ratchetWindow=window;
	arp.ratchetIndex=1;
	arp.ratchetCountdown=window/n; // the first repeat comes after 1/n of the step
}

// The countdown of a repeat is over: end the note and play it again, then schedule the next repeat, if any.
static void ratchetRepeat(void)
{
	uint8_t n;
	uint32_t previous,next;

	arp.ratchetCountdown=0;

	// the arp was switched off, restarted, or all notes were released since the step started: no repeat
	if(arp.mode==amOff || isEmpty() || arp.previousNote==ASSIGNER_NO_NOTE)
		return;

	finishPreviousNote();

	n=arp.previousNote&~ARP_NOTE_HELD_FLAG;
	assigner_assignNote(n+SCANNER_BASE_NOTE+arp.previousTranspose,1,HALF_RANGE,0); // same note, same velocity as the step

	if (settings.midiMode==0) // pass to MIDI out like a normal step, see arpStep
		midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.previousTranspose,1,HALF_RANGE);

	// the repeat k is at k/n of the step. Computing the times of both repeats from the window (and not adding up a rounded length) spreads them evenly
	previous=((uint32_t)arp.ratchetIndex*arp.ratchetWindow)/arp.ratchetCount;
	++arp.ratchetIndex;
	if(arp.ratchetIndex<arp.ratchetCount)
	{
		next=((uint32_t)arp.ratchetIndex*arp.ratchetWindow)/arp.ratchetCount;
		arp.ratchetCountdown=(uint16_t)(next-previous);
	}
}

// plays one arp step (the whole job of arp_update before swing existed). window is the time in clock units until the next step, for the ratchets
static void arpStep(uint16_t window)
{
	uint8_t n, step;
	uint8_t ePattern, eStep, eHit; // Euclidean rhythm: selected rhythm, position in it, and whether this step plays
	uint8_t prob; // selected step probability
	
	arp.ratchetCountdown=0; // a new step ends the repeats of the previous one, whatever happens to this step

	// arp off -> nothing to do
	
	if(arp.mode==amOff)
		return;
	
	// nothing to play ?
	
	if(isEmpty())
		return;
	
	// yep
	
	// Euclidean rhythm: is this step a hit (play a note) or a rest?
	ePattern=currentPreset.steppedParameters[spArpEuclid];
	eHit=1; // rhythm off, or an invalid value (e.g. from corrupted data): every step plays
	if(ePattern>0 && ePattern<ARP_EUCLID_PATTERNS)
	{
		eStep=arp.euclidStep;
		if(eStep>=euclidLength[ePattern]) // the rhythm was changed while running: start it over
			eStep=0;
		eHit=(euclidMask[ePattern]>>eStep)&1;
		arp.euclidStep=(eStep+1>=euclidLength[ePattern])?0:eStep+1; // next step, wrapping at the end of the rhythm
	}
	else
		arp.euclidStep=0; // so that switching a rhythm on starts it from its first step

	// Probability: a hit may still be dropped. Choice 0 (100%) does not touch the random number generator.
	prob=currentPreset.steppedParameters[spArpProbability];
	if(eHit && prob>0 && prob<ARP_PROBABILITY_CHOICES)
	{
		if((uint8_t)(random()>>16)>=probabilityThreshold[prob]) // use high bits, the low bits of the generator are not very random
			eHit=0;
	}

	finishPreviousNote(); // the previous note ends at every step, also when this step is a rest

	if(!eHit)
	{
		// rest: the note was just finished above, so make sure it is not finished a second time later;
		// noteIndex and previousIndex stay as they are, so the note sequence continues unchanged at the next hit
		arp.previousNote=ASSIGNER_NO_NOTE;
		return;
	}
			
	// act depending on mode
	
	switch(arp.mode)
	{
        case amUpDown:
            do
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY; // cycle through the array
            while (arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE || (arp.previousIndex==(ARP_LAST_NOTE-arp.noteIndex))); // add here the avoidance of top/bottom note double playing
            break;
        case amAssign:
            do
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY;
            while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE);
            break;

        case amRandom:
            step=0;
            for (n=0;n<ARP_NOTE_MEMORY;n++)
            {
                if (arp.notes[n]!=ASSIGNER_NO_NOTE) step++;
            }
            // n is the random number of notes that will be skipped. Step is always >0 if there is more than one note but alsoways =0 if there is exactly one note
            n=0;
            if (step>1)
			{
				n=(random()%(step-1))+1;
			}
			if (arp.noteIndex<0) arp.noteIndex=0;
            while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE || n>0)
            {
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY;
                if (arp.notes[arp.noteIndex]!=ASSIGNER_NO_NOTE) n--;
            }
            break;
        default:
            return;
	}
	
	n=arp.notes[arp.noteIndex]&~ARP_NOTE_HELD_FLAG;
	
	// send note to assigner, velocity at half (MIDI value 64)
	
	assigner_assignNote(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE,0);
	
	// pass to MIDI out only in local on mode:
    // in local off mode external MIDI plays into arp so if MIDI out is connected (via MIDI recording tools) to MIDI in we would get an infinite loop...
    if (settings.midiMode==0)
    {
        midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE);
    }

	arp.previousNote=arp.notes[arp.noteIndex];
	arp.previousTranspose=arp.transpose;
	arp.previousIndex=arp.noteIndex;

	ratchetStart(window); // the step has been played: it may ratchet
}

// An arp step is due: called by the clock for resolution 0, by the resolution timer (arp_clockTick) for the others.
// Normally the step is played right away. With swing on, every second step is delayed: it is stored here and
// arp_clockTick plays it later. The steps are counted on the arp's own grid, so swing is independent of the Euclidean
// rhythm and of rests.
static void arpTrigger(void)
{
	uint8_t swing;
	uint16_t speed;
	uint16_t delay;
	uint16_t stepUnits; // length of one arp step in clock units
	uint16_t window; // time until the next step in clock units, for the ratchets

	// the same early exits as arpStep, so that an idle arp does not count steps
	if(arp.mode==amOff)
		return;

	if(isEmpty())
		return;

	swing=currentPreset.steppedParameters[spArpSwing];
	speed=clock_getSpeed();
	stepUnits=arpStepUnits(arpResolution()); // swing is a fraction of the arp's step, which depends on the resolution

	window=stepUnits;

	if(swing>0 && swing<ARP_SWING_CHOICES && speed!=UINT16_MAX)
	{
		delay=(uint16_t)(((uint32_t)stepUnits*swingDelayPercent[swing])/100); // clock units: 500Hz ticks, or MIDI clock pulses when synced
		if(delay>=stepUnits) // never run into the next step
			delay=stepUnits-1;

		arp.swingOdd=!arp.swingOdd; // toggles at every step: the first step of a pair is straight, the second one is delayed

		if(!arp.swingOdd) // we just toggled back to 0, so this was the second step of the pair
		{
			window=stepUnits-delay; // it starts late, so the next (straight) step comes sooner after it

			if(delay>0)
			{
				arp.swingCountdown=delay; // arp_clockTick plays this step when the delay is over
				arp.pendingWindow=window; // and needs its window then
				return;
			}
		}
		else
			window=stepUnits+delay; // the step after a straight step is delayed, so it comes later
	}
	else
		arp.swingOdd=0; // swing off: when it is switched on, the first step is a straight one

	arpStep(window);
}

// Called once per clock step. Only resolution 0 steps with the clock, the other resolutions have their own timer.
void arp_update(void)
{
	if(arpResolution()==0)
		arpTrigger();
	else if(arp.acc==ARP_WAIT_CLOCK && arp.mode!=amOff && !isEmpty()) // the first step of the resolution timer waits for the clock grid
	{
		arp.acc=0;
		arpTrigger();
	}
}

// Called once per clock unit (every 500Hz tick when the clock is internal, every MIDI clock pulse when synced),
// before the clock itself is updated. Plays a step that swing has delayed, when its delay is over, and runs the
// timer of the resolutions other than 0.
void arp_clockTick(void)
{
	uint8_t res;
	uint32_t interval;

	res=arpResolution();
	if(res!=arp.lastResolution) // the resolution was changed: count from the start again
	{
		arp.lastResolution=res;
		arp.acc=0;
		arp.swingCountdown=0; // a step that was waiting for its swing delay belongs to the old step length: drop it, so it can not play together with a new step
		arp.swingOdd=0;
		arp.ratchetCountdown=0; // the repeats were spread over the old step length
	}

	// repeats of a ratcheted step. This comes before the steps, so that a step that starts in this unit has its first repeat counted from the next unit
	if(arp.ratchetCountdown>0)
	{
		--arp.ratchetCountdown;
		if(arp.ratchetCountdown==0)
			ratchetRepeat();
	}

	if(arp.swingCountdown>0)
	{
		--arp.swingCountdown;
		if(arp.swingCountdown==0)
			arpStep(arp.pendingWindow); // checks for itself that the arp is still on and has notes
	}

	if(res==0 || arp.mode==amOff || isEmpty() || clock_getSpeed()==UINT16_MAX || arp.acc==ARP_WAIT_CLOCK) // nothing to do: the clock drives resolution 0, and the first step waits for the clock
		return;

	if(arp.acc==ARP_FIRE_NOW) // the arp has just started: play the first step now
	{
		arp.acc=0;
		arpTrigger();
		return;
	}

	interval=arpInterval(res);
	arp.acc+=resolutionP[res];
	if(arp.acc>=interval) // a step is due (the interval is never below one unit's worth, so at most one step per unit)
	{
		arp.acc-=interval;
		arpTrigger();
	}
}

void arp_init(void)
{
	memset(&arp,0,sizeof(arp));

	memset(arp.notes,ASSIGNER_NO_NOTE,ARP_NOTE_MEMORY);
	
	arp.noteIndex=-1;
	arp.previousNote=ASSIGNER_NO_NOTE;
}
