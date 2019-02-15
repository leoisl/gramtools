FROM ubuntu:17.10
COPY ./install.sh ./
RUN chmod +x ./install.sh
RUN bash -eux ./install.sh
ENTRYPOINT ["/bin/bash", "-c", "cd /gramtools && /bin/bash"]
CMD []
