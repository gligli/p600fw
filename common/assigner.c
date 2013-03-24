////////////////////////////////////////////////////////////////////////////////
// Voice assigner
////////////////////////////////////////////////////////////////////////////////

#include "assigner.h"

#define LEGATO_NOTE_MEMORY 8

static struct
{
	int8_t voiceAssigned[P600_VOICE_COUNT];
	uint8_t voiceNote[P600_VOICE_COUNT];
	
	uint8_t legatoNotes[LEGATO_NOTE_MEMORY];
	
	int8_t assignInc;
	assignerMode_t mode;
} assigner;

static inline int8_t legatoIsPrio(uint8_t basePrioNote, uint8_t newNote)
{
	return ((assigner.mode==mUnisonLow || assigner.mode==mMonoLow) && newNote<=basePrioNote) ||
			((assigner.mode==mUnisonHigh || assigner.mode==mMonoHigh) && newNote>=basePrioNote);
}

static inline void legatoGetState(int8_t * count, uint8_t * mostPrio)
{
	int8_t i,c;
	uint8_t n,mp;
	
	c=0;
	mp=ASSIGNER_NO_NOTE;
	
	// compute legato prioritized notes and note count

	for(i=0;i<LEGATO_NOTE_MEMORY;++i)
	{
		n=assigner.legatoNotes[i];
		if(n!=ASSIGNER_NO_NOTE)
		{
			if(!c || legatoIsPrio(mp,n))
				mp=n;

			++c;
		}
	}
	
	*mostPrio=mp;
	*count=c;
}

static int8_t findOldest(uint8_t note)
{
	int8_t i,v=0,a,minInc=LEGATO_NOTE_MEMORY;
	
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		a=assigner.voiceAssigned[i];
		
		if(assigner.voiceNote[i]==note || a<0)
		{
			v=i;
			break;
		}
		
		if(a<minInc || a>minInc+P600_VOICE_COUNT)
		{
			minInc=a;
			v=i;
		}
	}
	
	return v;
}

static void setVoices(int8_t voice, int8_t assigned, uint8_t note, int8_t gate)
{
	if(voice>=0)
	{
		assigner.voiceAssigned[voice]=assigned;
		assigner.voiceNote[voice]=note;
	}
	else if(voice==-1)
	{
		int8_t v;
		
		for(v=0;v<P600_VOICE_COUNT;++v)
		{
			assigner.voiceAssigned[v]=assigned;
			assigner.voiceNote[v]=note;
		}
	}

	if(note!=ASSIGNER_NO_NOTE)
		p600_assignerEvent(note,gate,voice);
}

inline int8_t assigner_getAssignment(int8_t voice, uint8_t * note)
{
	if(note)
		*note=assigner.voiceNote[voice];
	
	return assigner.voiceAssigned[voice]>=0;
}

const char * assigner_modeName(assignerMode_t mode)
{
	switch(mode)
	{
	case mPoly:
		return "Poly";
	case mUnisonLow:
		return "Unison lo";
	case mUnisonHigh:
		return "Unison hi";
	case mMonoLow:
		return "Mono lo";
	case mMonoHigh:
		return "Mono hi";
	}
	
	return "";
}

int8_t assigner_getVoiceFromNote(uint8_t note)
{
	int8_t i;

	switch(assigner.mode)
	{
	case mPoly:
		for(i=0;i<P600_VOICE_COUNT;++i)
			if(assigner.voiceAssigned[i]>=0 && assigner.voiceNote[i]==note)
				return i;
		break;
	case mUnisonLow:
	case mUnisonHigh:
		return -1;
	case mMonoLow:
	case mMonoHigh:
		return assigner.assignInc%P600_VOICE_COUNT;
	}

	print("Warning: assigner_getVoiceFromNote found no voice!\n"); 
	return -2;
}

inline void assigner_setMode(assignerMode_t mode)
{
	if (mode!=assigner.mode)
	{
		assigner_init();
		assigner.mode=mode;
	}
}

inline assignerMode_t assigner_getMode(void)
{
	return assigner.mode;
}

void assigner_assignNote(uint8_t note, int8_t on)
{
	int8_t curVoice=-2,nextVoice=-2;
	
	// act depending on mode
	
	nextVoice=-1;

	if(assigner.mode==mPoly)
	{
		if(on)
		{
			nextVoice=findOldest(note);
			
			// assign new note

			assigner.assignInc=(assigner.assignInc+1)%P600_VOICE_COUNT;
			setVoices(nextVoice,assigner.assignInc,note,1);
		}
		else
		{
			curVoice=assigner_getVoiceFromNote(note);
			
			if(curVoice!=-2)
				setVoices(curVoice,assigner_getAssignment(curVoice,NULL),note,0);
		}
	}
	else
	{
		int8_t i;
		int8_t legatoCount, isMono,gate;
		uint8_t legatoMostPrio;
	
		if(on)
		{
			// add the note the list of legato notes
			
			for(i=0;i<LEGATO_NOTE_MEMORY;++i)
				if(assigner.legatoNotes[i]==ASSIGNER_NO_NOTE)
				{
					assigner.legatoNotes[i]=note;
					break;
				}
		}
		else
		{
			// remove the note from the list of legato notes
			
			for(i=0;i<LEGATO_NOTE_MEMORY;++i)
				if(assigner.legatoNotes[i]==note)
				{
					assigner.legatoNotes[i]=ASSIGNER_NO_NOTE;
					break;
				}
		}
		
		isMono=assigner.mode==mMonoLow || assigner.mode==mMonoHigh;
		
		// handle legato, play prioritized note, or none if we're done

		legatoGetState(&legatoCount,&legatoMostPrio);
			
		if(legatoCount)
		{
			note=legatoMostPrio;
		}

		gate=legatoCount?1:0;
			
		if(isMono)
		{
			if(legatoCount==1 && on) // detect new gate while all gates off
			{
				// deassign old voice
				assigner_voiceDone(assigner.assignInc);

				// change next voice
				assigner.assignInc=(assigner.assignInc+1)%P600_VOICE_COUNT;
			}
			
			nextVoice=assigner.assignInc;
		}

		// assign new note
		setVoices(nextVoice,0,note,gate);
	}
}

void assigner_voiceDone(int8_t voice)
{
	if(assigner.mode!=mPoly)
		setVoices(-1,-1,ASSIGNER_NO_NOTE,0);
	else
		setVoices(voice,-1,ASSIGNER_NO_NOTE,0);
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));
	assigner_voiceDone(-1); // init all voices to 'done'
	memset(&assigner.legatoNotes,ASSIGNER_NO_NOTE,LEGATO_NOTE_MEMORY);
}

