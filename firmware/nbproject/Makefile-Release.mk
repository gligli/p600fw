#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Environment
MKDIR=mkdir
CP=cp
GREP=grep
NM=nm
CCADMIN=CCadmin
RANLIB=ranlib
CC=avr-gcc
CCC=avr-g++
CXX=avr-g++
FC=gfortran
AS=avr-as

# Macros
CND_PLATFORM=MHV_AVR-Windows
CND_DLIB_EXT=dll
CND_CONF=Release
CND_DISTDIR=dist
CND_BUILDDIR=build

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=${CND_BUILDDIR}/${CND_CONF}/${CND_PLATFORM}

# Object Files
OBJECTFILES= \
	${OBJECTDIR}/_ext/1270477542/adsr.o \
	${OBJECTDIR}/_ext/1270477542/arp.o \
	${OBJECTDIR}/_ext/1270477542/assigner.o \
	${OBJECTDIR}/_ext/1270477542/dac.o \
	${OBJECTDIR}/_ext/1270477542/display.o \
	${OBJECTDIR}/_ext/1270477542/import.o \
	${OBJECTDIR}/_ext/1270477542/lfo.o \
	${OBJECTDIR}/_ext/1270477542/midi.o \
	${OBJECTDIR}/_ext/1270477542/potmux.o \
	${OBJECTDIR}/_ext/1270477542/scanner.o \
	${OBJECTDIR}/_ext/1270477542/seq.o \
	${OBJECTDIR}/_ext/1270477542/sh.o \
	${OBJECTDIR}/_ext/1270477542/storage.o \
	${OBJECTDIR}/_ext/1270477542/synth.o \
	${OBJECTDIR}/_ext/1270477542/tuner.o \
	${OBJECTDIR}/_ext/1270477542/uart_6850.o \
	${OBJECTDIR}/_ext/1270477542/ui.o \
	${OBJECTDIR}/_ext/1270477542/utils.o \
	${OBJECTDIR}/p600firmware.o \
	${OBJECTDIR}/print.o \
	${OBJECTDIR}/usb_debug_only.o


# C Compiler Flags
CFLAGS=

# CC Compiler Flags
CCFLAGS=
CXXFLAGS=

# Fortran Compiler Flags
FFLAGS=

# Assembler Flags
ASFLAGS=

# Link Libraries and Options
LDLIBSOPTIONS=

# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	"${MAKE}"  -f nbproject/Makefile-${CND_CONF}.mk ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/firmware.exe

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/firmware.exe: ${OBJECTFILES}
	${MKDIR} -p ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}
	${LINK.c} -o ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/firmware ${OBJECTFILES} ${LDLIBSOPTIONS}

${OBJECTDIR}/_ext/1270477542/adsr.o: ../common/adsr.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/adsr.o ../common/adsr.c

${OBJECTDIR}/_ext/1270477542/arp.o: ../common/arp.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/arp.o ../common/arp.c

${OBJECTDIR}/_ext/1270477542/assigner.o: ../common/assigner.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/assigner.o ../common/assigner.c

${OBJECTDIR}/_ext/1270477542/dac.o: ../common/dac.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/dac.o ../common/dac.c

${OBJECTDIR}/_ext/1270477542/display.o: ../common/display.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/display.o ../common/display.c

${OBJECTDIR}/_ext/1270477542/import.o: ../common/import.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/import.o ../common/import.c

${OBJECTDIR}/_ext/1270477542/lfo.o: ../common/lfo.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/lfo.o ../common/lfo.c

${OBJECTDIR}/_ext/1270477542/midi.o: ../common/midi.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/midi.o ../common/midi.c

${OBJECTDIR}/_ext/1270477542/potmux.o: ../common/potmux.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/potmux.o ../common/potmux.c

${OBJECTDIR}/_ext/1270477542/scanner.o: ../common/scanner.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/scanner.o ../common/scanner.c

${OBJECTDIR}/_ext/1270477542/seq.o: ../common/seq.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/seq.o ../common/seq.c

${OBJECTDIR}/_ext/1270477542/sh.o: ../common/sh.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/sh.o ../common/sh.c

${OBJECTDIR}/_ext/1270477542/storage.o: ../common/storage.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/storage.o ../common/storage.c

${OBJECTDIR}/_ext/1270477542/synth.o: ../common/synth.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/synth.o ../common/synth.c

${OBJECTDIR}/_ext/1270477542/tuner.o: ../common/tuner.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/tuner.o ../common/tuner.c

${OBJECTDIR}/_ext/1270477542/uart_6850.o: ../common/uart_6850.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/uart_6850.o ../common/uart_6850.c

${OBJECTDIR}/_ext/1270477542/ui.o: ../common/ui.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/ui.o ../common/ui.c

${OBJECTDIR}/_ext/1270477542/utils.o: ../common/utils.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/1270477542/utils.o ../common/utils.c

${OBJECTDIR}/p600firmware.o: p600firmware.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/p600firmware.o p600firmware.c

${OBJECTDIR}/print.o: print.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/print.o print.c

${OBJECTDIR}/usb_debug_only.o: usb_debug_only.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.c) -O2 -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/usb_debug_only.o usb_debug_only.c

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${CND_BUILDDIR}/${CND_CONF}
	${RM} ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/firmware.exe

# Subprojects
.clean-subprojects:

# Enable dependency checking
.dep.inc: .depcheck-impl

include .dep.inc
