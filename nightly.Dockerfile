# Now we setup the environment for using the cross-compiler
FROM valios/vali-toolchain:v1.9

# Build configuration arguments
# CROSS_PATH must match what is set in the toolchain image
ARG CROSS_PATH=/usr/workspace/toolchain-out
ARG ARCH

# Setup required environmental variables
ENV CROSS=$CROSS_PATH
ENV VALI_INSTALL_DIR=/usr/workspace
ENV VALI_ARCH=$ARCH
ENV VALI_APPLICATION_PATH=/usr/workspace/vali-apps
ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/.dotnet:${PATH}"

# Set the directory
WORKDIR /usr/workspace/vali

# Copy all repository files to image
COPY . .

# Build the operating system
RUN sed -i 's/\r$//' ./tools/depends.sh && chmod +x ./tools/depends.sh && chmod +x ./tools/dotnet-install.sh && \
    ./tools/depends.sh && mkdir -p $VALI_APPLICATION_PATH && cd $VALI_APPLICATION_PATH && \
    curl -sL https://api.github.com/repos/meulengracht/vali-userspace/actions/workflows/nightly-$VALI_ARCH.yml/runs | jq -r '.workflow_runs[0].id?' && \
    cd /usr/workspace/vali && mkdir -p build && cd build && \
    cmake -G "Unix Makefiles" -DVALI_ARCH=$VALI_ARCH -DCMAKE_INSTALL_PREFIX=$VALI_INSTALL_DIR .. && \
    make && make install_vmdk && tar -czvf vali-nightly-$VALI_ARCH.tar.gz ./mollenos.vmdk && \
    mkdir -p /github/workspace/out && cp ./vali-nightly-$VALI_ARCH.tar.gz /github/workspace/out/vali-nightly-$VALI_ARCH.tar.gz
