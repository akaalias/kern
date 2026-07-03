# Toolchain image for the pre-push hook's Linux-parity step. Mirrors the CI
# runner (ubuntu-latest + clang/llvm/libsdl2-dev) so the same test / coverage /
# perf jobs run locally on Linux before a push — catching Linux-only breakage
# (missing -lm, glibc-hidden POSIX, LeakSanitizer leaks, SDL header differences)
# that a macOS-only gate can't see.
#
# Built once and cached as the `kern-ci` image (the hook builds it on first use).
# After editing this file, force a rebuild with:  docker rmi kern-ci
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
# libclang-rt-18-dev ships the compiler-rt sanitizer static libs (libclang_rt.asan*).
# CI's x86 runner has them via `clang`, but on an arm64 host the image runs arm64
# Linux where the `clang` package omits the aarch64 runtime — install it explicitly
# so the ASan/UBSan test build links regardless of host architecture.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
         clang llvm libclang-rt-18-dev libsdl2-dev make python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
