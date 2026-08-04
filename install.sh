#!/bin/sh
#
# Install the c-test runner and its header into ~/.local.
# Any existing files in those locations are overwritten.
#
set -e

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

bin_dir="${HOME}/.local/bin"
inc_dir="${HOME}/.local/include"

mkdir -p "$bin_dir" "$inc_dir"

install -m 644 "$dir/ctest.h" "$inc_dir/ctest.h"
install -m 755 "$dir/dist/c-test" "$bin_dir/c-test"

echo "installed $bin_dir/c-test"
echo "installed $inc_dir/ctest.h"
