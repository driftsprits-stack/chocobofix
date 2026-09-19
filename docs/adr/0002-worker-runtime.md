# ADR 0002: Isolate the solver and package its libraries

Status: accepted

## Context

The OR-Tools solver can use significant CPU and memory. It also needs native
shared libraries. The Cloud Run image moves the executable after the build.

## Decision

Run each solve in a child process with CPU, memory, and file-size limits. Send
`exec` failures to the parent through a close-on-exec pipe. Capture bounded
stdout and stderr. Record exit codes and signals separately. Copy the OR-Tools
libraries to `/opt/chocobofix/lib`. Add `$ORIGIN/../lib` to the executable
runtime path. Run A, B, and C in the final image build.

## Consequences

A worker crash does not stop the HTTP service. The UI can distinguish a launch
error, a missing runtime library, invalid input, a solve timeout, and a signal.
The final-image smoke test detects missing libraries before deployment.
