p600fw-builder
==============

This directory contains a `Dockerfile` that you can use to build the 
firmware for your Prophet 600 without having to install anything other
than Docker.

How-To
------

1. Clone this repo to your local system.
2. First, install Docker CE; at least version 17.06
3. Follow instructions for your OS below to do the build and obtain the
   firmware file.
4. After completing the build, the file p600firmware.syx in the current
   directory is a SysEx file containing the firmware. Follow the instructions
   in the user guide to install it in your Prophet 600.

### Mac OS X, Linux, other Unix-ish OS using Bash

Run the following commands in your shell:
```
cd p600fw/docker
docker image build -t p600fw-builder .
id=$(docker container create p600fw-builder)
docker container start -ai $id
docker container cp $id:/dist/p600firmware.syx .
docker container rm $id
```

### Windows

> Hopefully someone who knows both Bash and the Windows shell can
> translate the Bash instructions above and will submit a pull request 
> to add the right instructions here.  :-)
