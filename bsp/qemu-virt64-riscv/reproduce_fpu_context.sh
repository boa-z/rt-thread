#!/usr/bin/env bash
#
# Reproduce the RISC-V Common Port FPU context bug on bsp/qemu-virt64-riscv.
#
# The expected result on an unfixed 7395c5d7f0 tree is:
#   FPU context summary: A=FAIL B=FAIL C=PASS D=FAIL
#
# Usage:
#   ./reproduce_fpu_context.sh [--expect fail|pass] [--timeout SEC] [--no-build]
#
# Environment:
#   RTT_EXEC_PATH   toolchain bin directory (required if the toolchain is
#                   not on PATH)
#   RTT_CC_PREFIX   tool prefix (default: riscv64-unknown-elf-)
#
# Exit status:
#   0  observed result matches --expect (default: fail)
#   1  observed result does not match --expect
#   2  build/environment error
#   3  test did not run (no summary found)

set -u

SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
cd "$(dirname "$SELF")"

CC_PREFIX="${RTT_CC_PREFIX:-riscv64-unknown-elf-}"
TOOLCHAIN="${RTT_EXEC_PATH:-}"

if [ -z "$TOOLCHAIN" ]; then
    if command -v "${CC_PREFIX}gcc" >/dev/null 2>&1; then
        TOOLCHAIN="$(dirname "$(command -v "${CC_PREFIX}gcc")")"
    else
        echo "error: set RTT_EXEC_PATH to the toolchain bin directory" >&2
        exit 2
    fi
fi
EXPECT="fail"
QEMU_TIMEOUT=45
DO_BUILD=1
LOG="${TMPDIR:-/tmp}/reproduce_fpu_context-$(date +%Y%m%d-%H%M%S).log"

while [ $# -gt 0 ]; do
    case "$1" in
        --expect) EXPECT="$2"; shift 2 ;;
        --timeout) QEMU_TIMEOUT="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        -h|--help) sed -n '2,25p' "$SELF" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

case "$EXPECT" in fail|pass) ;; *) echo "--expect must be fail or pass" >&2; exit 2 ;; esac

export RTT_EXEC_PATH="$TOOLCHAIN"
export RTT_CC_PREFIX="$CC_PREFIX"
unset CPATH

if [ "$DO_BUILD" -eq 1 ]; then
    echo "== building"
    scons -j"$(nproc)" >"$LOG.build" 2>&1 || {
        echo "error: build failed; see $LOG.build" >&2
        tail -30 "$LOG.build" >&2
        exit 2
    }
fi

echo "== running QEMU (timeout ${QEMU_TIMEOUT}s)"
timeout "$QEMU_TIMEOUT" python3 qemu.py \
    -no-aia -no-net -no-nvme -no-virtio-console -no-pci-serial >"$LOG" 2>&1
rc=$?
if [ $rc -ne 0 ] && [ $rc -ne 124 ]; then
    echo "warning: qemu.py exited $rc (124 means the outer timeout fired)" >&2
fi

SUMMARY="$(grep -a 'FPU context summary:' "$LOG" | tail -1 || true)"
if [ -z "$SUMMARY" ]; then
    echo "error: no FPU context summary in $LOG" >&2
    exit 3
fi

echo "   $SUMMARY"
echo "   full log: $LOG"

case "$EXPECT:$SUMMARY" in
    *"A=FAIL B=FAIL C=PASS D=FAIL"*) [ "$EXPECT" = fail ] && exit 0 || exit 1 ;;
    *"A=PASS B=PASS C=PASS D=PASS"*) [ "$EXPECT" = pass ] && exit 0 || exit 1 ;;
    *) exit 1 ;;
esac
