////////////////////////////////////////////////////////////////////////////////
// Voice assigner
////////////////////////////////////////////////////////////////////////////////

#include "assigner.h"

struct allocation_s
{
	uint32_t timestamp;
	uint16_t velocity;
	uint8_t rootNote;
	uint8_t note;
	int8_t assigned;
	int8_t gated;
	int8_t keyPressed;
};

static struct
{
	uint8_t noteStates[16]; // 1 bit per note, 128 notes
	uint16_t noteVelocities[128];
	struct allocation_s allocation[SYNTH_VOICE_COUNT];
	uint8_t patternOffsets[SYNTH_VOICE_COUNT];
	assignerPriority_t priority;
	uint8_t voiceMask;
	int8_t mono;
	int8_t hold;
} assigner;

static const uint8_t bit2mask[8] = {1,2,4,8,16,32,64,128};

static inline void setNoteState(uint8_t note, int8_t gate)
{
	uint8_t *bf,mask;
	
	bf=&assigner.noteStates[note>>3];
	mask=bit2mask[note&7];
	
	if(gate)	
		*bf|=mask;
	else
		*bf&=~mask;
}

static inline int8_t getNoteState(uint8_t note)
{
	uint8_t bf,mask;
	
	bf=assigner.noteStates[note>>3];
	
	if(!bf)
		return 0;
	
	mask=bit2mask[note&7];
	
	return (bf&mask)!=0;
}

static inline void setNoteVelocity(uint8_t note, uint8_t gate, uint16_t velocity)
{
	if (gate)
		assigner.noteVelocities[note]=velocity;
}

static inline uint16_t getNoteVelocity(uint8_t note)
{
	return assigner.noteVelocities[note];
}

static inline int8_t isVoiceDisabled(int8_t voice)
{
	return !(assigner.voiceMask&bit2mask[voice]);
}

static inline int8_t getAvailableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,oldestVoice=-1,sameNote=-1;
	uint32_t oldestTimestamp=UINT32_MAX;

	for(v=0;v<SYNTH_VOICE_COUNT;++v)
	{
		// never assign a disabled voice
		
		if(isVoiceDisabled(v))
			continue;
		
		if(assigner.allocation[v].assigned)
		{
			// triggering a note that is still allocated to a voice should use this voice
		
			if(assigner.allocation[v].timestamp<timestamp && assigner.allocation[v].note==note)
			{
				sameNote=v;
				break;
			}
		}
		else
		{
			// else use oldest voice, if there is one

			if (assigner.allocation[v].timestamp<oldestTimestamp)
			{
				oldestTimestamp=assigner.allocation[v].timestamp;
				oldestVoice=v;
			}
		}
	}
	
	if(sameNote>=0)
		return sameNote;
	else
		return oldestVoice;
}

static inline int8_t getDispensableVoice(uint8_t note)
{
	int8_t v,res=-1;
	uint32_t ts;

	// first pass, steal oldest released voice
	
	ts=UINT32_MAX;
		
	for(v=0;v<SYNTH_VOICE_COUNT;++v)
	{
		if(isVoiceDisabled(v))
			continue;
		
		if(!getNoteState(assigner.allocation[v].rootNote) && assigner.allocation[v].timestamp<ts)
		{
			ts=assigner.allocation[v].timestamp;
			res=v;
		}
	}
	
	if(res>=0)
		return res;
	
	// second pass, use priority rules to steal the less important held note
	
	ts=UINT32_MAX;
		
	for(v=0;v<SYNTH_VOICE_COUNT;++v)
	{
		if(isVoiceDisabled(v))
			continue;
		
		switch(assigner.priority)
		{
		case apLast:
			if(assigner.allocation[v].timestamp<ts)
			{
				res=v;
				ts=assigner.allocation[v].timestamp;
			}
			break;
		case apLow:
			if(assigner.allocation[v].note>note)
			{
				res=v;
				note=assigner.allocation[v].note;
			}
			break;
		case apHigh:
			if(assigner.allocation[v].note<note)
			{
				res=v;
				note=assigner.allocation[v].note;
			}
			break;
		}
	}
	
	return res;
}
	
