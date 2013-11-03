unit p600emuclasses;

{$mode delphi}

interface

uses
  Classes, SysUtils, raze, LazLogger, math, windows;

type
  TP600Pot=(ppMixer=0,ppCutoff=1,ppResonance=2,ppFilEnvAmt=3,ppFilRel=4,ppFilSus=5,
            ppFilDec=6,ppFilAtt=7,ppAmpRel=8,ppAmpSus=9,ppAmpDec=10,ppAmpAtt=11,
            ppGlide=12,ppBPW=13,ppMVol=14,ppMTune=15,ppPitchWheel=16,ppModWheel=22,
            ppSpeed,ppAPW,ppPModFilEnv,ppLFOFreq,ppPModOscB,ppLFOAmt,ppFreqB,ppFreqA,ppFreqBFine);

  TP600LED=(plSeq1=0,plSeq2=1,plArpUD=2,plArpAssign=3,plPreset=4,plRecord=5,plToTape=6,plFromTape=7,plTune=8,plDot=9);
  TP600LEDStates=set of TP600LED;

  TP600CV=(pcOsc1A=0,pcOsc2A,pcOsc3A,pcOsc4A,pcOsc5A,pcOsc6A,
           pcOsc1B=6,pcOsc2B,pcOsc3B,pcOsc4B,pcOsc5B,pcOsc6B,
           pcFil1=12,pcFil2,pcFil3,pcFil4,pcFil5,pcFil6,
           pcAmp1=18,pcAmp2,pcAmp3,pcAmp4,pcAmp5,pcAmp6,
           pcPModOscB=24,pcVolA,pcVolB,pcMVol,pcAPW,pcExtFil,pcRes,pcBPW);

  TP600Gate=(pgASaw=0,pgATri,pgSync,pgBSaw,pgBTri,pgPModFA,pgPModFil);

  TP600Button=(pb0=0,pb1,pb2,pb3,pb4,pb5,pb6,pb7,
               pb8=8,pb9,pbArpUD,pbArpAssign,pbPreset,pbRecord,pbToTape,pbFromTape,
               pbSeq1=16,pbSeq2,pbTune,
               pbASqr=24,pbBSqr,pbFilFull,pbFilHalf,pbLFOShape,pbLFOFreq,pbLFOPW,pbLFOFil,
               pbASaw=32,pbATri,pbSync,pbBSaw,pbBTri,pbPModFA,pbPModFil,pbUnisson);

  { T8253Channel }

  T8253Channel=record
    Start:Word;
    Counter:Integer;
    Mode:Byte;
    AccessingLSB:Boolean;
    Gate:Boolean;
    Output:Boolean;
    LoadCounterPending:Boolean;

    procedure LoadCounter;
  end;

  { T8253Timer }

  T8253Timer=record
  private
    FControlWord:Byte;
    FChannels:array[0..2] of T8253Channel;
  public
    procedure WriteReg(AReg,AValue:Byte);
    function ReadReg(AReg:Byte):Byte;
    procedure RunCycles(AChannel:Byte;ACount:Integer);
    function GetOutput(AChannel:Byte):Boolean;
    procedure SetGate(AChannel:Byte;AGate:Boolean);
  end;

  { TProphet600Hardware }

  TProphet600Hardware=record
  private
    FIOCSData:array[0..7] of Byte;

    F8253:T8253Timer;

    FFFStatus:Boolean;
    FFFD:Boolean;
    FFFP:Boolean;
    FFFCL:Boolean;

    FDACValue:Word;
    FMuxedPot:TP600Pot;
    FDACDemux:Byte;

    FIncompleteCycles:Double;

    FRom:array[0..8191] of Byte;
    FRam1,FRam2:array[0..2047] of Byte;
    FPotValues:array[TP600Pot] of Word;
    FOldDisplay:array[0..2] of Byte;
    FDisplay:array[0..2] of Byte;
    FCVValues:array[TP600CV] of Word;
    FKeyStates:array[0..127] of Boolean;

    function ADCCompare:Boolean;
    procedure UpdateCVs;
    procedure UpdateFFStatus(AClockTick:Boolean);

    // getters/setters
    function GetCVValues(ACV: TP600CV): Word;
    function GetCVVolts(ACV: TP600CV): Double;
    function GetCVHertz(ACV: TP600CV): Double;
    function GetGateValues(AGate: TP600Gate): Boolean;
    function GetLEDStates: TP600LEDStates;
    function GetPotValues(APot: TP600Pot): Word;
    function GetSevenSegment(AIndex: Integer): Byte;
    procedure SetButtonStates(AButton: TP600Button; AValue: Boolean);
    procedure SetKeyStates(AKey: Integer; AValue: Boolean);
    procedure SetPotValues(APot: TP600Pot; AValue: Word);
  public
    procedure Initialize;

    procedure LoadRomFromFile(AFileName:String);
    procedure Write(AIsIO:Boolean;AAddress:Word;AValue:Byte);
    function Read(AIsIO:Boolean;AAddress:Word):Byte;

    procedure RunCycles(ACount:Integer); // run ACount 4Mhz cycles

    property PotValues[APot:TP600Pot]:Word read GetPotValues write SetPotValues;
    property SevenSegment[AIndex:Integer]:Byte read GetSevenSegment;
    property LEDStates:TP600LEDStates read GetLEDStates;

    property ButtonStates[AButton:TP600Button]:Boolean write SetButtonStates;
    property KeyStates[AKey:Integer]:Boolean write SetKeyStates;

    property CVValues[ACV:TP600CV]:Word read GetCVValues;
    property CVVolts[ACV:TP600CV]:Double read GetCVVolts;
    property CVHertz[ACV:TP600CV]:Double read GetCVHertz;
    property GateValues[AGate:TP600Gate]:Boolean read GetGateValues;
  end;


  { TProphet600Emulator }

  TProphet600Emulator=record
  private
    FHW:TProphet600Hardware;
  public
    procedure Initialize;

    procedure Tick; // advance one 5ms tick

    property HW:TProphet600Hardware read FHW;
  end;

  { TProphet600Mockup }

  TMockupInit=procedure(AWrite,ARead,ADebug:Pointer);stdcall;
  TMockupStart=procedure;stdcall;

  TProphet600Mockup=record
  private
    FHW:TProphet600Hardware;
    FInit:TMockupInit;
    FStart:TMockupStart;
  public
    procedure Initialize;

    procedure Tick;

    property HW:TProphet600Hardware read FHW;
  end;
