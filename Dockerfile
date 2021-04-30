FROM debian:bullseye

RUN apt update \
 && apt install -y gcc python3-pip ninja-build libglib2.0-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libobs-dev libjson-glib-dev libsimde-dev \
 && pip3 install meson \
 && rm -rf /var/lib/apt/lists/*
