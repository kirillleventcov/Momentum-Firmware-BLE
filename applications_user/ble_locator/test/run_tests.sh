#!/bin/sh
# Host-side tests for the BLE Locator detection engine.
# The parsing/fingerprinting/matching logic is plain C with no Flipper
# dependencies beyond a handful of stubs, so it can be exercised on a PC.
#
#   ./applications_user/ble_locator/test/run_tests.sh
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=$(dirname "$DIR")
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I"$DIR/stubs" -I"$APP" \
    "$DIR/test_bl.c" "$APP/helpers/bl_adv.c" "$APP/helpers/bl_group.c" "$APP/helpers/bl_order.c" "$APP/helpers/bl_members.c" \
    -o "$OUT/test_bl"

"$OUT/test_bl"