var
{$if 1}
  P600Emu:TProphet600Emulator;
{$else}
  P600Emu:TProphet600Mockup;
{$endif}

const
  CTickMilliseconds=5;

implementation

const
  CZ80Frequency=4000000;
  CZ80CyclesPerTick=(CZ80Frequency div 1000) * CTickMilliseconds;
  CEmulationQuantum=4;

procedure P600Emu_WriteMem(AAddress:Word;AValue:Byte);cdecl;
begin
  P600Emu.HW.Write(False,AAddress,AValue);
end;

function P600Emu_ReadMem(AAddress:Word):Byte;cdecl;
begin
  Result:=P600Emu.HW.Read(False,AAddress);
end;

procedure P600Emu_WriteIO(AAddress:Word;AValue:Byte);cdecl;
begin
  P600Emu.HW.Write(True,AAddress,AValue);
end;

function P600Emu_ReadIO(AAddress:Word):Byte;cdecl;
begin
  Result:=P600Emu.HW.Read(True,AAddress);
end;

procedure P600Mockup_Write(AIsIO:Byte;AAddress:Word;AData:Byte);stdcall;
begin
  P600Emu.HW.Write(AIsIO<>0,AAddress,AData);
end;

function P600Mockup_Read(AIsIO:Byte;AAddress:Word):Byte;stdcall;
begin
  Result:=P600Emu.HW.Read(AIsIO<>0,AAddress);
end;

procedure P600Mockup_Debug(AChar:AnsiChar);stdcall;
begin
  DbgOut([AChar]);
end;

{ TProphet600Mockup }

procedure TProphet600Mockup.Initialize;
var hi:HINST;
begin
  HW.Initialize;
  HW.LoadRomFromFile(ExtractFilePath(ParamStr(0))+'p600.bin');

  hi:=LoadLibrary('p600mockup.dll');
  FInit:=GetProcAddress(hi,'emu_init@12');
  FStart:=GetProcAddress(hi,'emu_start@0');

  FInit(@P600Mockup_Write,@P600Mockup_Read,@P600Mockup_Debug);
