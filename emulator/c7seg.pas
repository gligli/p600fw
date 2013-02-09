unit c7Seg;

{$mode delphi}

interface

uses
  Classes, SysUtils, FileUtil, Forms, Controls, ExtCtrls;

type

  { TSevenSegFrame }

  TSevenSegFrame = class(TFrame)
    sDot: TShape;
    sF: TShape;
    sE: TShape;
    sB: TShape;
    sC: TShape;
    sD: TShape;
    sG: TShape;
    sA: TShape;
    procedure FrameClick(Sender: TObject);
  private
    FValue: Byte;
    procedure SetValue(AValue: Byte);
    { private declarations }
  public
    { public declarations }
    property Value:Byte read FValue write SetValue;
  end;

implementation

{$R *.lfm}

{ TSevenSegFrame }

procedure TSevenSegFrame.FrameClick(Sender: TObject);
begin

end;

procedure TSevenSegFrame.SetValue(AValue: Byte);
begin
  FValue:=AValue;
  sA.Visible:=AValue and $01 <> 0;
  sB.Visible:=AValue and $02 <> 0;
  sC.Visible:=AValue and $04 <> 0;
  sD.Visible:=AValue and $08 <> 0;
  sE.Visible:=AValue and $10 <> 0;
  sF.Visible:=AValue and $20 <> 0;
  sG.Visible:=AValue and $40 <> 0;
  sDot.Visible:=AValue and $80 <> 0;
end;

end.

