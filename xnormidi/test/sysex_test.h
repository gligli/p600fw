#ifndef SYSEX_TEST_H
#define SYSEX_TEST_H

#include <cppunit/extensions/HelperMacros.h>

class SYSEXTest : public CppUnit::TestCase { 
      CPPUNIT_TEST_SUITE( SYSEXTest );
      CPPUNIT_TEST( testSizes );
      CPPUNIT_TEST( testEncode );
      CPPUNIT_TEST( testDecode );
      CPPUNIT_TEST_SUITE_END(); 

   public:

      void testSizes();
      void testEncode();
      void testDecode();
};

#endif
