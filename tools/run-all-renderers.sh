#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# Runs the suite once per renderer compiled into an existing cna-benchmark binary and puts the
# results side by side.
#
#     tools/run-all-renderers.sh [build-dir] [output-dir] [-- extra cna-benchmark args...]
#
# One binary, one process per renderer. The process boundary is required rather than tidy: CNA
# latches the renderer selection when the first GraphicsDevice is created, so a second renderer
# in the same process is refused by design.

set -euo pipefail

build_dir="${1:-build}"
output_dir="${2:-results}"
shift 2 2>/dev/null || true
if [[ "${1:-}" == "--" ]]; then
    shift
fi

binary="${build_dir}/cna-benchmark"
if [[ ! -x "${binary}" ]]; then
    echo "run-all-renderers: ${binary} is not an executable. Build it first:" >&2
    echo "  cmake --build ${build_dir} --target cna-benchmark --parallel 8" >&2
    exit 1
fi

mkdir -p "${output_dir}"

# The renderer list comes from the binary itself, so this script never disagrees with what was
# actually compiled in.
mapfile -t renderers < <("${binary}" --list-renderers |
    sed -n 's/^  \([A-Z0-9_]*\).*/\1/p')

if [[ ${#renderers[@]} -eq 0 ]]; then
    echo "run-all-renderers: could not read the renderer list from ${binary}." >&2
    exit 1
fi

echo "Renderers to run: ${renderers[*]}"
echo "Results directory: ${output_dir}"
echo

failed=()
for renderer in "${renderers[@]}"; do
    echo "=============================================================="
    echo " ${renderer}"
    echo "=============================================================="
    if "${binary}" --renderer="${renderer}" \
        --json="${output_dir}/${renderer}.json" "$@"; then
        :
    else
        echo "run-all-renderers: ${renderer} did not complete." >&2
        failed+=("${renderer}")
    fi
    echo
done

shopt -s nullglob
produced=("${output_dir}"/*.json)
if [[ ${#produced[@]} -gt 0 ]]; then
    python3 "$(dirname "$0")/compare-results.py" "${produced[@]}"
fi

if [[ ${#failed[@]} -gt 0 ]]; then
    echo
    echo "run-all-renderers: these renderers did not complete: ${failed[*]}" >&2
    exit 1
fi
