# Container image for ChocoboFix.
#
# Base image note: scripts/fetch_deps.sh selects the OR-Tools binary by
# `uname -s`/`uname -m`, and for Linux/x86_64 that is the **Ubuntu 24.04** build.
# OR-Tools publishes no Debian binaries. An earlier version of this file built on
# debian:12-slim, whose glibc (2.36) is older than the one those binaries are
# linked against (2.39), so the image could not work. The base is therefore
# Ubuntu 24.04, matching the pinned binary exactly. If you move to another base,
# change fetch_deps.sh in the same commit or pass -DORTOOLS_ROOT.
#
#   docker build -f deploy/Dockerfile -t chocobofix .
#   docker run -p 8080:8080 -v chocobofix-data:/var/lib/trackaccess chocobofix

# --- stage 1: the React client --------------------------------------------
FROM node:22-slim AS client
WORKDIR /client
COPY client/package.json client/package-lock.json* ./
RUN npm ci --no-audit --no-fund
COPY client/ ./
# Vite writes to ../web-dist, which is /web-dist from this working directory.
RUN npm run build

# --- stage 2: the native service ------------------------------------------
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake curl ca-certificates \
        libsqlite3-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN ./scripts/fetch_deps.sh \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)" --target trackaccess trackaccess-service test_core check_sample
# Refuse to produce an image whose own checks do not pass.
RUN ./build/test_core && ./build/check_sample

# --- stage 3: runtime ------------------------------------------------------
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 libssl3t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home /var/lib/trackaccess --create-home trackaccess
WORKDIR /opt/chocobofix
COPY --from=build /src/build/trackaccess /src/build/trackaccess-service ./build/
# The solver is dynamically linked to the OR-Tools bundle. Give that bundle a
# stable runtime path instead of retaining CMake's build-stage /src path.
COPY --from=build /src/third_party/or-tools_*/lib/ /opt/ortools/lib/
ENV LD_LIBRARY_PATH=/opt/ortools/lib
# The React build is the interface that ships. web/ is the superseded frontend
# and is deliberately not copied; see docs/FRONTEND_MIGRATION.md.
COPY --from=client /web-dist ./web
COPY --from=build /src/data/upstream/PS1/01_data ./data/public
COPY --from=build /src/data/upstream/PS1/03_submission_sample ./data/sample
# Test the exact solver binary and library layout that the final image ships.
RUN ./build/trackaccess validate \
      --data ./data/public \
      --submission ./data/sample
USER trackaccess
VOLUME ["/var/lib/trackaccess"]
EXPOSE 8080
# 0.0.0.0 inside the container only; the container itself must sit behind the
# TLS proxy and must not be published directly to the internet.
ENTRYPOINT ["./build/trackaccess-service", \
            "--host", "0.0.0.0", "--port", "8080", \
            "--root", "/var/lib/trackaccess", \
            "--web", "./web", \
            "--worker", "./build/trackaccess", \
            "--public-instance", "./data/public"]
