#ifndef MIDI_TEST_H
#define MIDI_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class MIDITest : public CppUnit::TestCase { 

   CPPUNIT_TEST_SUITE( MIDITest );
   CPPUNIT_TEST( statusByteTest );
   CPPUNIT_TEST( realtimeTest );
   CPPUNIT_TEST( packetLengthTest );
   CPPUNIT_TEST( threeByteCallbacks );
   CPPUNIT_TEST( twoByteCallbacks );
   CPPUNIT_TEST( oneByteCallbacks );
   CPPUNIT_TEST( sysexCallback );
   CPPUNIT_TEST( interspersedRealtime );
   CPPUNIT_TEST_SUITE_END(); 

   public:
      virtual void setUp();

      void statusByteTest();
      void realtimeTest();
      void packetLengthTest();
      void threeByteCallbacks();
      void twoByteCallbacks();
      void oneByteCallbacks();
      void sysexCallback();
      void interspersedRealtime();
};

#endif
