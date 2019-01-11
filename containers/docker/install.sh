#!/usr/bin/env bash

apt-get update
apt-get install -y \
    build-essential \
    libbz2-dev \
    liblzma-dev \
    cmake \
    git \
    python3-pip \
    autoconf \
    sudo \
    wget \
    zlib1g-dev

sudo pip3 install -vvv --process-dependency-links wheel git+https://github.com/iqbal-lab-org/gramtools

apt-get autoremove --purge -y \
    cmake \
    git \
    autoconf \
    wget

apt-get clean

export SUDO_FORCE_REMOVE=yes
apt-get autoremove --purge -y sudo