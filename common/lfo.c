////////////////////////////////////////////////////////////////////////////////
// LFO (low frequency oscillator)
////////////////////////////////////////////////////////////////////////////////

#include "lfo.h"

static uint16_t sineShape[256];
static uint16_t lfoSeq[16];  // NEW LFO shapes JHHL


static void updateIncrement(struct lfo_s * lfo)
{
	lfo->increment=lfo->speed*(1-lfo->halfPeriod*2);
}

static void updateSpeed(struct lfo_s * lfo)
{
	int32_t spd;
	
	spd=exponentialCourse(UINT16_MAX-lfo->speedCV,13000.0,65535.0f);
	spd<<=lfo->speedShift;
	
	lfo->speed=spd;
}

static void handlePhaseOverflow(struct lfo_s * l)
{
	l->halfPeriod=1-l->halfPeriod;
	l->phase=l->halfPeriod?0x00ffffff:0;

	updateIncrement(l);

	switch(l->shape)
	{
	case lsPulse:
		l->rawOutput=l->halfPeriod*UINT16_MAX;
		break;
	case lsRand:
		l->rawOutput=random();
		break;
	default:
		;
	}
}
// NEW LFO shapes JHHL
void LOWERCODESIZE newSeq(void)  //V2.29 remed out to see if this has an effect on the UI -- V2.33 added back in to see if changes UI speed
;

void LOWERCODESIZE lfo_setCVs(struct lfo_s * lfo, uint16_t spd, uint16_t lvl)
{
	lfo->levelCV=lvl;

	if(spd!=lfo->speedCV)
	{
		lfo->speedCV=spd;
		updateSpeed(lfo);
		updateIncrement(lfo);
	}
}

// NEW LFO shapes JHHL the idea here is to make up a little melody with some redundancy.

void LOWERCODESIZE newSeq(void)
{
    for(int i = 0 ; i<16;i++)
    {
        uint16_t r=random();
        
         if(i==0)
        {
            lfoSeq[i]=r;
        }
        else
        {
            // 3/8 of the time, choose a new pitch
            if((r&0x0007) <3)
            {
                lfoSeq[i]=random();
            }
            else
            {
                // 5/8ths of the time, repeat the last one
                if((r&0x0070)<0x0050)
                {
                    lfoSeq[i]=lfoSeq[i-1];
                }
                else
                {
                    int t = (r>>8)%i;
                    lfoSeq[i]=lfoSeq[t];
                }
            }
        }
    }
}


static lfoShape_t lastShape;

void LOWERCODESIZE lfo_setShape(struct lfo_s * lfo, lfoShape_t shape)
{
	lfo->shape=shape;
	
	if(lfo->noise==0)
		srandom(currentTick);
	
	lfo->noise=random();
    
    // NEW LFO shapes JHHL
    if((shape != lastShape) && (shape == lsSeq))
    {
        newSeq();
    }
    
    lastShape = shape;
}

void LOWERCODESIZE lfo_setSpeedShift(struct lfo_s * lfo, uint8_t shift)
{
	if(shift!=lfo->speedShift)
	{
		lfo->speedShift=shift;
		updateSpeed(lfo);
		updateIncrement(lfo);
	}

}

int16_t inline lfo_getOutput(struct lfo_s * lfo)
{
	return lfo->output;
}

const char * lfo_shapeName(lfoShape_t shape)
{
	switch(shape)
	{
	case lsPulse:
		return "pulse";
	case lsTri:
		return "tri";
	case lsRand:
		return "rand";
	case lsSine:
		return "sine";
	case lsNoise:
		return "noise";
	case lsSaw:
		return "saw";
// NEW LFO shapes JHHL
    case lsSeq:
        return "seq";
    case lsStep8:
        return "step8";
    case lsStep4:
        return "step4";
// from Overcycler code
    case lsRevSaw:
        return "rev-saw";
    case lsbitTri:
        return "bit-tri";
    case lsbitSine:
        return "bitSine";
	}
	
	return "";
}

