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
	int8_t voice;
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

static int8_t getAllocIdxFromVoice(int8_t voice)
{
	int8_t i,ai=-1;
	
	// shortcut
	if(assigner.allocation[voice].voice==voice)
		return voice;
	
	for(i=0;i<P600_VOICE_COUNT;++i)
		if(assigner.allocation[i].voice==voice)
		{
			ai=i;
			break;
		}

	return ai;
}

LOWERCODESIZE static int8_t getAvailableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,sameNote=-1,firstFree=-1;

	for(v=0;v<(assigner.mono?1:P600_VOICE_COUNT);++v)
	{
		// never assign a disabled voice
		
		if(assigner.allocation[v].voice==-1)
			break;
		
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

LOWERCODESIZE static int8_t getDispensableVoice(uint8_t note, uint32_t timestamp)
{
	int8_t v,res=-1;
		
	for(v=0;v<(assigner.mono?1:P600_VOICE_COUNT);++v)
	{
		// never assign a disabled voice
		
		if(assigner.allocation[v].voice==-1)
			break;
		
		// steal any released note
		
		if(!getNoteState(assigner.allocation[v].rootNote))
			return v;
		
		// else use priority rules to steal the less important one
		
		switch(assigner.priority)
		{
		case apLast:
			if(assigner.allocation[v].timestamp<timestamp)
			{
				res=v;
				timestamp=assigner.allocation[v].timestamp;
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
	int8_t i,v;

	if(mask==assigner.voiceMask)
		return;
	
	// generate a list of valid voice indices
	
	v=0;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		assigner.allocation[i].voice=-1;
		if(mask&(1<<i))
			assigner.allocation[v++].voice=i;
	}
	
	assigner_voiceDone(-1);
	assigner.voiceMask=mask;
}

int8_t assigner_getAssignment(int8_t voice, uint8_t * note)
{
	int8_t a,ai;
	
	ai=getAllocIdxFromVoice(voice);
	if(ai<0)
		return -1;	
	
	a=assigner.allocation[ai].assigned;
	
	if(a && note)
		*note=assigner.allocation[ai].note;
	
	return a;
}

LOWERCODESIZE void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity, int8_t forceLegato)
{
	uint32_t timestamp=currentTick;
	uint16_t oldVel=UINT16_MAX;
	uint8_t n,restoredNote=ASSIGNER_NO_NOTE;
	int8_t v,vc,i,redo,legato=forceLegato;
	
	do
	{
		redo=0;
		
		setNoteState(note,gate);

		if(gate)
		{
			// first, try to get a free voice

			v=getAvailableVoice(note,timestamp);

			// no free voice, try to steal one

			if(v<0)
			{
				v=getDispensableVoice(note,timestamp);

				// legato is for lo/hi note priority in case solen note is still held
				legato|=(assigner.priority!=apLast && getNoteState(assigner.allocation[v].rootNote));
			}

			// we might still have no voice

			if(v>=0)
			{
				// handle mono/poly

				if(assigner.mono)
				{
					v=0;
					vc=assigner.patternNoteCount;
				}
				else
				{
					vc=v+1;
				}

				// try to assign the whole pattern of notes

				for(;v<vc;++v)
				{
					if(assigner.patternOffsets[v]==ASSIGNER_NO_NOTE)
						break;

					n=note+assigner.patternOffsets[v];

					assigner.allocation[v].assigned=1;
					assigner.allocation[v].velocity=velocity;
					assigner.allocation[v].rootNote=note;
					assigner.allocation[v].note=n;
					assigner.allocation[v].timestamp=timestamp;

					p600_assignerEvent(n,1,assigner.allocation[v].voice,velocity,legato);
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
						assigner_voiceDone(assigner.allocation[v].voice);
					}
					else
					{
						p600_assignerEvent(assigner.allocation[v].note,0,assigner.allocation[v].voice,velocity,0);
					}
				}

			// restored notes can be assigned again

			if(restoredNote!=ASSIGNER_NO_NOTE)
			{
				redo=1;
				note=restoredNote;
				gate=1;
				velocity=oldVel;
				forceLegato=1;
			}
		}
	} while(redo);
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
		int8_t ai;
		
		ai=getAllocIdxFromVoice(voice);
		if(ai<0)
			return;
		
		assigner.allocation[ai].assigned=0;
		assigner.allocation[ai].note=ASSIGNER_NO_NOTE;
		assigner.allocation[ai].rootNote=ASSIGNER_NO_NOTE;
		assigner.allocation[ai].timestamp=0;
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
	uint8_t dummy[P600_VOICE_COUNT]={0,0,0,0,0,0};	
	assigner_setPattern(dummy,0);
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));
	assigner_setPoly();
}

