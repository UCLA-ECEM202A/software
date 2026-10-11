FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git curl wget xz-utils file make gcc g++ \
    cmake ninja-build gperf device-tree-compiler python3 python3-venv \
    python3-dev libffi-dev libssl-dev libmagic1 unzip \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:${PATH}"
RUN pip install --no-cache-dir west==1.5.0
WORKDIR /opt/workspace
RUN west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.4.2 . \
    && west config manifest.project-filter -- +tflite-micro \
    && west update --narrow -o=--depth=1 cmsis cmsis_6 hal_nordic picolibc segger tflite-micro \
    && west zephyr-export \
    && pip install --no-cache-dir -r zephyr/scripts/requirements-base.txt
# Install setuptools (provides distutils in Python 3.12) and emlearn
RUN pip install --no-cache-dir setuptools emlearn
ENV ZEPHYR_BASE=/opt/workspace/zephyr
WORKDIR /opt/workspace/zephyr
RUN west sdk install --toolchains arm-zephyr-eabi
WORKDIR /work
