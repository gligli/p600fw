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
CC=avr-gcc.exe
CCC=avr-g++.exe
CXX=avr-g++.exe
FC=gfortran
AS=avr-as.exe

# Macros
CND_PLATFORM=WinAVR-Windows
CND_DLIB_EXT=dll
CND_CONF=Debug
CND_DISTDIR=dist
CND_BUILDDIR=build

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=${CND_BUILDDIR}/${CND_CONF}/${CND_PLATFORM}

# Object Files
OBJECTFILES= \
	${OBJECTDIR}/p600firmware.o \
	${OBJECTDIR}/print.o \
	${OBJECTDIR}/_ext/1270477542/scanner.o \
	${OBJECTDIR}/usb_rawhid_debug.o \
	${OBJECTDIR}/_ext/1270477542/display.o \
	${OBJECTDIR}/_ext/1270477542/p600.o


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

${OBJECTDIR}/p600firmware.o: p600firmware.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} $@.d
	$(COMPILE.c) -g -I../common -MMD -MP -MF $@.d -o ${OBJECTDIR}/p600firmware.o p600firmware.c

${OBJECTDIR}/print.o: print.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} $@.d
	$(COMPILE.c) -g -I../common -MMD -MP -MF $@.d -o ${OBJECTDIR}/print.o print.c

${OBJECTDIR}/_ext/1270477542/scanner.o: ../common/scanner.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -g -I../common -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/scanner.o ../common/scanner.c

${OBJECTDIR}/usb_rawhid_debug.o: usb_rawhid_debug.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} $@.d
	$(COMPILE.c) -g -I../common -MMD -MP -MF $@.d -o ${OBJECTDIR}/usb_rawhid_debug.o usb_rawhid_debug.c

${OBJECTDIR}/_ext/1270477542/display.o: ../common/display.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -g -I../common -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/display.o ../common/display.c

${OBJECTDIR}/_ext/1270477542/p600.o: ../common/p600.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -g -I../common -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/p600.o ../common/p600.c

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