void assigner_setPriority(assignerPriority_t prio)
{
	if(prio==assigner.priority)
		return;
	
	assigner_voiceDone(-1);
	
	if(prio>2)
		prio=0;
	
	assigner.priority=prio;
}

void assigner_setVoiceMask(uint8_t mask)
{
	if(mask==assigner.voiceMask)
		return;
	
	assigner_voiceDone(-1);
	assigner.voiceMask=mask;
}

FORCEINLINE int8_t assigner_getAssignment(int8_t voice, uint8_t * note)
{
	int8_t a;
	
	a=assigner.allocation[voice].assigned;
	
	if(a && note)
		*note=assigner.allocation[voice].note;
	
	return a;
}

int8_t assigner_getAnyPressed(void)
{
	int8_t i;
	int8_t v=0;
	
	for(i=0;i<sizeof(assigner.noteStates);++i)
		v|=assigner.noteStates[i];
	
	return v!=0;
}

int8_t assigner_getAnyAssigned(void)
{
	int8_t i;
	int8_t v=0;
	
	for(i=0;i<SYNTH_VOICE_COUNT;++i)
		v|=assigner.allocation[i].assigned;
	
	return v!=0;
}

void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity)
{
	uint32_t timestamp;
	uint16_t oldVel;
	uint8_t restoredNote;
	int8_t v,vi,legato=0;
	int16_t ni,n;
	
	setNoteState(note,gate);
	// Save velocity for later, in case note needs to be restored
	setNoteVelocity(note,gate,velocity);

	if(gate)
	{
		
reassign:

		timestamp=currentTick;

		if(assigner.mono)
		{
			// just handle legato & priority
			
			v=0;

			if(assigner.priority!=apLast)
				for(n=0;n<128;++n)
					if(n!=note && getNoteState(n))
					{
						if (note>n && assigner.priority==apLow)
							return;
						if (note<n && assigner.priority==apHigh)
							return;
						
						legato=1;
					}
		}
		else
		{
			// first, try to get a free voice

			v=getAvailableVoice(note,timestamp);

			// no free voice, try to steal one

			if(v<0)
				v=getDispensableVoice(note);

			// we might still have no voice

			if(v<0)
				return;
		}

		// try to assign the whole pattern of notes

		for(vi=0;vi<SYNTH_VOICE_COUNT;++vi)
		{
			if(assigner.patternOffsets[vi]==ASSIGNER_NO_NOTE)
				break;

			n=note+assigner.patternOffsets[vi];

			assigner.allocation[v].assigned=1;
			assigner.allocation[v].gated=1;
			assigner.allocation[v].keyPressed=1;
			assigner.allocation[v].velocity=velocity;
			assigner.allocation[v].rootNote=note;
			assigner.allocation[v].note=n;
			assigner.allocation[v].timestamp=timestamp;

			synth_assignerEvent(n,1,v,velocity,legato);

			do
				v=(v+1)%SYNTH_VOICE_COUNT;
			while(isVoiceDisabled(v));
		}
	}
	else
	{
		restoredNote=ASSIGNER_NO_NOTE;

		// some still triggered notes might have been stolen, find them

		for(ni=0;ni<128;++ni)
		{
			if(assigner.priority==apHigh)
				n=127-ni;
			else
				n=ni;
			
			if(getNoteState(n))
			{
				for(v=0;v<SYNTH_VOICE_COUNT;++v)
					if(assigner.allocation[v].assigned && assigner.allocation[v].rootNote==n)
						break;

				if(v==SYNTH_VOICE_COUNT)
				{
					restoredNote=n;
					oldVel=getNoteVelocity(n);
					break;
				}
			}
		}

		if(restoredNote==ASSIGNER_NO_NOTE)
		// no note to restore, gate off all voices with rootNote=note
		{
			for(v=0;v<SYNTH_VOICE_COUNT;++v)
			{
				if(assigner.allocation[v].assigned && assigner.allocation[v].rootNote==note)
				{
					assigner.allocation[v].keyPressed=0;
					if(!assigner.hold)
					{
						assigner.allocation[v].gated=0;
						synth_assignerEvent(assigner.allocation[v].note,0,v,velocity,0);
					}
				}
			}
		}
		else
		// restored notes can be assigned again
		{
			note=restoredNote;
			velocity=oldVel;
			gate=1;
			legato=1;
			
			goto reassign;
		}
	}
}

