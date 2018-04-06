FROM debian:stable

RUN apt-get update && \
    apt-get install -y build-essential gcc-avr avr-libc git python2.7 && \
    ln /usr/bin/python2.7 /usr/bin/python

COPY p600fw-build.sh /bin/p600fw-build
RUN chmod +x /bin/p600fw-build

CMD [ "/bin/p600fw-build" ]