end;

procedure TProphet600Mockup.Tick;
begin
  FStart;
end;

{ T8253Channel }

procedure T8253Channel.LoadCounter;
begin
  if Start=0 then
    Counter:=65536
  else
    Counter:=Start;

  LoadCounterPending:=False;
end;

{ T8253Timer }

procedure T8253Timer.WriteReg(AReg, AValue: Byte);
var cmd:Byte;
begin
//  DebugLn(['W ',Areg,' ',AValue,' ',binStr(AValue,8)]);
  if AReg = $03 then
  begin
    // control word
    FControlWord:=AValue;

    FChannels[AValue shr 6].Mode:=(AValue shr 1) and $07;

    // output is initially high except mode 0
    FChannels[AValue shr 6].Output:=FChannels[AValue shr 6].Mode<>0;

    FChannels[AValue shr 6].AccessingLSB:=True;
    FChannels[AValue shr 6].LoadCounterPending:=False;
  end
  else
  begin
    cmd:=(FControlWord shr 4) and $03;

    case cmd of
      0:
        Assert(False); //TODO: counter latch command
      1:
      begin
        FChannels[AReg].Start:=(FChannels[AReg].Start and $ff00) or AValue;
      end;
      2:
      begin
        FChannels[AReg].Start:=(FChannels[AReg].Start and $00ff) or (AValue shl 8);
      end;
      3:
      begin
        if FChannels[AReg].AccessingLSB then
          FChannels[AReg].Start:=(FChannels[AReg].Start and $ff00) or AValue
        else
          FChannels[AReg].Start:=(FChannels[AReg].Start and $00ff) or (AValue shl 8);

        FChannels[AReg].AccessingLSB:=not FChannels[AReg].AccessingLSB;
      end;
    end;

    if (FChannels[AReg].Mode=0) and ((cmd<>3) or FChannels[AReg].AccessingLSB) then // interrupt on terminal count
      FChannels[AReg].LoadCounterPending:=True;
  end;
end;

function T8253Timer.ReadReg(AReg: Byte): Byte;
var cmd:Byte;
    wrdCtr:Word;
begin
  if AReg=$03 then
  begin
    Assert(false);
  end
  else
  begin
    cmd:=(FControlWord shr 4) and $03;

    wrdCtr:=FChannels[AReg].Counter-1;

    case cmd of
      0:
        Assert(False); //TODO: counter latch command
      1:
      begin
        Result:=wrdCtr and $ff;
      end;
      2:
      begin
        Result:=(wrdCtr and $ff00) shr 8;
      end;
      3:
      begin
        if FChannels[AReg].AccessingLSB then
          Result:=wrdCtr and $ff
        else
          Result:=(wrdCtr and $ff00) shr 8;

        FChannels[AReg].AccessingLSB:=not FChannels[AReg].AccessingLSB;
      end;
    end;
  end;

//  DebugLn(['R ',Areg,' ',Result,' ',binStr(Result,8)]);
end;

procedure T8253Timer.RunCycles(AChannel: Byte; ACount: Integer);
begin
  case FChannels[AChannel].Mode of
    0: // interrupt on terminal count
    begin
      if (ACount>0) and (FChannels[AChannel].LoadCounterPending) then
      begin
        FChannels[AChannel].LoadCounter;
        Dec(ACount);
      end;

      if not FChannels[AChannel].Gate or (FChannels[AChannel].Counter=0) then
        Exit; // disable counting
    end;
    1:
    begin
      if (ACount>0) and (FChannels[AChannel].LoadCounterPending) then
      begin
        FChannels[AChannel].LoadCounter;
        FChannels[AChannel].Output:=False;
        Dec(ACount);
      end;
    end;
  end;

  dec(FChannels[AChannel].Counter,ACount);

  if FChannels[AChannel].Counter<=0 then
  begin
    case FChannels[AChannel].Mode of
      0,1: // interrupt on terminal count / one shot
      begin
        FChannels[AChannel].Counter:=0;
        FChannels[AChannel].Output:=True
      end;
      else
      begin
        Assert(False); // TODO
      end;
    end;
  end;
