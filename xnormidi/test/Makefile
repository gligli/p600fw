CFLAGS += -I. -I../ -g -Wall -DDEBUG -lcppunit

DUMMY_SRC = dummy_device.c ../midi.c ../midi_device.c ../bytequeue/bytequeue.c ../bytequeue/interrupt_setting.c
DUMMY_OBJ = $(DUMMY_SRC:.c=.o)

MIDI_SRC = ../midi.c ../midi_device.c ../bytequeue/bytequeue.c ../bytequeue/interrupt_setting.c
MIDI_OBJ = $(MIDI_SRC:.c=.o)

SYSEX_SRC = ../sysex_tools.c
SYSEX_OBJ = $(SYSEX_SRC:.c=.o)

TEST_SRC = test_runner.cpp midi_test.cpp sysex_test.cpp
TEST_OBJ = $(TEST_SRC:.cpp=.o) $(SYSEX_OBJ) $(MIDI_OBJ)

.c.o:
	@echo CC $<
	@$(CC) -c $(CFLAGS) -o $*.o $<

.cpp.o:
	@echo CXX $<
	@$(CXX) -c $(CFLAGS) -o $*.o $<

test: $(TEST_OBJ)
	@$(CXX) -o test $(CFLAGS) $(LDFLAGS) $(TEST_OBJ)

run_tests: test
	./test

all: run_tests

#-------------------
clean:
	rm -f *.o *.map *.out *.hex *.tar.gz ../*.o ../bytequeue/*.o test
#-------------------

