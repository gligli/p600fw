#include "sysex_test.h"
#include <inttypes.h>
#include <stdlib.h>
#include <streambuf>
#include <string.h>

#include "sysex_tools.h"

#define SIZE 1028

CPPUNIT_TEST_SUITE_REGISTRATION( SYSEXTest );

void SYSEXTest::testSizes() {
   for(uint16_t i = 1; i < SIZE; i++) {
      uint16_t enc_size = sysex_encoded_length(i);
      CPPUNIT_ASSERT(enc_size > i);
      int rem = i % 7;
      if (rem)
         CPPUNIT_ASSERT_EQUAL(rem + 1 + (i / 7) * 8, (int)enc_size);
      else
         CPPUNIT_ASSERT_EQUAL((i / 7) * 8, (int)enc_size);

      uint16_t dec_size = sysex_decoded_length(enc_size);
      CPPUNIT_ASSERT_EQUAL(i, dec_size);
   }
}

void SYSEXTest::testEncode() {
   uint8_t orig[SIZE];
   uint8_t * encoded = new uint8_t[sysex_encoded_length(SIZE)];

   //create randomized data to start
   for(unsigned int i = 0; i < SIZE; i++)
      orig[i] = rand() & 0xFF;

   //test byte encodeing
   for (unsigned i = 1; i < SIZE; i++) {
      uint16_t enc_size = sysex_encode(encoded, orig, i);

      //the encoded size should always be greater than the original size
      CPPUNIT_ASSERT(enc_size > i);
      CPPUNIT_ASSERT_EQUAL(enc_size, sysex_encoded_length(i));

      //assert that all the top bits are unset
      for (unsigned int j = 0; j < enc_size; j++)
         CPPUNIT_ASSERT_EQUAL(0, (encoded[j] & 0x80));

      //every 8th byte should be the MSbits of all of the next bytes
      for (unsigned int j = 0; j < enc_size; j += 8) {
         for(unsigned int k = 0; (k < 7) && ((k + j + 1) < enc_size); k++) {
            unsigned int orig_i = (j / 8) * 7 + k;
            //test the MSbit
            CPPUNIT_ASSERT_EQUAL(
                  orig[orig_i] & 0x80,
                  (encoded[j] << (k + 1)) & 0x80);
            //test the rest of the data
            CPPUNIT_ASSERT_EQUAL(
                  (unsigned int)orig[orig_i] & 0x7F,
                  (unsigned int)encoded[j + k + 1]);
         }
      }
   }

   sysex_encode(encoded, orig, SIZE);

   //encode in chunks
   uint8_t * encoded2 = new uint8_t[sysex_encoded_length(SIZE)];
   for (unsigned int i = 0, j = 0; i < (SIZE - 7); i += 7, j +=8) {
      uint16_t enc_size = sysex_encode(encoded2 + j, orig + i, 7);
      CPPUNIT_ASSERT_EQUAL((uint16_t)8, enc_size);
      for (unsigned int k = 0; k < (j + 8); k++)
         CPPUNIT_ASSERT_EQUAL((unsigned int)encoded[k], (unsigned int)encoded2[k]);
   }

   delete [] encoded;
   delete [] encoded2;
}

void SYSEXTest::testDecode() {
   uint8_t orig[SIZE];
   uint8_t decoded[SIZE];
   uint8_t * encoded = new uint8_t[sysex_encoded_length(SIZE)];

   //create randomized data to start
   for(unsigned int i = 0; i < SIZE; i++)
      orig[i] = rand() & 0xFF;

   //we'll trust that encoding works correctly and test decoding encoded data
   for(uint16_t i = 1; i < SIZE; i++) {
      //encode some data
      uint16_t enc_size = sysex_encode(encoded, orig, i);

      //just in case, zero out decoded..
      memset(decoded, SIZE, 0);

      //decode it
      uint16_t dec_size = sysex_decode(decoded, encoded, enc_size);
      CPPUNIT_ASSERT_EQUAL(i, dec_size);

      for(uint16_t j = 0; j < i; j++)
         CPPUNIT_ASSERT_EQUAL(orig[j], decoded[j]);
   }

   //decode in chunks
   sysex_encode(encoded, orig, SIZE);

   for (uint16_t i = 0, j = 0; i < (SIZE - 7); i += 7, j +=8) {
      //just in case, zero out decoded..
      memset(decoded, SIZE, 0);

      uint16_t dec_size = sysex_decode(decoded + i, encoded + j, 8);
      CPPUNIT_ASSERT_EQUAL((uint16_t)7, dec_size);
      for(uint16_t k = 0; k < (i + 7); k++)
         CPPUNIT_ASSERT_EQUAL((unsigned int)orig[k], (unsigned int)decoded[k]);
   }

   delete [] encoded;
}

