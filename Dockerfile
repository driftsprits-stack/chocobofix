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
 && cmake --build build -j"$(nproc)" --target trackaccess trackaccess-service test_core check_sample \
 && ./build/test_core \
 && ./build/check_sample

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
COPY --from=client /web-dist ./web
COPY --from=build /src/data/upstream/PS1/01_data ./data/public
COPY --from=build /src/data/upstream/PS1/03_submission_sample ./data/sample
# Test the exact solver binary and library layout that the final image ships.
# This catches the Cloud Run failure where the service started but every worker
# exited before main() because libortools could not be found.
RUN ./build/trackaccess validate \
      --data ./data/public \
      --submission ./data/sample
USER trackaccess
EXPOSE 8080
ENTRYPOINT ["sh", "-c", "exec ./build/trackaccess-service --host 0.0.0.0 --port ${PORT:-8080} --root /var/lib/trackaccess --web ./web --worker ./build/trackaccess --public-instance ./data/public"]
