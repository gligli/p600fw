MCU=atmega8
CC=avr-gcc
OBJCOPY=avr-objcopy
# optimize for size:
CFLAGS=-g -mmcu=$(MCU) -Wall -Wstrict-prototypes -Os -mcall-prologues -I../
PORT=/dev/ttyUSB0
PART=ATmega8
UISP = uisp -dprog=stk500 -dserial=/dev/ttyUSB0 -dpart=$(PART)

#-------------------
current: basic.hex
#-------------------

BASICSRC = basic.c ../midi.c ../midi_device.c ../bytequeue/bytequeue.c ../bytequeue/interrupt_setting.c serial_midi.c
SPITSRC  = spit.c ../midi.c serial_midi.c 

BASICOBJ = ${BASICSRC:.c=.o}
SPITOBJ  = ${SPITSRC:.c=.o}

.c.o:
	@echo CC $<
	@$(CC) -c $(CFLAGS) -Os -o $*.o $<


basic.bin : basic.out
	$(OBJCOPY) -R .eeprom -O binary basic.out basic.bin 

basic.hex : basic.out 
	$(OBJCOPY) -R .eeprom -O ihex basic.out basic.hex 

basic.out : $(BASICOBJ)
	$(CC) $(CFLAGS) -o basic.out -Wl,-Map,basic.map $(BASICOBJ)

# load (program) the software
load_basic: basic.hex
	$(UISP) --erase
	$(UISP) --upload --verify if=basic.hex -v=3 --hash=32


spit.bin : spit.out
	$(OBJCOPY) -R .eeprom -O binary spit.out spit.bin 

spit.hex : spit.out 
	$(OBJCOPY) -R .eeprom -O ihex spit.out spit.hex 

spit.out : $(SPITOBJ)
	$(CC) $(CFLAGS) -o spit.out -Wl,-Map,spit.map $(SPITOBJ)

load_spit: spit.hex
	$(UISP) --erase
	$(UISP) --upload --verify if=spit.hex -v=3 --hash=32


fuse_mega16:
	$(UISP) --wr_fuse_l=0x9f --wr_fuse_h=0xd0

fuse_mega8:
	$(UISP) --wr_fuse_l=0xef --wr_fuse_h=0xc9

check_fuse:
	$(UISP) --rd_fuses

#-------------------
clean:
	rm -f *.o *.map *.out *.hex *.tar.gz
#-------------------

all: spit.hex basic.hex
