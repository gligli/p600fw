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
	int8_t disabled;
};

static struct
{
	uint8_t noteStates[16]; // 1 bit per note, 128 notes
	struct allocation_s allocation[P600_VOICE_COUNT];
	uint8_t patternOffsets[P600_VOICE_COUNT];
	int8_t patternNoteCount;
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

static inline int8_t getAvailableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,sameNote=-1,firstFree=-1;

	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		// never assign a disabled voice
		
		if(assigner.allocation[v].disabled)
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
		
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		if(assigner.allocation[v].disabled)
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
		
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		if(assigner.allocation[v].disabled)
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
	int8_t v;

	if(mask==assigner.voiceMask)
		return;
	
	for(v=0;v<P600_VOICE_COUNT;++v)
		assigner.allocation[v].disabled=!(mask&(1<<v));
	
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
	uint8_t v=0;
	
	for(i=0;i<sizeof(assigner.noteStates);++i)
		v|=assigner.noteStates[i];
	
	return v!=0;
}

void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity, int8_t forceLegato)
{
	uint32_t timestamp;
	uint16_t oldVel;
	uint8_t n,restoredNote;
	int8_t v,vi,i,legato,stolenHeld;
	
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

			for(vi=0;vi<P600_VOICE_COUNT;++vi)
			{
				if(assigner.patternOffsets[vi]==ASSIGNER_NO_NOTE)
					break;
				
				n=note+assigner.patternOffsets[vi];

				assigner.allocation[v].assigned=1;
				assigner.allocation[v].velocity=velocity;
				assigner.allocation[v].rootNote=note;
				assigner.allocation[v].note=n;
				assigner.allocation[v].timestamp=timestamp;

				p600_assignerEvent(n,1,v,velocity,legato);
				
				do
					v=(v+1)%P600_VOICE_COUNT;
				while(assigner.allocation[v].disabled);
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
				for(v=0;v<P600_VOICE_COUNT;++v)
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

		for(v=0;v<P600_VOICE_COUNT;++v)
			if(assigner.allocation[v].assigned && assigner.allocation[v].rootNote==note)
			{
				if(restoredNote!=ASSIGNER_NO_NOTE)
				{
					oldVel=assigner.allocation[v].velocity;
				}
				else
				{
					p600_assignerEvent(assigner.allocation[v].note,0,v,velocity,0);
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
	if(voice<0)
	{
		for(voice=0;voice<P600_VOICE_COUNT;++voice)
			assigner_voiceDone(voice);
	}
	else
	{
		assigner.allocation[voice].assigned=0;
		assigner.allocation[voice].note=ASSIGNER_NO_NOTE;
		assigner.allocation[voice].rootNote=ASSIGNER_NO_NOTE;
		assigner.allocation[voice].timestamp=0;
	}
}

LOWERCODESIZE void assigner_setPattern(uint8_t * pattern, int8_t mono)
{
	int8_t i,count=0;

	if(mono!=assigner.mono)
		assigner_voiceDone(-1);
	
	assigner.mono=mono;
	memset(assigner.patternOffsets,ASSIGNER_NO_NOTE,P600_VOICE_COUNT);

	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		if(pattern[i]==ASSIGNER_NO_NOTE)
			break;
		
		assigner.patternOffsets[i]=pattern[i];
		++count;
	}

	if(count>0)
	{
		assigner.patternNoteCount=count;
		assigner.patternOffsets[0]=0; // root note always has offset 0
	}
	else
	{
		// empty pattern means unison
		assigner.patternNoteCount=P600_VOICE_COUNT;
		memset(assigner.patternOffsets,0,P600_VOICE_COUNT);
	}
}

void assigner_getPattern(uint8_t * pattern, int8_t * mono)
{
	memcpy(pattern,assigner.patternOffsets,P600_VOICE_COUNT);
	
	if(mono!=NULL)
		*mono=assigner.mono;
}

LOWERCODESIZE void assigner_latchPattern(void)
{
	uint8_t i;
	int8_t count;
	uint8_t pattern[P600_VOICE_COUNT];	
	count=0;
	
	memset(pattern,ASSIGNER_NO_NOTE,P600_VOICE_COUNT);
	
	for(i=0;i<128;++i)
		if(getNoteState(i))
		{
			pattern[count]=i;
			
			if(count>0)
				pattern[count]-=pattern[0]; // it's a list of offsets to the root note
						
			++count;
			
			if(count>=P600_VOICE_COUNT)
				break;
		}

	assigner_setPattern(pattern,1);
}

LOWERCODESIZE void assigner_setPoly(void)
{
	print("poly");
	uint8_t polyPattern[P600_VOICE_COUNT]={0,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE};	
	assigner_setPattern(polyPattern,0);
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));
	assigner_setPoly();
}

