#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
printf '\nStart GUI: ./build/jamila-game --gui\nStart CLI: ./build/jamila-game\n'
