////////////////////////////////////////////////////////////////////////////////
// Voice assigner
////////////////////////////////////////////////////////////////////////////////

#include "assigner.h"

const char * assigner_priorityName[3]={"last","lo","hi"};

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
	struct allocation_s voiceAllocation[P600_VOICE_COUNT];
	uint8_t patternOffsets[P600_VOICE_COUNT];
	int8_t patternNoteCount;
	assignerPriority_t priority;
	uint8_t voiceMask;
} assigner;

static void setNoteState(uint8_t note, int8_t gate)
{
	uint8_t mask;
	
	mask=1<<(note&7);
	
	if(gate)	
		assigner.noteStates[note>>3]|=mask;
	else
		assigner.noteStates[note>>3]&=~mask;
}

static int8_t getNoteState(uint8_t note)
{
	uint8_t mask;
	
	mask=1<<(note&7);

	return (assigner.noteStates[note>>3]&mask)!=0;
}

static inline int8_t isVoiceDisabled(int8_t voice)
{
	return !(assigner.voiceMask&(1<<voice));
}

LOWERCODESIZE static int8_t getAvailableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,sameNote=-1,firstFree=-1;
		
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		// never assign a disabled voice
		
		if(isVoiceDisabled(v))
			continue;
		
		if(assigner.voiceAllocation[v].assigned)
		{
			// triggering a note that is still allocated to a voice should use this voice
		
			if(assigner.voiceAllocation[v].timestamp<timestamp && assigner.voiceAllocation[v].note==note)
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

LOWERCODESIZE static int8_t getDispensableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,res=-1;
		
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		// never assign a disabled voice
		
		if(isVoiceDisabled(v))
			continue;
		
		// else use priority rules to steal the less important one
		
		switch(assigner.priority)
		{
		case apLast:
			if(assigner.voiceAllocation[v].timestamp<timestamp)
			{
				res=v;
				timestamp=assigner.voiceAllocation[v].timestamp;
			}
			break;
		case apLow:
			if(assigner.voiceAllocation[v].note>note)
			{
				res=v;
				note=assigner.voiceAllocation[v].note;
			}
			break;
		case apHigh:
			if(assigner.voiceAllocation[v].note<note)
			{
				res=v;
				note=assigner.voiceAllocation[v].note;
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

int8_t assigner_getAssignment(int8_t voice, uint8_t * note)
{
	int8_t a;
	
	a=assigner.voiceAllocation[voice].assigned;
	
	if(a)
		*note=assigner.voiceAllocation[voice].note;
	
	return a;
}

LOWERCODESIZE void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity, int8_t forceLegato)
{
	uint32_t timestamp=currentTick;
	uint16_t oldVel=UINT16_MAX;
	uint8_t n,restoredNote=ASSIGNER_NO_NOTE;
	int8_t i,v,legato=forceLegato;
	
	setNoteState(note,gate);
	
	if(gate)
	{
		// try to assign the whole pattern of notes
		
		for(i=0;i<assigner.patternNoteCount;++i)
		{
			n=note+assigner.patternOffsets[i];
			
			// first, try to get a free voice
			
			v=getAvailableVoice(n,timestamp);
			
			// no free voice, try to steal one
			
			if(v<0)
			{
				v=getDispensableVoice(n,timestamp);
				
				legato|=assigner.priority!=apLast; // legato is for lo/hi note priority
			}
			
			// we might still have no voice
			
			if(v>=0)
			{
				assigner.voiceAllocation[v].assigned=1;
				assigner.voiceAllocation[v].velocity=velocity;
				assigner.voiceAllocation[v].rootNote=note;
				assigner.voiceAllocation[v].note=n;
				assigner.voiceAllocation[v].timestamp=timestamp;

				p600_assignerEvent(n,1,v,velocity,legato);
			}
		}
	}
	else
	{
		// some still triggered notes might have been stolen, find them
		
		for(n=0;n<128;++n)
			if(getNoteState(n))
			{
				i=0;
				for(v=0;v<P600_VOICE_COUNT;++v)
					if(assigner.voiceAllocation[v].assigned && assigner.voiceAllocation[v].rootNote==n)
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
			if(assigner.voiceAllocation[v].assigned && assigner.voiceAllocation[v].rootNote==note)
			{
				if(restoredNote!=ASSIGNER_NO_NOTE)
				{
					oldVel=assigner.voiceAllocation[v].velocity;
					assigner_voiceDone(v);
				}
				else
				{
					p600_assignerEvent(assigner.voiceAllocation[v].note,0,v,velocity,0);
				}
			}
		
		// restored notes can be assigned again
		
		if(restoredNote!=ASSIGNER_NO_NOTE)
			assigner_assignNote(restoredNote,1,oldVel,1);
	}
}

LOWERCODESIZE void assigner_voiceDone(int8_t voice)
{
	if(voice<0)
	{
		for(voice=0;voice<P600_VOICE_COUNT;++voice)
			assigner_voiceDone(voice);
	}
	else
	{
		assigner.voiceAllocation[voice].assigned=0;
		assigner.voiceAllocation[voice].note=ASSIGNER_NO_NOTE;
		assigner.voiceAllocation[voice].rootNote=ASSIGNER_NO_NOTE;
		assigner.voiceAllocation[voice].timestamp=0;
	}
}

LOWERCODESIZE void assigner_setPattern(uint8_t * pattern)
{
	int8_t i,count=0;
	
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

void assigner_getPattern(uint8_t * pattern)
{
	memcpy(pattern,assigner.patternOffsets,P600_VOICE_COUNT);
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

	assigner_setPattern(pattern);
}

LOWERCODESIZE void assigner_setPolyPattern(void)
{
	// root note only
	uint8_t pattern[P600_VOICE_COUNT]={0,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE,ASSIGNER_NO_NOTE};	
	assigner_setPattern(pattern);
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));
	assigner_setPolyPattern();
}

