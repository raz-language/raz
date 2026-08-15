#!/usr/bin/env sh
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

set -eu
exec python3 "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/scripts/bootstrap.py" "$@"
