////////////////////////////////////////////////////////////////////////////////
// Original Z80 stuff importer
////////////////////////////////////////////////////////////////////////////////

#include "import.h"

#include "storage.h"

const uint8_t z80EnvCV[16]=
{
	0,55,86,101,111,124,135,139,143,148,154,164,176,185,191,210
};

__attribute__((packed)) struct z80Patch_t
{
	// pots
	uint16_t pwA : 7;
	uint16_t pmodFilEnv : 4;
	uint16_t lfoFreq : 4;
	uint16_t pmodOscB : 7;
	uint16_t lfoAmt : 5;
	uint16_t freqB : 6;
	uint16_t freqA : 6;
	uint16_t fineB : 7;
	uint16_t mixer : 6;
	uint16_t cutoff : 7;
	uint16_t reso : 6;
	uint16_t filEnvAmt : 4;
	uint16_t filRel : 4;
	uint16_t filSus : 4;
	uint16_t filDec : 4;
	uint16_t filAtk : 4;
	uint16_t ampRel : 4;
	uint16_t ampSus : 4;
	uint16_t ampDec : 4;
	uint16_t ampAtk : 4;
	uint16_t glide : 4;
	uint16_t pwB : 7;
	// switches
	uint16_t pulseA : 1;
	uint16_t pulseB : 1;
	uint16_t trackFull : 1;
	uint16_t trackHalf : 1;
	uint16_t lfoShape : 1;
	uint16_t lfoPitch : 1;
	uint16_t lfoPW : 1;
	uint16_t lfoFil : 1;
	uint16_t sawA : 1;
	uint16_t triA : 1;
	uint16_t syncA : 1;
	uint16_t sawB : 1;
	uint16_t triB : 1;
	uint16_t pmodFreqA : 1;
	uint16_t pmodFil : 1;
	uint16_t unison : 1;
};

LOWERCODESIZE void import_sysex(uint8_t * buf, int16_t size)
{
	int8_t presetNumber,i;
	uint8_t patchData[16];
	struct z80Patch_t * zp = (struct z80Patch_t *)patchData;
	struct preset_s p,savedP;
	uint16_t tmp;
	
	// basic checks
	
	if(size!=35 || buf[0]!=0x01 || buf[1]!=0x02 || buf[2]>0x63)
		return;

	// get raw patch data
	
	presetNumber=buf[2];
	for(i=0;i<16;++i)
		patchData[i]=(buf[i*2+3] & 0x0f) | (buf[i*2+4] << 4);

#ifdef DEBUG
	print("syx imp ");
	phex(presetNumber);
	print(" = ");
	for(i=0;i<16;++i)
		phex(patchData[i]);
	print("\n");
#endif	

	// import patch

	tmp=zp->mixer<<10;
	p.continuousParameters[cpVolA]=UINT16_MAX-tmp;
	p.continuousParameters[cpVolB]=tmp;
	p.continuousParameters[cpFreqA]=zp->freqA<<10;
	p.continuousParameters[cpFreqB]=zp->freqB<<10;
	p.continuousParameters[cpFreqBFine]=HALF_RANGE+(zp->fineB<<8);
	p.continuousParameters[cpAPW]=zp->pwA<<9;
	p.continuousParameters[cpBPW]=zp->pwB<<9;
	p.continuousParameters[cpCutoff]=zp->cutoff<<8;
	p.continuousParameters[cpResonance]=zp->reso<<10;
	p.continuousParameters[cpFilEnvAmt]=HALF_RANGE+(zp->filEnvAmt<<11);
	p.continuousParameters[cpFilRel]=z80EnvCV[zp->filRel]<<8;
	p.continuousParameters[cpFilSus]=zp->filSus<<12;
	p.continuousParameters[cpFilDec]=z80EnvCV[zp->filDec]<<8;
	p.continuousParameters[cpFilAtt]=z80EnvCV[zp->filAtk]<<8;
	p.continuousParameters[cpAmpRel]=z80EnvCV[zp->ampRel]<<8;
	p.continuousParameters[cpAmpSus]=zp->ampSus<<12;
	p.continuousParameters[cpAmpDec]=z80EnvCV[zp->ampDec]<<8;
	p.continuousParameters[cpAmpAtt]=z80EnvCV[zp->ampAtk]<<8;
	p.continuousParameters[cpPModFilEnv]=HALF_RANGE+(zp->pmodFilEnv<<11);
	p.continuousParameters[cpPModOscB]=zp->pmodOscB<<9;
	p.continuousParameters[cpLFOFreq]=(uint16_t)(0.615385f*(float)(zp->lfoFreq<<10))+8570; // from version 8 on rescaled according to legacy setting "slow"
	p.continuousParameters[cpLFOAmt]=(currentPreset.continuousParameters[cpLFOAmt]<=512)?0:512+(uint16_t)(15000.0f*log((((float)((zp->lfoAmt<<4)-512))/870.0f)+1));
	p.continuousParameters[cpGlide]=(zp->glide)?(0xc000+(zp->glide<<10)):0; // this is not quite faithful compared to the Z80 (in this version glide time is longer)
	p.continuousParameters[cpAmpVelocity]=0;
	p.continuousParameters[cpFilVelocity]=0;

	p.steppedParameters[spASaw]=zp->sawA;
	p.steppedParameters[spATri]=zp->triA;
	p.steppedParameters[spASqr]=zp->pulseA;
	p.steppedParameters[spBSaw]=zp->sawB;
	p.steppedParameters[spBTri]=zp->triB;
	p.steppedParameters[spBSqr]=zp->pulseB;
	p.steppedParameters[spSync]=zp->syncA;
	p.steppedParameters[spPModFA]=zp->pmodFreqA;
	p.steppedParameters[spPModFil]=zp->pmodFil;
	p.steppedParameters[spLFOShape]=zp->lfoShape;
	p.steppedParameters[spEnvRouting]=0;
	p.steppedParameters[spLFOTargets]=zp->lfoPitch | (zp->lfoPW<<1) | (zp->lfoFil<<2);
	p.steppedParameters[spTrackingShift]=(zp->trackHalf?1:0) + (zp->trackFull?2:0);
	p.steppedParameters[spFilEnvShape]=0;
	p.steppedParameters[spFilEnvSlow]=1;
	p.steppedParameters[spAmpEnvShape]=0;
	p.steppedParameters[spAmpEnvSlow]=1;
	p.steppedParameters[spUnison]=zp->unison;
	p.steppedParameters[spAssignerPriority]=apLast;
	p.steppedParameters[spBenderSemitones]=3;
	p.steppedParameters[spBenderTarget]=modAB;
	p.steppedParameters[spLFOSync]=0;
	p.steppedParameters[spChromaticPitch]=1;

	// save it
	
	BLOCK_INT
	{
		savedP=currentPreset;
		currentPreset=p;		
		
		preset_saveCurrent(presetNumber);

		currentPreset=savedP;
	}
}