void lfo_init(struct lfo_s * lfo)
{
	int16_t i;
	
	memset(lfo,0,sizeof(struct lfo_s));
	
	for(i=0;i<256;++i)
		sineShape[i]=(cosf((i/255.0f+1.0f)*M_PI)+1.0f)/2.0f*65535.0f;
}

// bit reduction quantitizer for bit-tri and bit-sine
short keep_bits_from_16(short input, int keepBits) {
    return (input & (-1 << (16-keepBits)));
}


inline void lfo_update(struct lfo_s * l)
{
	// if bit 24 or higher is set, it's an overflow -> a half period is done!
	
	if(l->phase>>24) 
		handlePhaseOverflow(l);
	
	// handle continuous shapes

	switch(l->shape)
	{
	case lsTri:
		l->rawOutput=l->phase>>8;
		break;
	case lsSine:
		l->rawOutput=computeShape(l->phase,sineShape,1);
		break;
	case lsNoise:
		l->noise=lfsr(l->noise,(l->speedCV>>12)+1);
		l->rawOutput=l->noise;
		break;
	case lsSaw:
		l->rawOutput=l->phase>>9;
		if(l->halfPeriod)
			l->rawOutput=UINT16_MAX-l->rawOutput;
		break;
//from GLIGLI Overcycler code
    case lsRevSaw:
        l->rawOutput=l->phase>>9;
        if(!l->halfPeriod)
            l->rawOutput=UINT16_MAX-l->rawOutput;
        break;
            
//New LFO shapes JHHL added version 2.20 and 2.21
    case lsSeq:
        l->rawOutput=l->phase>>9;
        if(l->halfPeriod)
            l->rawOutput=UINT16_MAX-l->rawOutput;
        
        l->rawOutput=lfoSeq[(l->rawOutput>>(16-4)) & 0x000F];
        break;
 
    case lsStep8:
        l->rawOutput=l->phase>>9;
        if(l->halfPeriod)
            l->rawOutput=UINT16_MAX-l->rawOutput;
        l->rawOutput&=0xE000;
        break;
            
    case lsStep4:
        l->rawOutput=l->phase>>9;
        if(l->halfPeriod)
            l->rawOutput=UINT16_MAX-l->rawOutput;
        l->rawOutput&=0x8000;
        break;
            
    // quantitized triangle and Sine added version 2.22
    case lsbitTri:
        l->rawOutput=l->phase>>8;
        l->rawOutput=keep_bits_from_16(l->rawOutput, 3);
        break;
    case lsbitSine:
        l->rawOutput=computeShape(l->phase,sineShape,1);
        l->rawOutput=keep_bits_from_16(l->rawOutput, 6);
        break;
            
    //Pink noise V2.33 --- note sounds no different than white noise for LFO so I remmed it out
   // case lsPkNoise:
   //     l->noise=lfsr(l->noise,(l->speedCV>>12)+1);
   //     l->rawOutput=LPF(l->noise);
    //    break;
            
    default:
        ;
	}
	
	// phase increment
    // revised by New LFO shapes JHHL
   //  l->phase+=l->increment;  //rem out for new LFO waveform Seq
    
    switch(l->shape)
    {
        default:
            l->phase+=l->increment;
            break;
            //New LFO shapes
        case lsSeq:
            l->phase+=(l->increment>>4);
            break;
        case lsStep8:
            l->phase+=(l->increment>>3);
            break;
        case lsStep4:
            l->phase+=(l->increment>>2);
            break;
    }
    

	// compute output
	
	l->output=scaleU16S16(l->levelCV,(int32_t)l->rawOutput+INT16_MIN); // code below this line causes UI slowdown

    /*   this is for the SEQ waveform
     int32_t o;
    
    o=l->rawOutput;
    o+=INT16_MIN;
    o*=l->levelCV;
    o/=UINT16_MAX;
    l->output=o;
     */
}