end;

function T8253Timer.GetOutput(AChannel: Byte): Boolean;
begin
  Result:=FChannels[AChannel].Output;
end;

procedure T8253Timer.SetGate(AChannel: Byte; AGate: Boolean);
var goHi:Boolean;
begin
  goHi:=not FChannels[AChannel].Gate and AGate;
  FChannels[AChannel].Gate:=AGate;

  if goHi and (FChannels[AChannel].Mode=1) then // one shot
  begin
    FChannels[AChannel].LoadCounterPending:=True;
  end;
end;

{ TProphet600Emulator }

procedure TProphet600Emulator.Initialize;
begin
  HW.Initialize;
  HW.LoadRomFromFile(ExtractFilePath(ParamStr(0))+'p600.bin');

  z80_init_memmap;
  z80_map_fetch($0000,$1fff,@HW.FRom[0]);
  z80_add_read($0000,$ffff,Z80_MAP_HANDLED,@P600Emu_ReadMem);
  z80_add_write($0000,$ffff,Z80_MAP_HANDLED,@P600Emu_WriteMem);
  z80_end_memmap;

  z80_set_in(@P600Emu_ReadIO);
  z80_set_out(@P600Emu_WriteIO);

  z80_reset;
end;

procedure TProphet600Emulator.Tick;
var i:Integer;
begin
  for i:=0 to CZ80CyclesPerTick div CEmulationQuantum - 1 do
  begin
    z80_emulate(CEmulationQuantum);
    HW.RunCycles(CEmulationQuantum);
  end;

  z80_raise_IRQ($ff);
  z80_lower_IRQ;
end;

{ TProphet600Hardware }

function TProphet600Hardware.GetLEDStates: TP600LEDStates;
var v0,v1,v2:Byte;
begin
  Result:=[];

  v0:=FDisplay[0] or FOldDisplay[0];
  v1:=FDisplay[1] or FOldDisplay[1];
  v2:=FDisplay[2] or FOldDisplay[2];

  if v0 and $01 <> 0 then Result:=Result + [plSeq1];
  if v0 and $02 <> 0 then Result:=Result + [plSeq2];
  if v0 and $04 <> 0 then Result:=Result + [plArpUD];
  if v0 and $08 <> 0 then Result:=Result + [plArpAssign];
  if v0 and $10 <> 0 then Result:=Result + [plPreset];
  if v0 and $20 <> 0 then Result:=Result + [plRecord];
  if v0 and $40 <> 0 then Result:=Result + [plToTape];
  if v0 and $80 <> 0 then Result:=Result + [plFromTape];
  if v1 and $80 <> 0 then Result:=Result + [plDot];
  if v2 and $80 <> 0 then Result:=Result + [plTune];
end;

function TProphet600Hardware.ADCCompare: Boolean;
begin
  Result:=FPotValues[FMuxedPot]>(65535-FDACValue);
end;

procedure TProphet600Hardware.UpdateCVs;
var reg:Byte;
begin
  reg:=FDACDemux and $7;

  if FDACDemux and $08 = 0 then
    FCVValues[TP600CV(reg+$00)]:=(65535-FDACValue);
  if FDACDemux and $10 = 0 then
    FCVValues[TP600CV(reg+$08)]:=(65535-FDACValue);
  if FDACDemux and $20 = 0 then
    FCVValues[TP600CV(reg+$10)]:=(65535-FDACValue);
  if FDACDemux and $40 = 0 then
    FCVValues[TP600CV(reg+$18)]:=(65535-FDACValue);
end;

procedure TProphet600Hardware.UpdateFFStatus(AClockTick: Boolean);
var prev:Boolean;
begin
  prev:=FFFStatus;

  if AClockTick then
    FFFStatus:=not FFFD;

  if not FFFP then
    FFFStatus:=False;

  if not FFFCL then
    FFFStatus:=True;

  if prev and not FFFStatus then // 8253 ticks on falling edge of clock
    F8253.RunCycles(2,1);
end;

