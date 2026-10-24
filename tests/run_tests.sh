#!/bin/sh
# run_tests.sh: builds and runs the host tests for Claude 68's engine on this
# computer, under ASan and UBSan. They compile the code straight from a
# Claude 68 checkout: ../Claude68 next to this repo, or $CLAUDE68_DIR.
#
#   tests/run_tests.sh
#
# What each test checks is at the top of its file.
set -e
cd "$(dirname "$0")/.."
C68="${CLAUDE68_DIR:-../Claude68}"
NET="$C68/src/core/net"
if [ ! -f "$NET/uECC.c" ]; then
    echo "run_tests: Claude 68 not found at $C68; set CLAUDE68_DIR" >&2
    exit 2
fi
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
export UBSAN_OPTIONS="suppressions=tests/ubsan.supp"
UECC="-DuECC_OPTIMIZATION_LEVEL=2 -DuECC_WORD_SIZE=4"

run() {
    name=$1; shift
    echo "== $name"
    cc -std=gnu99 -O1 -g -fsanitize=address,undefined "$@" -o "$OUT/$name"
    "$OUT/$name"
}

# The app's own parsing: SSE, JSON and UTF-8, on a stub HttpClient
echo "== sse_json_utf8"
c++ -std=c++11 -g -Wall -fsanitize=address,undefined \
    -I tests/stubs -I "$C68/src" -I "$C68/src/core" -I "$C68/src/core/helpers" \
    tests/test_host.cpp "$C68/src/core/sse.cpp" "$C68/src/api/claude_client.cpp" \
    "$C68/src/core/helpers/utf8_mac.cpp" -o "$OUT/sse_json_utf8"
"$OUT/sse_json_utf8"

# P-256: the Comba square and Solinas reduction models, the 4-point verify,
# and the 2-bit and 3-bit windows Speed Test rebuilds
run vli_square_model tests/test_vli_square_model.c
run solinas_model    tests/test_solinas_model.c
run uecc_verify4     $UECC -I "$NET" tests/test_uecc_verify4.c "$NET/uECC.c"
run windows          $UECC -DuECC_SQUARE_FUNC=1 -DuECC_SUPPORTS_secp160r1=0 \
                     -DuECC_SUPPORTS_secp192r1=0 -DuECC_SUPPORTS_secp224r1=0 \
                     -DuECC_SUPPORTS_secp256k1=0 -I src -I src/variants -I "$NET" \
                     tests/test_windows.c
# X25519: today's code against RFC 7748 and donna, and the fused donna model
run curve25519_sat   -std=gnu11 -I "$NET" -I src/variants tests/test_curve25519_sat.c
run donna_fused      -I "$NET" -I src/variants tests/test_donna_fused.c
# The TLS random seed's interrupt counting, on stubs
run mac_entropy      -I tests/stubs_entropy -I "$NET" tests/test_mac_entropy.c "$NET/mac_entropy.c"

echo "All tests passed."
