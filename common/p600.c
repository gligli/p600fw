////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "p600.h"

#include "scanner.h"
#include "display.h"
#include "synth.h"
#include "potmux.h"
#include "adsr.h"

#define P600_VOICE_COUNT 2

static struct
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];
	int8_t currentVoice;
} p600;

void p600_init(void)
{
	scanner_init();
	display_init();
	synth_init();
	potmux_init();

	int8_t i;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}
	
	sevenSeg_setAscii('H','i');
	led_set(plSeq1,0,0);
	led_set(plSeq2,1,0);
	led_set(plArpUD,1,1);
	led_set(plArpAssign,0,1);
	
	synth_update();
}

void p600_update(void)
{
	int8_t i;
	static uint8_t frc=0;
	
	// free running counter
	
	++frc;
	
	// toggle tape out (debug)

	int_clear();
	io_write(0x0e,((frc&1)<<2)|0b00110001);
	int_set();
	
	// which pots do we have to read?
	
	if((frc&0x01)==0) // 1/4 of the time, alternatively
	{
		potmux_need(ppAmpAtt,ppAmpDec,ppAmpSus,ppAmpRel,ppFilAtt,ppFilDec,ppFilSus,ppFilRel);
	}
	
	if((frc&0x03)==2) // 1/4 of the time, alternatively
	{
		potmux_need(ppMVol,ppMixer,ppGlide,ppResonance,ppFilEnvAmt,ppAPW,ppBPW);
	}

	potmux_need(ppCutoff,ppFreqA,ppFreqB);
	
	// read them
	
	potmux_update();
	
	// update CVs

	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_setCVs(&p600.ampEnvs[i],potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
		adsr_setCVs(&p600.filEnvs[i],potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),potmux_getValue(ppFilEnvAmt));
	}

	synth_setCV(pcMVol,potmux_getValue(ppMVol),1);
	synth_setCV(pcVolA,potmux_getValue(ppMixer),1);
	synth_setCV(pcVolB,potmux_getValue(ppGlide),1);
	synth_setCV(pcAPW,potmux_getValue(ppAPW),1);
	synth_setCV(pcBPW,potmux_getValue(ppBPW),1);
	synth_setCV(pcRes,potmux_getValue(ppResonance),1);
	
	synth_setCV(pcOsc1A,potmux_getValue(ppFreqA),1);
	synth_setCV(pcOsc1B,potmux_getValue(ppFreqB),1);
	
	if(!frc)
		synth_update(); // needs to be done from time to time, to keep S&Hs afloat
}

void p600_fastInterrupt(void)
{
	int8_t i,v;
	uint16_t cut;
	
	cut=potmux_getValue(ppCutoff);
	
	v=p600.currentVoice;
	
	for(i=0;i<P600_VOICE_COUNT/2;++i)
	{

		adsr_update(&p600.filEnvs[v]);
		adsr_update(&p600.ampEnvs[v]);

		synth_setCV(pcFil1+v,p600.filEnvs[v].final+cut,1);
		synth_setCV(pcAmp1+v,p600.ampEnvs[v].final,1);

		v=(v+1)%P600_VOICE_COUNT;
	}
			
	p600.currentVoice=v;
}

void p600_slowInterrupt(void)
{
	scanner_update(); // do this first (clears display)
	display_update();
}

void p600_buttonEvent(p600Button_t button, int pressed)
{
	int8_t i;
	
	sevenSeg_setNumber(button);
	led_set(plToTape,pressed,0);
	
	switch(button)
	{
	case pbASaw:
		synth_setGate(pgASaw,pressed);
		break;
	case pbBSaw:
		synth_setGate(pgBSaw,pressed);
		break;
	case pbATri:
		synth_setGate(pgATri,pressed);
		break;
	case pbBTri:
		synth_setGate(pgBTri,pressed);
		break;
	case pbASqr:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setShape(&p600.filEnvs[i],pressed);
		break;
	case pbBSqr:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setShape(&p600.ampEnvs[i],pressed);
		break;
	case pbFromTape:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setGate(&p600.ampEnvs[i],pressed);
		break;
	default:
		;
	}
	
}

void p600_keyEvent(uint8_t key, int pressed)
{
	int8_t i;

	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);

	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_setGate(&p600.ampEnvs[i],pressed);
		adsr_setGate(&p600.filEnvs[i],pressed);
	}
}

