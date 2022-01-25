import collections
import optparse


parser = optparse.OptionParser()
#parser.add_option(
#    '-o',
#    '--output_file',
#    dest='output_file',
#    default=None,
#    help='Write output file to FILE',
#    metavar='FILE')

parser.add_option(
    '-p',
    '--patch_number',
    dest='patch_number',
    default=None,
    help='Patch number to be extracted and converted',
    metavar='NUM')


options, args = parser.parse_args()

data = []
spec7 = []
spec8 = []

fittingSpec = []

fileVar = open("storage_7.spec","rt")
for c in fileVar.readlines():
	spec7.append([c.split(';')[0],int(c.split(';')[1]),int(c.split(';')[2])])
fileVar.close()

fileVar = open("storage_8.spec","rt")
for c in fileVar.readlines():
	spec8.append([c.split(';')[0],int(c.split(';')[1]),int(c.split(';')[2])])
fileVar.close()

fileVar = open(args[0],"rb")
f = fileVar.read(1)

while f:
	inVal = int.from_bytes(f,"big")
	data += [inVal]
	f=fileVar.read(1)

fileVar.close()

countPatches=0
expectStructure=240
byteCount=0
i=0

patches = []
patch = []
#collections.defaultdict(lambda:0)

while i < len(data):


    if expectStructure==256:
        if data[i]==247: # end of patch
            patches.append(patch)
            patch = []
            expectStructure=240
            countPatches+=1
            i+=1
            continue

        if i+4 > len(data)-1:
            print('Non SysEx structure: proper F7 closure missing or byte sequence incomplete. Stopped at input byte: ', i)
            break


        patch.append(data[i] + 128 * ((data[i+4]) & 1))
        patch.append(data[i+1] + 128 * ((data[i+4]>>1) & 1))
        patch.append(data[i+2] + 128 * ((data[i+4]>>2) & 1))
        patch.append(data[i+3] + 128 * ((data[i+4]>>3) & 1))


        if data[i]>127 or data[i+1]>127 or data[i+2]>127 or data[i+3]>127:
            print('Non SysEx structure: byte exceeding MIDI byte size. Stopped at input byte: ', i)
            break

        byteCount+=4
        i+=5
        continue

    else:

        if data[i]!=expectStructure:
            print('Non SysEx structure. Expected: ', expectStructure, ", found: ", data[i])
            break

        if data[i]==240:
            expectStructure=0
            byteCount=0

        if data[i]==0: expectStructure=97
        if data[i]==97: expectStructure=22
        if data[i]==22: expectStructure=1
        if data[i]==1: expectStructure=256

        i+=1


#print('Patches found: ', countPatches)

# interpret

if not options.patch_number:
    filter=-1
else:
    filter=int(options.patch_number)


for patch in patches:

    if filter>=0 and patch[0]!=filter:
        continue

    print('Patch Number: ', patch[0])

    if patch[1]!=165 or patch[2]!=22 or patch[3]!=97 or patch[4]!=0:
        print('Storage Magic is not found')
        quit()


    if patch[5]==7:
        fittingSpec=spec7
        print('Storage version is 7')
    elif patch[5]==8:
        fittingSpec=spec8
        print('Storage version is 8')
    else:
        print('Unsupported storage version: ', patch[5])
        quit()

    i=6
    msb=0
    lsb=0
    for spec in fittingSpec:
        for cnt in range(0,spec[1]):
            if spec[2]==2:
                if i>len(patch)-1:
                    msb=0
                    lsb=0
                elif i+1>len(patch)-1: # assume zero
                    msb=0
                    lsb=patch[i]
                else:
                    msb=patch[i+1]
                    lsb=patch[i]

                i+=2
            else:
                msb=0
                if i>len(patch)-1:
                    lsb=0
                else:
                    lsb=patch[i]

                i+=1

            if spec[1]>1:
                print(spec[0], '(', cnt+1, ' of ', spec[1],'): ', msb*256+lsb)
            else:
                print(spec[0], ': ', msb*256+lsb)