function TProphet600Hardware.GetCVHertz(ACV: TP600CV): Double;
begin
  Result:=(27.5)*power(2.0,CVVolts[ACV]/0.5);
end;

function TProphet600Hardware.GetGateValues(AGate: TP600Gate): Boolean;
begin
  case AGate of
    pgASaw:
      Result:= FIOCSData[3] and $01 <> 0;
    pgATri:
      Result:= FIOCSData[3] and $02 <> 0;
    pgSync:
      Result:= FIOCSData[3] and $04 <> 0;
    pgBSaw:
      Result:= FIOCSData[3] and $08 <> 0;
    pgBTri:
      Result:= FIOCSData[3] and $10 <> 0;
    pgPModFA:
      Result:= FIOCSData[3] and $20 <> 0;
    pgPModFil:
      Result:= FIOCSData[3] and $40 <> 0;
  end;
end;

function TProphet600Hardware.GetCVValues(ACV: TP600CV): Word;
begin
  Result:=FCVValues[ACV];
end;

function TProphet600Hardware.GetCVVolts(ACV: TP600CV): Double;
begin
  Result:=(CVValues[ACV] / 65535.0) * 5.0;
end;

function TProphet600Hardware.GetPotValues(APot: TP600Pot): Word;
begin
  Result:=FPotValues[APot];
end;

function TProphet600Hardware.GetSevenSegment(AIndex: Integer): Byte;
begin
  Result:=(FDisplay[AIndex+1] or FOldDisplay[AIndex+1]) and $7f;
end;

procedure TProphet600Hardware.SetButtonStates(AButton: TP600Button;
  AValue: Boolean);
begin
  FKeyStates[Ord(AButton)]:=AValue;
end;

procedure TProphet600Hardware.SetKeyStates(AKey: Integer; AValue: Boolean);
begin
  FKeyStates[64+AKey]:=AValue;
end;

procedure TProphet600Hardware.SetPotValues(APot: TP600Pot; AValue: Word);
begin
  FPotValues[APot]:=AValue;
end;

procedure TProphet600Hardware.Initialize;
begin
  FFFStatus:=True;
end;

procedure TProphet600Hardware.LoadRomFromFile(AFileName: String);
var fs:TFileStream;
begin
  fs:=TFileStream.Create(AFileName,fmOpenRead or fmShareDenyWrite);
  try
    Assert(fs.Size=Length(FRom));
    fs.Read(FRom,Length(FRom));
  finally
    fs.Free;
  end;
end;

procedure TProphet600Hardware.Write(AIsIO: Boolean; AAddress: Word; AValue: Byte
  );

var reg:Byte;
begin
  if not AIsIO then
  begin
    case AAddress of
      $0000..$1fff:
        Assert(False);
      $2000..$27ff:
        FRam1[AAddress-$2000]:=AValue;
      $3000..$37ff:
        FRam2[AAddress-$3000]:=AValue;
      $4000:
      begin
        FDACValue:=(FDACValue and $fc00) or (Integer(AValue xor $ff) shl 2) or $03;
        UpdateCVs;
      end;
      $4001:
      begin
        FDACValue:=(FDACValue and $03ff) or ((Integer(AValue xor $ff) and $3f) shl 10);
        UpdateCVs;
      end;
    end;
  end
  else
  begin
    if AAddress and $08 <> 0 then
    begin
      // U322
      FIOCSData[AAddress and $07]:=AValue;

      case AAddress and $07 of
        $00,$01:
        begin
          // display
          if FIOCSData[0] and $10 <> 0 then
          begin
            FOldDisplay[0]:=FDisplay[0];
            FDisplay[0]:=FIOCSData[1];
          end;
          if FIOCSData[0] and $20 <> 0 then
          begin
            FOldDisplay[1]:=FDisplay[1];
            FDisplay[1]:=FIOCSData[1];
          end;
          if FIOCSData[0] and $40 <> 0 then
          begin
            FOldDisplay[2]:=FDisplay[2];
            FDisplay[2]:=FIOCSData[1];
          end;
        end;
        $02:
        begin
          // pot mux
          reg:=AValue and $0f;

          if AValue and $10 = 0 then
            FMuxedPot:=TP600Pot(reg);
          if AValue and $20 = 0 then
            FMuxedPot:=TP600Pot(reg+$10);
        end;
        $05:
        begin
          // dac
          FDACDemux:=AValue;
          UpdateCVs;
        end;
        $06:
        begin
