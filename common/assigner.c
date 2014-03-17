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
};

static struct
{
	uint8_t noteStates[16]; // 1 bit per note, 128 notes
	struct allocation_s allocation[SYNTH_VOICE_COUNT];
	uint8_t patternOffsets[SYNTH_VOICE_COUNT];
	assignerPriority_t priority;
	uint8_t voiceMask;
	int8_t mono;
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

static inline int8_t isVoiceDisabled(int8_t voice)
{
	return !(assigner.voiceMask&bit2mask[voice]);
}

static inline int8_t getAvailableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,sameNote=-1,firstFree=-1;

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
			// else use first free voice, if there's one
			
			if(firstFree<0)
				firstFree=v;
		}
	}
	
	if(sameNote>=0)
		return sameNote;
	else
		return firstFree;
}

static inline int8_t getDispensableVoice(uint8_t note, int8_t * stolenHeld)
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
	
	*stolenHeld=0;
	
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
	
	*stolenHeld=1;
	
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

void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity, int8_t forceLegato)
{
	uint32_t timestamp;
	uint16_t oldVel;
	uint8_t restoredNote;
	int8_t v,vi,i,legato,stolenHeld;
	int16_t n;
	
	setNoteState(note,gate);

	if(gate)
	{
		
reassign:

		timestamp=currentTick;
		legato=forceLegato;
	
		// first, try to get a free voice

		v=getAvailableVoice(note,timestamp);

		// no free voice, try to steal one

		if(v<0)
		{
			v=getDispensableVoice(note,&stolenHeld);
			legato|=stolenHeld && assigner.priority!=apLast;
		}

		// we might still have no voice

		if(v>=0)
		{
			if(assigner.mono)
				v=0;
			
			// try to assign the whole pattern of notes

			for(vi=0;vi<SYNTH_VOICE_COUNT;++vi)
			{
				if(assigner.patternOffsets[vi]==ASSIGNER_NO_NOTE)
					break;
				
				n=note+assigner.patternOffsets[vi];

				assigner.allocation[v].assigned=1;
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
	}
	else
	{
		oldVel=UINT16_MAX;
		restoredNote=ASSIGNER_NO_NOTE;

		// some still triggered notes might have been stolen, find them

		for(n=0;n<128;++n)
			if(getNoteState(n))
			{
				i=0;
				for(v=0;v<SYNTH_VOICE_COUNT;++v)
					if(assigner.allocation[v].assigned && assigner.allocation[v].rootNote==n)
					{
						i=1;
						break;
					}

				if(i==0)
				{
					restoredNote=n;
					break;
				}
			}

		// hitting a note spawns a pattern of note, not just one

		for(v=0;v<SYNTH_VOICE_COUNT;++v)
			if(assigner.allocation[v].assigned && assigner.allocation[v].rootNote==note)
			{
				if(restoredNote!=ASSIGNER_NO_NOTE)
				{
					oldVel=assigner.allocation[v].velocity;
				}
				else
				{
					synth_assignerEvent(assigner.allocation[v].note,0,v,velocity,0);
				}
			}

		// restored notes can be assigned again

		if(restoredNote!=ASSIGNER_NO_NOTE)
		{
			note=restoredNote;
			gate=1;
			velocity=oldVel;
			forceLegato=1;
			
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
			assigner.allocation[v].note=ASSIGNER_NO_NOTE;
			assigner.allocation[v].rootNote=ASSIGNER_NO_NOTE;
			assigner.allocation[v].timestamp=0;
		}
	
	if(voice<0)
	{
		// also remove any stuck notes
		memset(assigner.noteStates,0,sizeof(assigner.noteStates));
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

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));

	assigner.voiceMask=0x3f;
	memset(&assigner.patternOffsets[0],ASSIGNER_NO_NOTE,SYNTH_VOICE_COUNT);
	assigner.patternOffsets[0]=0;
}

