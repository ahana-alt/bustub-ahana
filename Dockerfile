FROM ubuntu:22.04
SHELL ["/bin/bash", "-lc"]
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config \
    zlib1g-dev libelf-dev libdwarf-dev doxygen vim ccache \
    clang-15 clang-tidy-15 clang-format-15 && \
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-15 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-15 100 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-15 100 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-15 100 && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
CMD ["/bin/bash"]