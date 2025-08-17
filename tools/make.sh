#!/usr/bin/env bash
set -e
docker run -it --rm \
  -v "$PWD":/work \
  -w /work \
  agodio/itba-so-multi-platform:3.0 \
  make "$@"
