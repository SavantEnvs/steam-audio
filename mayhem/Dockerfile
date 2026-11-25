# syntax=docker/dockerfile:1.7
# Commit image for steam-audio (libphonon core). Builds on the org C/C++ base.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Upstream's core/build/get_dependencies.py fetches these at the commits pinned in
# core/build/dependencies.json. They are built once here, uninstrumented, from those pinned commits into
# /opt/toolchains/deps/<name>-<rev>/ (SPEC §6.2 item 19); build.sh points CMake's Find*.cmake at them.
# zlib comes from the base image (zlib1g-dev, static libz.a).
USER root
COPY mayhem/install-deps.sh /tmp/install-deps.sh
RUN sh /tmp/install-deps.sh && rm /tmp/install-deps.sh
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
