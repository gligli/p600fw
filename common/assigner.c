////////////////////////////////////////////////////////////////////////////////
// Voice assigner
////////////////////////////////////////////////////////////////////////////////

#include "assigner.h"

#define ASSIGNER_INC_MODULO 16

static struct
{
	int8_t voiceAssigned[P600_VOICE_COUNT];
	uint8_t voiceNote[P600_VOICE_COUNT];
	int8_t assignInc;
	assignerMode_t mode;
} assigner;

static int8_t findOldest(uint8_t note)
{
	int8_t i,v=0,a,minInc=ASSIGNER_INC_MODULO;
	
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

static void setVoices(int8_t voice, int8_t assigned, uint8_t note)
{
	if(voice>=0)
	{
		assigner.voiceAssigned[voice]=assigned;
		assigner.voiceNote[voice]=note;

		p600_assignerEvent(note,voice);
	}
	else
	{
		for(voice=0;voice<P600_VOICE_COUNT;++voice)
			setVoices(voice,assigned,note);
	}
}

int8_t inline assigner_getAssignment(int8_t voice, uint8_t * note)
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
	case mUnison:
		return "Unison";
	case mMonoLow:
		return "Mono lo";
	case mMonoHigh:
		return "Mono hi";
	case mMonoLast:
		return "Mono last";
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
	case mUnison:
		return -1;
	case mMonoLow:
	case mMonoHigh:
	case mMonoLast:
		return assigner.assignInc%P600_VOICE_COUNT;
	}

	print("Warning: assigner_getVoiceFromNote found no voice!\n"); 
	return -2;
}

void assigner_setMode(assignerMode_t mode)
{
	setVoices(-1,-1,ASSIGNER_NO_NOTE);
	assigner.mode=mode;
}

void assigner_assignNote(uint8_t note)
{
	int8_t curVoice=-2,nextVoice=-2;
	uint8_t curNote=ASSIGNER_NO_NOTE;
	int8_t curAssigned=0;
	
	// in mono mode, use all voices, sequentially

	if(assigner.mode>=mMonoLow && assigner.mode<=mMonoLast)
	{
		curVoice=assigner.assignInc%P600_VOICE_COUNT;
		nextVoice=(assigner.assignInc+1)%P600_VOICE_COUNT;
		curNote=assigner.voiceNote[curVoice];
		curAssigned=assigner.voiceAssigned[curVoice]>=0;
	}
	
	// act depending on mode
	
	switch(assigner.mode)
	{
	case mPoly:
		nextVoice=findOldest(note);
		break;
	case mUnison:
		nextVoice=-1; // assign all voices
		break;
	case mMonoLow:
		if(curAssigned && note>curNote)
			nextVoice=-2; // dismiss note
		break;
	case mMonoHigh:
		if(curAssigned && note<curNote)
			nextVoice=-2; // dismiss note
		break;
	case mMonoLast:
		// nothing do do ...
		break;
	}
	
	if(nextVoice!=-2)
	{
		// in mono mode, deassign old voice

		if(assigner.mode>=mMonoLow && assigner.mode<=mMonoLast)
			assigner_voiceDone(curVoice);

		// assign new note

		assigner.assignInc=(assigner.assignInc+1)%ASSIGNER_INC_MODULO;
		setVoices(nextVoice,assigner.assignInc,note);
	}
}

void assigner_voiceDone(int8_t voice)
{
	if(assigner.mode==mUnison)
		setVoices(-1,-1,ASSIGNER_NO_NOTE);
	else
		setVoices(voice,-1,ASSIGNER_NO_NOTE);
}

void assigner_init(void)
{
	memset(&assigner,0,sizeof(assigner));
	assigner_voiceDone(-1); // init all voices to 'done'
}