//          debugln(['W ',Ord(AIsIO),' ',hexStr(AAddress,4),' ',hexStr(AValue,2),' (',AValue,')']);

          // tune
          F8253.SetGate(2,AValue and $02 <> 0); // Cntr En

          FFFD:=AValue and $08 <> 0; // FF D
          FFFP:=AValue and $01 <> 0; // -FF P
          FFFCL:=AValue and $10 <> 0; // -FF CL

          UpdateFFStatus(False);
        end;
      end;
    end

    else
    begin
      // 8253 timer
      F8253.WriteReg(AAddress and $03,AValue);
    end;
  end;
end;

function TProphet600Hardware.Read(AIsIO: Boolean; AAddress: Word): Byte;
var i,bIdx:Integer;
begin
  Result:=$ff;

  if not AIsIO then
  begin
    case AAddress of
      $0000..$1fff:
        Result:=FRom[AAddress];
      $2000..$27ff:
        Result:=FRam1[AAddress-$2000];
      $3000..$37ff:
        Result:=FRam2[AAddress-$3000];
    end;
  end
  else
  begin
    if AAddress and $08 <> 0 then
    begin
      if AAddress and $03 = $01 then // CSI0 (misc driver)
      begin
        Result:=0;
        if FFFStatus then
          Result:=Result or $02;
        if F8253.GetOutput(2) then
          Result:=Result or $04;
        if ADCCompare then
          Result:=Result or $08;
//        debugln(['R ',Ord(AIsIO),' ',hexStr(AAddress,4),' ',hexStr(Result,2),' (',Result,')']);
      end
      else if AAddress and $03 = $02 then // CSI1 (switch matrix)
      begin
        bIdx:=(FIOCSData[0] and $0f)*8;
        Result:=0;
        for i:=0 to 7 do
          Result:=Result or (ifthen(FKeyStates[i+bIdx],1,0) shl i);
      end;
    end
    else
    begin
      // 8253 timer
      Result:=F8253.ReadReg(AAddress and $03);
    end;
  end;

end;

procedure TProphet600Hardware.RunCycles(ACount: Integer);
var cv,cvAmp,cvHi:TP600CV;
    cvV:Word;
    i,cycles:Integer;
    ratio:Double;
begin
  // timer 2 runs at audio output frequency

    // find highest pitched osc
  cvV:=0;
  cvHi:=pcMVol; // dummy
  for i:=0 to 5 do
  begin
    cvAmp:=TP600CV(Ord(pcAmp1)+i);
    if CVValues[cvAmp]=0 then
      Continue;

    // osc A
    cv:=TP600CV(Ord(pcOsc1A)+i);
    if CVValues[cv]>cvV then
    begin
      cvHi:=cv;
      cvV:=CVValues[cv];
    end;

    // osc B
    cv:=TP600CV(Ord(pcOsc1B)+i);
    if CVValues[cv]>cvV then
    begin
      cvHi:=cv;
      cvV:=CVValues[cv];
    end;

    // filter self oscillation
    if CVValues[pcRes]>60000 then
    begin
      cv:=TP600CV(Ord(pcFil1)+i);
      if CVValues[cv]>cvV then
      begin
        cvHi:=cv;
        cvV:=CVValues[cv];
      end;
    end;
  end;

  if cvHi<>pcMVol then
  begin
      // compute cycles per ACount ticks
    ratio:=ACount / CZ80Frequency;
    FIncompleteCycles:=FIncompleteCycles + CVHertz[cvHi] * ratio;
    cycles:=Trunc(FIncompleteCycles);
    FIncompleteCycles:=FIncompleteCycles-cycles;

    Assert(cycles in [0,1]); // quantum is too big if this fails

    if cycles<>0 then
      UpdateFFStatus(True);
  end;

  // timer 1 runs at 2Mhz and is gated by timer 2 output

  F8253.SetGate(1,not F8253.GetOutput(2));
  F8253.RunCycles(1,ACount div 2);
end;

end.

