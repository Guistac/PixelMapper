# 1. Define the build argument. Docker will automatically set this 
#    based on the `--platform` flag used in the build command.
ARG TARGETPLATFORM

# 2. Use the argument in the FROM instruction. This resolves the 
#    'FromPlatformFlagConstDisallowed' warning.
FROM --platform=$TARGETPLATFORM debian:bookworm

# Install build essentials and development tools
RUN apt-get update && \
    apt-get install -y \
        build-essential \
        git \
        cmake \
        clang \
        xorg-dev \
        gtk+-3.0 \
        python3 \
        python3-pip \
        coreutils \
        file \
        dpkg-dev \
    # Clean up apt caches to minimize image size
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
CMD ["bash"]