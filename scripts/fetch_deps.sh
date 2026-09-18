#!/usr/bin/env bash
# Fetches the pinned native dependencies into third_party/.
# No sudo, no package manager, no network access at build time afterwards.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party

ORTOOLS_VERSION=9.14
ORTOOLS_BUILD=9.14.6206
HTTPLIB_VERSION=v0.18.3
HTTPLIB_SHA256=a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5

arch=$(uname -m)
os=$(uname -s)
case "$os/$arch" in
  Darwin/arm64) ASSET="or-tools_arm64_macOS-15.5_cpp_v${ORTOOLS_BUILD}.tar.gz";;
  Darwin/x86_64) ASSET="or-tools_x86_64_macOS-15.5_cpp_v${ORTOOLS_BUILD}.tar.gz";;
  Linux/x86_64) ASSET="or-tools_amd64_ubuntu-24.04_cpp_v${ORTOOLS_BUILD}.tar.gz";;
  Linux/aarch64) ASSET="or-tools_arm64_ubuntu-24.04_cpp_v${ORTOOLS_BUILD}.tar.gz";;
  *) echo "No pinned OR-Tools binary for $os/$arch." >&2
     echo "Build OR-Tools $ORTOOLS_VERSION from source and pass -DORTOOLS_ROOT=..." >&2
     exit 1;;
esac

if ls third_party/or-tools_* >/dev/null 2>&1; then
  echo "OR-Tools already present: $(ls -d third_party/or-tools_*)"
else
  echo "Fetching $ASSET"
  curl -fL --retry 3 -o third_party/ortools.tar.gz \
    "https://github.com/google/or-tools/releases/download/v${ORTOOLS_VERSION}/${ASSET}"
  echo "sha256: $(shasum -a 256 third_party/ortools.tar.gz | cut -d' ' -f1)"
  tar xzf third_party/ortools.tar.gz -C third_party
  rm third_party/ortools.tar.gz
fi

if [ ! -f third_party/httplib.h ]; then
  echo "Fetching cpp-httplib ${HTTPLIB_VERSION}"
  curl -fL --retry 3 -o third_party/httplib.h \
    "https://raw.githubusercontent.com/yhirose/cpp-httplib/${HTTPLIB_VERSION}/httplib.h"
fi
got=$(shasum -a 256 third_party/httplib.h | cut -d' ' -f1)
if [ "$got" != "$HTTPLIB_SHA256" ]; then
  echo "httplib.h checksum mismatch: got $got, expected $HTTPLIB_SHA256" >&2
  exit 1
fi
echo "httplib.h checksum verified."
echo "Dependencies ready."
