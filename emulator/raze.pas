unit raze;
// Copy raze.pas and raze.dll into your project's directory. Add raze.pas to
// your project. After adding raze to the Uses clause of another unit, you
// should be able to call the functions below. Have fun :)

interface

    const   Z80_MAP_DIRECT  = 0;    // Reads/writes are done directly
            Z80_MAP_HANDLED = 1;    // Reads/writes use a function handler

    type pbyte = ^byte;

    // Z80 registers
    type z80_register = (
        Z80_REG_AF,
        Z80_REG_BC,
        Z80_REG_DE,
        Z80_REG_HL,
        Z80_REG_IX,
        Z80_REG_IY,
        Z80_REG_PC,
        Z80_REG_SP,
        Z80_REG_AF2,
        Z80_REG_BC2,
        Z80_REG_DE2,
        Z80_REG_HL2,
        Z80_REG_IFF1,        // boolean - 1 or 0
        Z80_REG_IFF2,        // boolean - 1 or 0
        Z80_REG_IR,
        Z80_REG_IM,          // 0, 1, or 2
        Z80_REG_IRQVector,   // 0x00 to 0xff
        Z80_REG_IRQLine      // boolean - 1 or 0
    );

    type
        tSetInProcedure = procedure(port: word);
        tSetOutProcedure = procedure(port: word; value: byte);
        tSetRetiProcedure = procedure;
        tsetFetchCallbackProcedure = procedure(pc: word);


    // Z80 main functions
    procedure z80_reset; cdecl; external 'raze.dll' name '_z80_reset';
    function z80_emulate(cycles: integer): integer; cdecl; external 'raze.dll' name '_z80_emulate';
    procedure z80_raise_IRQ(vector: byte); cdecl; external 'raze.dll' name '_z80_raise_IRQ';
    procedure z80_lower_IRQ; cdecl; external 'raze.dll' name '_z80_lower_IRQ';
    procedure z80_cause_NMI; cdecl; external 'raze.dll' name '_z80_cause_NMI';

    // Z80 context functions
    function z80_get_context_size: integer; cdecl; external 'raze.dll' name '_z80_get_context_size';
    procedure z80_set_context(context: pointer); cdecl; external 'raze.dll' name '_z80_set_context';
    procedure z80_get_context(context: pointer); cdecl; external 'raze.dll' name '_z80_get_context';
    function z80_get_reg(reg: z80_register): word; cdecl; external 'raze.dll' name '_z80_get_reg';
    procedure z80_set_reg(reg: z80_register; value: word); cdecl; external 'raze.dll' name '_z80_set_reg';

    // Z80 cycle functions
    function z80_get_cycles_elapsed: integer; cdecl; external 'raze.dll' name '_z80_get_cycles_elapsed';
    procedure z80_stop_emulating; cdecl; external 'raze.dll' name '_z80_stop_emulating';
    procedure z80_skip_idle; cdecl; external 'raze.dll' name '_z80_skip_idle';
    procedure z80_do_wait_states(n: integer); cdecl; external 'raze.dll' name '_z80_do_wait_states';

    // Z80 I/O functions
    procedure z80_init_memmap; cdecl; external 'raze.dll' name '_z80_init_memmap';
    procedure z80_map_fetch(start, finish: word; memory: pbyte); cdecl; external 'raze.dll' name '_z80_map_fetch';
    procedure z80_map_read(start, finish: word; memory: pbyte); cdecl; external 'raze.dll' name '_z80_map_read';
    procedure z80_map_write(start, finish: word; memory: pbyte); cdecl; external 'raze.dll' name '_z80_map_write';
    procedure z80_add_read(start, finish: word; method: integer; data: pointer); cdecl; external 'raze.dll' name '_z80_add_read';
    procedure z80_add_write(start, finish: word; method: integer; data: pointer); cdecl; external 'raze.dll' name '_z80_add_write';
    procedure z80_set_in(handler: tSetInProcedure); cdecl; external 'raze.dll' name '_z80_set_in';
    procedure z80_set_out(handler: tSetOutProcedure); cdecl; external 'raze.dll' name '_z80_set_out';
    procedure z80_set_reti(handler: tSetRetiProcedure); cdecl; external 'raze.dll' name '_z80_set_reti';
    procedure z80_set_fetch_callback(handler: tsetFetchCallbackProcedure); cdecl; external 'raze.dll' name '_z80_set_fetch_callback';
    procedure z80_end_memmap; cdecl; external 'raze.dll' name '_z80_end_memmap';


implementation


end.
