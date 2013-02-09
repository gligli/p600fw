unit FMain;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, FileUtil, Forms, Controls, Graphics, Dialogs, StdCtrls,
  ExtCtrls, ComCtrls, ValEdit, CheckLst, p600emuclasses, ueled, uEKnob,
  LazLogger, windows, strutils, typinfo, Midi, c7Seg, syncobjs;

const
    WM_MIDI = WM_USER + 42;

type

  { TMainForm }

  TMainForm = class(TForm)
    bt0: TButton;
    bt1: TButton;
    bt2: TButton;
    bt3: TButton;
    bt4: TButton;
    bt5: TButton;
    bt6: TButton;
    bt7: TButton;
    bt8: TButton;
    bt9: TButton;
    btArpAss: TButton;
    btArpUD: TButton;
    btFromTape: TButton;
    btPreset: TButton;
    btRecord: TButton;
    btSeq1: TButton;
    btSeq2: TButton;
    btTick: TButton;
    btInit: TButton;
    btToTape: TButton;
    btTune: TButton;
    kSpeed: TuEKnob;
    llCurCtrl: TLabel;
    lArpAss: TuELED;
    lArpUD: TuELED;
    lbxInputDevices: TCheckListBox;
    lFromTape: TuELED;
    lPreset: TuELED;
    lRecord: TuELED;
    lSeq1: TuELED;
    lSeq2: TuELED;
    lToTape: TuELED;
    lTune: TuELED;
    lvCV: TListView;
    lvGates: TListView;
    Panel1: TPanel;
    pnP600: TPanel;
    ssDigit1: TSevenSegFrame;
    ssDigit0: TSevenSegFrame;
    tbXModFreqA: TToggleBox;
    tbASaw: TToggleBox;
    tbASqr: TToggleBox;
    tbATri: TToggleBox;
    tbBSaw: TToggleBox;
    tbBSqr: TToggleBox;
    tbBTri: TToggleBox;
    tbXModFilter: TToggleBox;
    tbLFOTri: TToggleBox;
    tbLFOFreqAB: TToggleBox;
    tbLFOPWAB: TToggleBox;
    tbLFOFilter: TToggleBox;
    tbUnison: TToggleBox;
    tbSync: TToggleBox;
    tbTracking: TTrackBar;
    kOscBPW: TuEKnob;
    kMix: TuEKnob;
    kGlide: TuEKnob;
    kCutoff: TuEKnob;
    kReso: TuEKnob;
    kEnvAmt: TuEKnob;
    kFilA: TuEKnob;
    kFilD: TuEKnob;
    kFilS: TuEKnob;
    kFilR: TuEKnob;
    kXModFilEnv: TuEKnob;
    kAmpA: TuEKnob;
    kAmpD: TuEKnob;
    kAmpS: TuEKnob;
    kAmpR: TuEKnob;
    kMasterTune: TuEKnob;
    kVolume: TuEKnob;
    kXModOscB: TuEKnob;
    kLFOFreq: TuEKnob;
    kLFOAmt: TuEKnob;
    kOscAFreq: TuEKnob;
    kOscBFine: TuEKnob;
    kOscBFreq: TuEKnob;
    kOscAPW: TuEKnob;
    tiTick: TTimer;
    tbRun: TToggleBox;
    procedure btInitClick(Sender: TObject);
    procedure btTickClick(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure kSpeedMouseEnter(Sender: TObject);
    procedure kSpeedMouseLeave(Sender: TObject);
    procedure lbxInputDevicesClickCheck(Sender: TObject);
    procedure lvCVData(Sender: TObject; Item: TListItem);
    procedure lvGatesData(Sender: TObject; Item: TListItem);
    procedure P600ButtonClick(Sender: TObject);
    procedure P600ButtonMouseDown(Sender: TObject; Button: TMouseButton;
      Shift: TShiftState; X, Y: Integer);
    procedure tbRunChange(Sender: TObject);
    procedure tbTrackingChange(Sender: TObject);
    procedure tiTickTimer(Sender: TObject);
  private
    { private declarations }
    procedure UpdateState;
    procedure DoMidiInData( const aDeviceIndex: integer; const aStatus, aData1, aData2: byte );
    procedure OnMidi(var AMsg:TMessage);message WM_MIDI;
  public
    { public declarations }
  end;

var
  MainForm: TMainForm;

implementation

const CTimesRealSpeed=1;

{$R *.lfm}

{ TMainForm }

procedure TMainForm.FormCreate(Sender: TObject);
begin
  Constraints.MinHeight:=Height;
  Constraints.MinWidth:=Width;

  lbxInputDevices.Items.Assign( MidiInput.Devices );
  MidiInput.OnMidiData := @DoMidiInData;

  btInit.Click;

  lvCV.Items.Count:=Ord(High(TP600CV))+1;
  lvGates.Items.Count:=Ord(High(TP600Gate))+1;
end;

procedure TMainForm.FormDestroy(Sender: TObject);
begin
  MidiInput.CloseAll;
end;

procedure TMainForm.kSpeedMouseEnter(Sender: TObject);
var k:TuEKnob;
begin
  k:=Sender as TuEKnob;

  llCurCtrl.Caption:=Copy(k.Name,2,MaxInt)+' (Value = '+IntToStr(round(k.Position))+')';
end;

procedure TMainForm.kSpeedMouseLeave(Sender: TObject);
begin
  llCurCtrl.Caption:='';
end;

procedure TMainForm.lbxInputDevicesClickCheck(Sender: TObject);
begin
  if lbxInputDevices.Checked[ lbxInputDevices.ItemIndex ] then
    MidiInput.Open( lbxInputDevices.ItemIndex )
  else
    MidiInput.Close( lbxInputDevices.ItemIndex )
end;

procedure TMainForm.lvCVData(Sender: TObject; Item: TListItem);
var cv:TP600CV;
begin
  cv:=TP600CV(Item.Index);

  with P600Emu.HW do
  begin
    Item.Caption:=Copy(GetEnumName(TypeInfo(TP600CV),Ord(cv)),3,MaxInt);
    Item.SubItems.Add(IntToStr(CVValues[cv]));
    Item.SubItems.Add(FormatFloat('0.000',CVVolts[cv]));
    Item.SubItems.Add(FormatFloat('0.0',CVHertz[cv]));
  end;
end;

procedure TMainForm.lvGatesData(Sender: TObject; Item: TListItem);
var gt:TP600Gate;
begin
  gt:=TP600Gate(Item.Index);

  with P600Emu.HW do
  begin
    Item.Caption:=Copy(GetEnumName(TypeInfo(TP600Gate),Ord(gt)),3,MaxInt);
    Item.SubItems.Add(IntToStr(Ord(GateValues[gt])));
  end;
end;

procedure TMainForm.P600ButtonClick(Sender: TObject);
begin
  if Sender is TToggleBox then
    P600Emu.HW.ButtonStates[TP600Button((Sender as TToggleBox).Tag)]:=(Sender as TToggleBox).Checked
  else if Sender is TButton then
    P600Emu.HW.ButtonStates[TP600Button((Sender as TButton).Tag)]:=False;
end;

procedure TMainForm.P600ButtonMouseDown(Sender: TObject; Button: TMouseButton;
  Shift: TShiftState; X, Y: Integer);
begin
  Assert(Sender is TButton);
  P600Emu.HW.ButtonStates[TP600Button((Sender as TButton).Tag)]:=True;
end;

procedure TMainForm.tbRunChange(Sender: TObject);
begin
  tiTick.Enabled:=tbRun.Checked;
end;

procedure TMainForm.tbTrackingChange(Sender: TObject);
begin
  P600Emu.HW.ButtonStates[pbFilFull]:=tbTracking.Position=0;
  P600Emu.HW.ButtonStates[pbFilHalf]:=tbTracking.Position=1;
end;

procedure TMainForm.tiTickTimer(Sender: TObject);
var i:Integer;
begin
  for i:=0 to tiTick.Interval div CTickMilliseconds * CTimesRealSpeed - 1 do
    P600Emu.Tick;

  UpdateState;
end;

procedure TMainForm.btTickClick(Sender: TObject);
begin
  P600Emu.Tick;
  UpdateState;
end;

procedure TMainForm.btInitClick(Sender: TObject);
begin
  DebugLn(Caption);
  P600Emu.Initialize;
end;

procedure TMainForm.UpdateState;
var cv:TP600CV;
    gt:TP600Gate;
begin
  with P600Emu.HW do
  begin
    lSeq1.Active:=plSeq1 in LEDStates;
    lSeq2.Active:=plSeq2 in LEDStates;
    lArpUD.Active:=plArpUD in LEDStates;
    lArpAss.Active:=plArpAssign in LEDStates;
    lPreset.Active:=plPreset in LEDStates;
    lRecord.Active:=plRecord in LEDStates;
    lFromTape.Active:=plFromTape in LEDStates;
    lToTape.Active:=plToTape in LEDStates;
    lTune.Active:=plTune in LEDStates;

    ssDigit0.Value:=SevenSegment[0];
    ssDigit1.Value:=SevenSegment[1];
    if plDot in LEDStates then
     ssDigit0.Value:=ssDigit0.Value or $80;

    PotValues[ppMixer]:=round(kMix.Position);
    PotValues[ppCutoff]:=round(kCutoff.Position);
    PotValues[ppResonance]:=round(kReso.Position);
    PotValues[ppFilEnvAmt]:=round(kEnvAmt.Position);
    PotValues[ppFilRel]:=round(kFilR.Position);
    PotValues[ppFilSus]:=round(kFilS.Position);
    PotValues[ppFilDec]:=round(kFilD.Position);
    PotValues[ppFilAtt]:=round(kFilA.Position);
    PotValues[ppAmpRel]:=round(kAmpR.Position);
    PotValues[ppAmpSus]:=round(kAmpS.Position);
    PotValues[ppAmpDec]:=round(kAmpD.Position);
    PotValues[ppAmpAtt]:=round(kAmpA.Position);
    PotValues[ppGlide]:=round(kGlide.Position);
    PotValues[ppBPW]:=round(kOscBPW.Position);
    PotValues[ppMVol]:=round(kVolume.Position);
    PotValues[ppMTune]:=round(kMasterTune.Position);
    PotValues[ppSpeed]:=round(kSpeed.Position);
    PotValues[ppAPW]:=round(kOscAPW.Position);
    PotValues[ppPModFilEnv]:=round(kXModFilEnv.Position);
    PotValues[ppLFOFreq]:=round(kLFOFreq.Position);
    PotValues[ppPModOscB]:=round(kXModOscB.Position);
    PotValues[ppLFOAmt]:=round(kLFOAmt.Position);
    PotValues[ppFreqB]:=round(kOscBFreq.Position);
    PotValues[ppFreqA]:=round(kOscAFreq.Position);
    PotValues[ppFreqBFine]:=round(kOscBFine.Position);

    PotValues[ppPitchWheel]:=32768;
    PotValues[ppModWheel]:=0;

    lvCV.Invalidate;
    lvGates.Invalidate;
  end;
end;

procedure TMainForm.DoMidiInData(const aDeviceIndex: integer; const aStatus,
  aData1, aData2: byte);
begin
  // MIDI classes don't seem to be thread safe, posting a message shoud be ok tho
  PostMessage(Handle,WM_MIDI,aDeviceIndex,aData1 or (aData2 shl 8) or (aStatus shl 16));
end;

procedure TMainForm.OnMidi(var AMsg: TMessage);
var devIndex:Integer;
    status,data1,data2:Byte;
begin
  devIndex:=AMsg.wParam;
  status:=AMsg.lParam shr 16;
  data2:=AMsg.lParam shr 8;
  data1:=AMsg.lParam;

  // skip active sensing signals from keyboard
  if status = $FE then Exit;

  // print the message log
  DebugLn(Format( '%s: <Status> %.2x, <Data 1> %.2x <Data 2> %.2x',
    [ MidiInput.Devices[devIndex], status, data1, data2 ] ));

  if data1<61 then
  begin
    if status=$90 then
      P600Emu.HW.KeyStates[data1]:=True
    else if status=$80 then
      P600Emu.HW.KeyStates[data1]:=False;
  end;

  AMsg.Result:=0;
end;

end.

