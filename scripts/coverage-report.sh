#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${repo_root}/build/coverage"
html_dir="${build_dir}/html"
html_report="${html_dir}/index.html"

cd "${repo_root}"

if ! command -v gcovr >/dev/null 2>&1; then
    echo "error: gcovr is not installed or not in PATH" >&2
    exit 1
fi

cmake --workflow --preset coverage

mkdir -p "${html_dir}"

gcovr -r . \
    --object-directory "${build_dir}" \
    --filter 'lib/' \
    --filter 'apps/' \
    --exclude '.*/test/.*' \
    --exclude 'build/.*' \
    --txt

gcovr -r . \
    --object-directory "${build_dir}" \
    --filter 'lib/' \
    --filter 'apps/' \
    --exclude '.*/test/.*' \
    --exclude 'build/.*' \
    --html-details "${html_report}"

echo "coverage report: ${html_report}"
