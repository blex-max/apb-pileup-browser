# trixie for htslib >= 1.17
FROM debian:trixie-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        git \
        ca-certificates \
        libsqlite3-dev \
        libhts-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/pileup-browser
COPY CMakeLists.txt CMakeLists.txt
COPY src src
COPY tests tests

RUN cmake -S . -B build \
    && cmake --build build -j"$(nproc)"

FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 \
        libhts3 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/pileup-browser/build/apb /usr/local/bin/apb

ENTRYPOINT ["/usr/local/bin/apb"]
