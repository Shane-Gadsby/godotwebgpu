#!/usr/bin/env bash
exec "$(dirname "$0")/../../webgpu_tests/converter_smoke/run.sh" "$@" # moved there (TASKS.md §2.38); worker prompts name this path
