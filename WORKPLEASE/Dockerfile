FROM ubuntu:groovy

ENV TZ Europe/Warsaw
RUN apt update
RUN apt install -y qemu-system-x86 sudo

# uid and gid have to match the host system
RUN addgroup --gid 1000 developer
RUN adduser --uid 1000 --gid 1000 --disabled-password --gecos "" developer

# Replace 1000 with your user / group id
#RUN export uid=1000 gid=1000 && \
#    mkdir -p /home/developer && \
#    echo "developer:x:${uid}:${gid}:Developer,,,:/home/developer:/bin/bash" >> /etc/passwd && \
#    echo "developer:x:${uid}:" >> /etc/group && \
#    echo "developer ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/developer && \
#    chmod 0440 /etc/sudoers.d/developer && \
#    chown ${uid}:${gid} -R /home/developer
RUN echo "developer ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/developer && \
    chmod 0440 /etc/sudoers.d/developer

USER developer
ENV HOME /home/developer
