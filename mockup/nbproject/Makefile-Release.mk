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
CC=gcc.exe
CCC=g++.exe
CXX=g++.exe
FC=gfortran
AS=as.exe

# Macros
CND_PLATFORM=MinGW-Windows
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
	${OBJECTDIR}/_ext/1270477542/scanner.o \
	${OBJECTDIR}/_ext/1270477542/potmux.o \
	${OBJECTDIR}/_ext/1270477542/synth.o \
	${OBJECTDIR}/p600mockup.o \
	${OBJECTDIR}/_ext/1270477542/display.o \
	${OBJECTDIR}/_ext/1270477542/p600.o \
	${OBJECTDIR}/_ext/1270477542/dac.o


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
	"${MAKE}"  -f nbproject/Makefile-${CND_CONF}.mk ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libmockup.${CND_DLIB_EXT}

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libmockup.${CND_DLIB_EXT}: ${OBJECTFILES}
	${MKDIR} -p ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}
	${LINK.c} -shared -o ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libmockup.${CND_DLIB_EXT} ${OBJECTFILES} ${LDLIBSOPTIONS} 

${OBJECTDIR}/_ext/1270477542/adsr.o: ../common/adsr.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/adsr.o ../common/adsr.c

${OBJECTDIR}/_ext/1270477542/scanner.o: ../common/scanner.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/scanner.o ../common/scanner.c

${OBJECTDIR}/_ext/1270477542/potmux.o: ../common/potmux.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/potmux.o ../common/potmux.c

${OBJECTDIR}/_ext/1270477542/synth.o: ../common/synth.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/synth.o ../common/synth.c

${OBJECTDIR}/p600mockup.o: p600mockup.c 
	${MKDIR} -p ${OBJECTDIR}
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/p600mockup.o p600mockup.c

${OBJECTDIR}/_ext/1270477542/display.o: ../common/display.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/display.o ../common/display.c

${OBJECTDIR}/_ext/1270477542/p600.o: ../common/p600.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/p600.o ../common/p600.c

${OBJECTDIR}/_ext/1270477542/dac.o: ../common/dac.c 
	${MKDIR} -p ${OBJECTDIR}/_ext/1270477542
	${RM} $@.d
	$(COMPILE.c) -O2  -MMD -MP -MF $@.d -o ${OBJECTDIR}/_ext/1270477542/dac.o ../common/dac.c

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${CND_BUILDDIR}/${CND_CONF}
	${RM} ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libmockup.${CND_DLIB_EXT}

# Subprojects
.clean-subprojects:

# Enable dependency checking
.dep.inc: .depcheck-impl

include .dep.inc
