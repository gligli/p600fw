#!/usr/bin/env python3

# based on Hex2SysEx utility by Olivier Gillet (ol.gillet@gmail.com)

import logging
import argparse
import os
import struct
import sys
from itertools import repeat

page_size = 0x100
firmware_max_size = 0x10000
syx_start = b'\xf0'
syx_end = b'\xf7'
my_id = b'\x00\x61\x16'
update_command = b'\x6b'

def checkCRC(message):
    #CRC-16-CITT poly, the CRC scheme used by ymodem protocol
    poly = 0x1021
    #16bit operation register, initialized to zeros
    reg = 0x0000
    #pad the end of the message with the size of the poly
    message += b'\x00\x00'
    #for each bit in the message
    for byte in message:
        mask = 0x80
        while(mask > 0):
            #left shift by one
            reg<<=1
            #input the next bit from the message into the right hand side of the op reg
            if byte & mask:
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
    return bytes([(value >> 7) & 0x7f, value & 0x7f])

def encode16(value):
    return bytes([(value >> 9) & 0x7f, (value >> 2) & 0x7f, value & 0x03])

def encode32(value):
    res = [0,0,0,0,0]

    for i in range(0,4):
        res[i] = value[i] & 0x7f

    res[4]  = (value[0] & 0x80) >> 7
    res[4] |= (value[1] & 0x80) >> 6
    res[4] |= (value[2] & 0x80) >> 5
    res[4] |= (value[3] & 0x80) >> 4

    return bytes(res)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='fw2syx v1 (p600fw MIDI sysex firmware update builder)')

    parser.add_argument('-o', '--output_file', help='Write output file to FILE', metavar='FILE')
    parser.add_argument('input_file', help='Input firmware file name')
    
    args = parser.parse_args()

    data = []

    with open(args.input_file,"rb") as f:
        c = f.read(1)
        while c:
            data += [ord(c)]
            c = f.read(1)

    if not data:
        logging.fatal('Error while loading input file', args.input_file)
        sys.exit(2)

    output_file = args.output_file
    if not output_file:
        if '.bin' in args.input_file:
            output_file = args.input_file.replace('.bin', '.syx')
        else:
            output_file = args.input_file + '.syx'

    print('Firmware size: %d bytes' % (len(data)))

    # pad to a full page
    end = len(data)
    if end % page_size !=0:
        data.extend(repeat(0, (page_size - (end % page_size))))

    print('Padded size: %d bytes uses %d pages (of a maximum of 256 pages)' % (len(data), len(data)/256))

    syx_data = b''

    # data blocks
    for i in range(len(data) - page_size, -1, -page_size):
        block = encode14(page_size)
        block += encode14(int(i / page_size))

        for j in range(i, i + page_size, 4):
            block += encode32(data[j : j + 4])

        block += encode16(checkCRC(block))

        syx_data += syx_start + my_id + update_command
        syx_data += block
        syx_data += syx_end

    # indicates end of transmission
    syx_data += syx_start + my_id + update_command
    syx_data += b'\x00\x00'
    syx_data += syx_end

    print('Sysex size: %d bytes' % (len(syx_data)))

    with open(output_file, 'wb') as f:
        f.write(syx_data)

    print('Done.')
