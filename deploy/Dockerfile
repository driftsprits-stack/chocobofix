# Container image for TrackAccess.
#
# NOT BUILT OR RUN. No container runtime was available on the development
# machine, so this file is written from the same steps that were executed by
# hand on macOS and is UNVERIFIED on Linux. Build it once before relying on it;
# docs/DEPLOYMENT.md records this as an open item.
#
#   docker build -f deploy/Dockerfile -t trackaccess .
#   docker run -p 8080:8080 -v trackaccess-data:/var/lib/trackaccess trackaccess

FROM debian:12-slim AS build
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

FROM debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home /var/lib/trackaccess --create-home trackaccess
WORKDIR /opt/trackaccess
COPY --from=build /src/build/trackaccess /src/build/trackaccess-service ./build/
COPY --from=build /src/third_party/or-tools_* ./third_party/
COPY --from=build /src/web ./web
COPY --from=build /src/data/upstream/PS1/01_data ./data/public
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
