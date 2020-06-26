# Now we setup the environment for using the cross-compiler
FROM valios/vali-toolchain:latest

# Build configuration arguments
# CROSS_PATH must match what is set in the toolchain image
ARG CROSS_PATH=/usr/workspace/toolchain-out
ARG ARCH

# Setup required environmental variables
ENV CROSS=$CROSS_PATH
ENV VALI_ARCH=$ARCH
ENV DEBIAN_FRONTEND=noninteractive

# Set the directory
WORKDIR /usr/workspace/vali

# copy all the files to the container
COPY . .

# Build the operating system
RUN sed -i 's/\r$//' ./tools/depends.sh && chmod +x ./tools/depends.sh && ./tools/depends.sh && \
    mkdir -p /usr/worspace/vali-build && cd /usr/worspace/vali-build && \
    cmake -G "Unix Makefiles" -DVALI_ARCH=$VALI_ARCH ../vali && \
    make
