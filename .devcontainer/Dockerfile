# Now we setup the environment for using the cross-compiler
FROM valios/vali-toolchain:v1.9

# This Dockerfile's base image has a non-root user with sudo access. Use the "remoteUser"
# property in devcontainer.json to use it. On Linux, the container user's GID/UIDs
# will be updated to match your local UID/GID (when using the dockerFile property).
# See https://aka.ms/vscode-remote/containers/non-root-user for details.
ARG CROSS_PATH=/usr/workspace/toolchain-out
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

# Setup required environmental variables
ENV CROSS=$CROSS_PATH
ENV DEBIAN_FRONTEND=noninteractive

# Set the directory
WORKDIR /usr/workspace/vali

# Copy all repository files to image
COPY . .

# Configure apt and install packages
RUN apt-get update \
    #
    # Install vali dependencies for development
    && ls \
    && sed -i 's/\r$//' ./tools/depends.sh && chmod +x ./tools/depends.sh \
    && sed -i 's/\r$//' ./tools/dotnet-install.sh && chmod +x ./tools/dotnet-install.sh \
    && ./tools/depends.sh \
    #
    # [Optional] Update UID/GID if needed
    && if [ "$USER_GID" != "1000" ] || [ "$USER_UID" != "1000" ]; then \
        groupmod --gid $USER_GID $USERNAME \
        && usermod --uid $USER_UID --gid $USER_GID $USERNAME \
        && chown -R $USER_UID:$USER_GID /home/$USERNAME; \
    fi \
    #
    # Clean up
    && apt-get autoremove -y \
    && apt-get clean -y \
    && rm -rf /var/lib/apt/lists/*
