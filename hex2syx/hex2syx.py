# based on Hex2SysEx utility by Olivier Gillet (ol.gillet@gmail.com)

import logging
import optparse
import os
import struct
import sys
import intelhex
from itertools import repeat

page_size = 0x100
firmware_max_size = 0x10000
syx_start = '\xf0'
syx_end = '\xf7'
my_id = '\x00\x61\x16'
update_command = '\x6b'

def checkCRC(message):
    #CRC-16-CITT poly, the CRC sheme used by ymodem protocol
    poly = 0x1021
    #16bit operation register, initialized to zeros
    reg = 0x0000
    #pad the end of the message with the size of the poly
    message += '\x00\x00'
    #for each bit in the message
    for byte in message:
        mask = 0x80
        while(mask > 0):
            #left shift by one
            reg<<=1
            #input the next bit from the message into the right hand side of the op reg
            if ord(byte) & mask:
                reg += 1
            mask>>=1
            #if a one popped out the left of the reg, xor reg w/poly
            if reg > 0xffff:
                #eliminate any one that popped out the left
                reg &= 0xffff
                #xor with the poly, this is the remainder
                reg ^= poly
    return reg

def encode14(value):
	return [(value >> 7) & 0x7f, value & 0x7f]

def encode16(value):
	return [(value >> 9) & 0x7f, (value >> 2) & 0x7f, value & 0x03]

def encode32(value):
	res = [0,0,0,0,0]

	for i in range(0,4):
		res[i] = value[i] & 0x7f

	res[4]  = (value[0] & 0x80) >> 7
	res[4] |= (value[1] & 0x80) >> 6
	res[4] |= (value[2] & 0x80) >> 5
	res[4] |= (value[3] & 0x80) >> 4

	return res

if __name__ == "__main__":
	print 'hex2syx v1 (p600fw MIDI sysex firmware update builder)'
	
	parser = optparse.OptionParser()

	parser.add_option(
		'-o',
		'--output_file',
		dest='output_file',
		default=None,
		help='Write output file to FILE',
		metavar='FILE')

	options, args = parser.parse_args()
	if len(args) != 1:
		logging.fatal('Specify one, and only one firmware .hex file!')
		sys.exit(1)

	ih = intelhex.IntelHex(file(args[0]))
	data = ih[slice(0,firmware_max_size)].tobinarray()

	if not data:
		logging.fatal('Error while loading .hex file')
		sys.exit(2)

	output_file = options.output_file
	if not output_file:
		if '.hex' in args[0]:
			output_file = args[0].replace('.hex', '.syx')
		else:
			output_file = args[0] + '.syx'

	print 'Firmware size: %d bytes' % (len(data))

	# pad to a full page
	end = len(data)
	if end % page_size !=0:
		data.extend(repeat(0, (page_size - (end % page_size))))

	print 'Padded size: %d bytes' % (len(data))

	syx_data = ''

	# data blocks
	for i in xrange(len(data) - page_size, -1, -page_size):
		block = encode14(page_size)
		block += encode14(i / page_size)

		for j in xrange(i, i + page_size, 4):
			block += encode32(data[j : j + 4])

		block += encode16(checkCRC(''.join(chr(e) for e in block)))

		syx_data += syx_start + my_id + update_command
		syx_data += ''.join(chr(e) for e in block)
		syx_data += syx_end

	# indicates end of transmission
	syx_data += syx_start + my_id + update_command
	syx_data += '\x00\x00'
	syx_data += syx_end

	print 'Sysex size: %d bytes' % (len(syx_data))

	f = file(output_file, 'wb')
	f.write(''.join(syx_data))
	f.close()

	print 'Done.'