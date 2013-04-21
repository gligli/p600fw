MCU=atmega8
CC=avr-gcc
OBJCOPY=avr-objcopy
# optimize for size:
CFLAGS=-g -mmcu=$(MCU) -Wall -Wstrict-prototypes -Os -mcall-prologues
PORT=/dev/ttyUSB0
PART=ATmega8
UISP = uisp -dprog=stk500 -dserial=/dev/ttyUSB0 -dpart=$(PART)
GITHUBNAME = 'xnormidi'

VERSION = 0.2.develop
DISTDIR = avr-midi.${VERSION}

#-------------------
current: implementations
#-------------------

implementations:
	cd implementations && make

.c.o:
	@echo CC $<
	@$(CC) -c $(CFLAGS) -Os -o $*.o $<

dist: clean
	mkdir -p ${DISTDIR}
	cp -R COPYING Makefile *.c *.h README implementations/ test/ bytequeue/ ${DISTDIR}
	tar -czf ${DISTDIR}.tar.gz ${DISTDIR}
	rm -rf ${DISTDIR}

post: dist
	scp ${DISTDIR}.tar.gz x37v.info:x37v.info/projects/microcontroller/avr-midi/files/
	scp basic.c x37v.info:x37v.info/projects/microcontroller/avr-midi/

clean:
	rm -f *.o *.map *.out *.hex *.tar.gz */*.o
	cd implementations/ && make clean
	cd test/ && make clean
	rm -rf tmp

doc:
	@cd doc && doxygen Doxyfile

doc-post: doc
	mkdir tmp/ && cd tmp/ && \
		git clone git@github.com:x37v/${GITHUBNAME}.git && \
		cd ${GITHUBNAME} && git co gh-pages
	cp -r doc/html/* tmp/${GITHUBNAME}
	cd tmp/${GITHUBNAME} && git add . && \
		git ci -a -m 'auto updated docs' && \
		git push origin gh-pages
	rm -rf tmp

.PHONY: implementations doc post-doc

all: implementations
