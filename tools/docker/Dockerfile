# The image tools/build_decomp.sh builds the decompilation (lib/perfect_dark) in: what its README asks for on Ubuntu
# (the MIPS binutils, a host gcc and g++ for ido-static-recomp and the decomp's own tools, capstone, python3), armips at
# a tag for the RSP microcode, and cmake for armips and N64Recomp. The decomp's IDO 5.3 and 7.1 are its own
# tools/recomp submodule, recompiled by its Makefile. build_decomp.sh tags the image with a hash of this file, so a
# change here makes the next run build a new one.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends build-essential binutils-mips-linux-gnu \
    libcapstone-dev python3 git cmake ninja-build ca-certificates && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch v0.11.0 --recurse-submodules https://github.com/Kingcom/armips /tmp/armips \
    && cmake -S /tmp/armips -B /tmp/armips/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/armips/build && cp /tmp/armips/build/armips /usr/local/bin/ && rm -rf /tmp/armips
