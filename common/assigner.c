////////////////////////////////////////////////////////////////////////////////
// Voice assigner
////////////////////////////////////////////////////////////////////////////////

#include "assigner.h"

#define LEGATO_NOTE_MEMORY 8

static struct
{
	uint32_t voiceTimestamp[P600_VOICE_COUNT];
	uint8_t voiceNote[P600_VOICE_COUNT];
	
	uint8_t legatoNotes[LEGATO_NOTE_MEMORY];
	
	uint32_t timestamp;
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
	int8_t i,v=-2;
	uint32_t a,minTs=UINT32_MAX;
	
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		a=assigner.voiceTimestamp[i];
		
		if(assigner.voiceNote[i]==note)
		{
			// playing the same note again should stay on the same voice
			v=i;
			break;
		}
		
		if(a<minTs)
		{
			minTs=a;
			v=i;
		}
	}
	
	return v;
}

static void setVoices(int8_t voice, uint32_t timestamp, uint8_t note, int8_t gate, uint16_t velocity)
{
	if(voice>=0)
	{
		assigner.voiceTimestamp[voice]=timestamp;
		assigner.voiceNote[voice]=note;
	}
	else if(voice==-1)
	{
		int8_t v;
		
		for(v=0;v<P600_VOICE_COUNT;++v)
		{
			assigner.voiceTimestamp[v]=timestamp;
			assigner.voiceNote[v]=note;
		}
	}

	if(note!=ASSIGNER_NO_NOTE)
		p600_assignerEvent(note,gate,voice,velocity);
}

static int8_t getVoiceFromNote(uint8_t note)
{
	int8_t v;

	switch(assigner.mode)
	{
	case mPoly:
		for(v=0;v<P600_VOICE_COUNT;++v)
			if(assigner.voiceNote[v]==note && assigner.voiceTimestamp[v]==UINT32_MAX)
				return v;
		break;
	case mUnisonLow:
	case mUnisonHigh:
		return -1;
	case mMonoLow:
	case mMonoHigh:
		return assigner.timestamp%P600_VOICE_COUNT;
	}

#ifdef DEBUG
	print("Warning: assigner_getVoiceFromNote found no voice!\n"); 
#endif	
	return -2;
}

inline int8_t assigner_getAssignment(int8_t voice, uint8_t * note)
{
	if(note)
		*note=assigner.voiceNote[voice];
	
	return assigner.voiceTimestamp[voice]!=0;
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

void assigner_assignNote(uint8_t note, int8_t on, uint16_t velocity)
{
	int8_t voice=-1;
	
	// act depending on mode
	
	if(assigner.mode==mPoly)
	{
		if(on)
		{
			voice=findOldest(note);
			
			// on note on, if we have less than 6 pressed notes (timestamp=UINT32_MAX), assign new note
			
			if (voice>=0)
				setVoices(voice,UINT32_MAX,note,1,velocity);
		}
		else
		{
			voice=getVoiceFromNote(note);
			
			// on note off, assign a timestamp, so that oldest note in release gets stolen in case there's more than 6 still playing
			
			if(voice>=0)
			{
				// steal the voice if it's assigned
				if(assigner_getAssignment(voice,NULL))
					assigner_voiceDone(voice);

				setVoices(voice,++assigner.timestamp,note,0,velocity);
			}
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
				assigner_voiceDone(assigner.timestamp);

				// change next voice
				assigner.timestamp=(assigner.timestamp+1)%P600_VOICE_COUNT;
			}
			
			voice=assigner.timestamp;
		}

		// assign new note
		setVoices(voice,1,note,gate,velocity);
	}
}

void assigner_voiceDone(int8_t voice)
{
	if(assigner.mode!=mPoly)
		setVoices(-1,0,ASSIGNER_NO_NOTE,0,0);
	else
		setVoices(voice,0,ASSIGNER_NO_NOTE,0,0);
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));
	assigner_voiceDone(-1); // init all voices to 'done'
	memset(&assigner.legatoNotes,ASSIGNER_NO_NOTE,LEGATO_NOTE_MEMORY);
}

