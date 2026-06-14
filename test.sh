#!/bin/sh
set -e

FERMI="${FERMI:-./fermi}"
BENCH=/tmp/fermi_bench.fe

cat > "$BENCH" << 'FE'
struct Point { x: int; y: int; }
fn dist(p: Point): int { return p.x * p.x + p.y * p.y; }
fn fib(n: int): int {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn main(): int {
    mut p = Point { x: 3, y: 4 };
    mut d = dist(p);
    return d + fib(10);
}
FE

echo "=== 1. build ==="
make build 2>&1 | tail -2

echo ""
echo "=== 2. compile --fir ==="
"$FERMI" --fir "$BENCH"

echo ""
echo "=== 3. compile --llvm (head) ==="
"$FERMI" --llvm "$BENCH" | head -8
echo "  ..."

echo ""
echo "=== 4. compile + run ==="
if "$FERMI" -o /tmp/fermi_test_bin "$BENCH" 2>/dev/null; then
    echo "exit code: $(/tmp/fermi_test_bin; echo $?)"
    rm -f /tmp/fermi_test_bin
else
    echo "(skipped: clang not available or compilation failed)"
fi

echo ""
echo "=== 5. hyperfine benchmark (target: <3ms) ==="
if command -v hyperfine >/dev/null 2>&1; then
    hyperfine \
        --warmup 20 \
        --runs 200 \
        --shell none \
        "$FERMI --fir $BENCH"
else
    echo "hyperfine not found."
    echo "Install: pkg install hyperfine     (Termux)"
    echo "         cargo install hyperfine   (cargo)"
    echo ""
    echo "Timing 100 runs with shell:"
    TIMEFORMAT="%R s total (divide by 100 for per-run ms)"
    { time for i in $(seq 100); do
        "$FERMI" --fir "$BENCH" >/dev/null 2>&1
    done; } 2>&1
fi