void assigner_voiceDone(int8_t voice)
{
	int8_t v;
	for(v=0;v<SYNTH_VOICE_COUNT;++v)
		if(v==voice || voice<0)
		{
			assigner.allocation[v].assigned=0;
			assigner.allocation[v].keyPressed=0;
			assigner.allocation[v].note=ASSIGNER_NO_NOTE;
			assigner.allocation[v].rootNote=ASSIGNER_NO_NOTE;
			if(voice<0)
				assigner.allocation[v].timestamp=0; // reset to voice 0 in case all voices stopped at once
		}
}

LOWERCODESIZE void assigner_setPattern(uint8_t * pattern, int8_t mono)
{
	int8_t i,count=0;
	
	if(mono==assigner.mono && !memcmp(pattern,&assigner.patternOffsets[0],SYNTH_VOICE_COUNT))
		return;

	if(mono!=assigner.mono)
		assigner_voiceDone(-1);
	
	assigner.mono=mono;
	memset(&assigner.patternOffsets[0],ASSIGNER_NO_NOTE,SYNTH_VOICE_COUNT);

	for(i=0;i<SYNTH_VOICE_COUNT;++i)
	{
		if(pattern[i]==ASSIGNER_NO_NOTE)
			break;
		
		assigner.patternOffsets[i]=pattern[i];
		++count;
	}

	if(count>0)
	{
		assigner.patternOffsets[0]=0; // root note always has offset 0
	}
	else
	{
		// empty pattern means unison
		memset(assigner.patternOffsets,0,SYNTH_VOICE_COUNT);
	}
}

void assigner_getPattern(uint8_t * pattern, int8_t * mono)
{
	memcpy(pattern,assigner.patternOffsets,SYNTH_VOICE_COUNT);
	
	if(mono!=NULL)
		*mono=assigner.mono;
}

LOWERCODESIZE void assigner_latchPattern(void)
{
	int16_t i;
	int8_t count;
	uint8_t pattern[SYNTH_VOICE_COUNT];	
	count=0;
	
	memset(pattern,ASSIGNER_NO_NOTE,SYNTH_VOICE_COUNT);
	
	for(i=0;i<128;++i)
		if(getNoteState(i))
		{
			pattern[count]=i;
			
			if(count>0)
				pattern[count]-=pattern[0]; // it's a list of offsets to the root note
						
			++count;
			
			if(count>=SYNTH_VOICE_COUNT)
				break;
		}

	assigner_setPattern(pattern,1);
}

LOWERCODESIZE void assigner_setPoly(void)
{
	uint8_t polyPattern[SYNTH_VOICE_COUNT]={0,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE};	
	assigner_setPattern(polyPattern,0);
}

void assigner_holdEvent(int8_t hold)
{
	int8_t v;

	if (hold) {
		assigner.hold=1;
		return;
	}

	assigner.hold=0;
	// Send gate off to all voices whose corresponding key is up
	for(v=0;v<SYNTH_VOICE_COUNT;++v) {
		if (!isVoiceDisabled(v) && 
		    assigner.allocation[v].gated &&
		    !assigner.allocation[v].keyPressed) {
			synth_assignerEvent(assigner.allocation[v].note,0,v,assigner.allocation[v].velocity,0);
		    	assigner.allocation[v].gated=0;
		}
	}
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));

	assigner.voiceMask=0x3f;
	memset(&assigner.patternOffsets[0],ASSIGNER_NO_NOTE,SYNTH_VOICE_COUNT);
	assigner.patternOffsets[0]=0;
}

