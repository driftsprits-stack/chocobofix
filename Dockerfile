# Google Cloud Run build entry point.
FROM node:22-slim AS client
WORKDIR /client
COPY client/package.json client/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY client/ ./
RUN npm run build

FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake curl ca-certificates libsqlite3-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN ./scripts/fetch_deps.sh \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)" --target trackaccess trackaccess-service test_core test_store check_sample \
 && ./build/test_core \
 && ./build/test_store \
 && ./build/check_sample \
 && mkdir -p /runtime/lib \
 && cp -a /src/third_party/or-tools_*/lib/. /runtime/lib/

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 libssl3t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home /var/lib/trackaccess --create-home trackaccess
WORKDIR /opt/chocobofix
COPY --from=build /src/build/trackaccess /src/build/trackaccess-service ./build/
# Copy the DSOs into the exact path referenced by trackaccess's relative RPATH.
# LD_LIBRARY_PATH is retained as a clear, inspectable fallback for operators.
COPY --from=build /runtime/lib/ ./lib/
COPY --from=client /web-dist ./web
COPY --from=build /src/data/upstream/PS1/01_data ./data/public
COPY --from=build /src/data/upstream/PS1/03_submission_sample ./data/sample
COPY --from=build /src/tests/data/micro ./data/smoke
ENV LD_LIBRARY_PATH=/opt/chocobofix/lib

# Test the executable after every runtime-stage copy. Build-stage tests cannot
# detect a library that was omitted or became unreachable during relocation.
RUN ldd ./build/trackaccess > /tmp/trackaccess-ldd.txt \
 && ! grep -q "not found" /tmp/trackaccess-ldd.txt \
 && ./build/trackaccess validate --data ./data/public --submission ./data/sample \
 && mkdir -p /tmp/chocobofix-smoke \
 && ./build/trackaccess solve --data ./data/smoke --out /tmp/chocobofix-smoke \
      --scenario all --seconds 15 --workers 1 \
 && test -f /tmp/chocobofix-smoke/A/VALIDATION.json \
 && test -f /tmp/chocobofix-smoke/B/VALIDATION.json \
 && test -f /tmp/chocobofix-smoke/C/VALIDATION.json \
 && rm -rf /tmp/chocobofix-smoke /tmp/trackaccess-ldd.txt
USER trackaccess
EXPOSE 8080
ENTRYPOINT ["sh", "-c", "exec ./build/trackaccess-service --host 0.0.0.0 --port ${PORT:-8080} --root /var/lib/trackaccess --web ./web --worker ./build/trackaccess --public-instance ./data/public"]
