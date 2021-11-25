# Now we setup the environment for using the cross-compiler
FROM valios/vali-toolchain:v1.9 AS build

# Build configuration arguments
# CROSS_PATH must match what is set in the toolchain image
ARG CROSS_PATH=/usr/workspace/toolchain-out
ARG ARCH

# Setup required environmental variables
ENV CROSS=$CROSS_PATH
ENV VALI_INSTALL_DIR=/usr/workspace
ENV VALI_ARCH=$ARCH
ENV VALI_APPLICATION_PATH=/usr/workspace/vali-apps
ENV PATH="/root/.dotnet:${PATH}"
ENV DOTNET_ROOT="/root/.dotnet"
ENV DEBIAN_FRONTEND=noninteractive

# Set the directory
WORKDIR /usr/workspace/vali

# Copy all repository files to image
COPY . .

# Build the operating system
RUN sed -i 's/\r$//' ./tools/depends.sh && chmod +x ./tools/depends.sh && chmod +x ./tools/dotnet-install.sh && \
    chmod +x ./tools/ci-nightly.sh && ./tools/depends.sh && mkdir -p $VALI_APPLICATION_PATH && cd $VALI_APPLICATION_PATH && \
    /usr/workspace/vali/tools/ci-nightly.sh && cd /usr/workspace/vali && mkdir -p build && cd build && \
    cmake -G "Unix Makefiles" -DVALI_ARCH=$VALI_ARCH -DCMAKE_INSTALL_PREFIX=$VALI_INSTALL_DIR .. && \
    make && make install_img && tar -czvf vali-nightly.tar.gz ./mollenos.img

# Make an artifact stage specifically for building output with the command
# DOCKER_BUILDKIT=1 docker build --target artifact --output type=local,dest=. .
FROM scratch AS artifact
COPY --from=build /usr/workspace/vali/build/vali-nightly.tar.gz /vali-nightly.tar.gz
