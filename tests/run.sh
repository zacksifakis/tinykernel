#!/usr/bin/env bash
# Boots the kernel in QEMU, drives the shell over the serial port, and checks the output.
set -u
cd "$(dirname "$0")/.."

KERNEL=build/kernel.elf
TIMEOUT=15 # seconds to wait for each expected line

work=$(mktemp -d)
out="$work/serial.log"
fifo="$work/input"
mkfifo "$fifo"

qemu-system-i386 -kernel "$KERNEL" -m 128M -no-reboot -nographic -monitor none <"$fifo" >"$out" 2>&1 &
qpid=$!
exec 3>"$fifo"
trap 'kill $qpid 2>/dev/null; rm -rf "$work"' EXIT

failures=0

log() { sed $'s/\x1b\\[[0-9;]*[A-Za-z]//g' "$out" | tr -d '\r'; }
send() { printf '%s\r' "$1" >&3; }

# expect <pattern> [count]: wait until the (extended regex) pattern has appeared at least count times.
expect() {
    local pattern=$1 want=${2:-1}
    for ((i = 0; i < TIMEOUT * 10; i++)); do
        if [ "$(log | grep -cE -- "$pattern")" -ge "$want" ]; then
            printf '  \033[32mPASS\033[0m  %s\n' "$pattern"
            return 0
        fi
        sleep 0.1
    done
    printf '  \033[31mFAIL\033[0m  %s (not seen within %ss)\n' "$pattern" "$TIMEOUT"
    failures=$((failures + 1))
    return 1
}

prompt_count=0
# run <command> <expected pattern>: send a command and wait for its output and the next prompt.
run() {
    send "$1"
    expect "$2"
    prompt_count=$((prompt_count + 1))
    expect '^tiny> ' "$((prompt_count + 1))" >/dev/null || echo "        (no prompt after '$1')"
}

echo "Booting..."
expect "Type 'help' for commands" || { log; exit 1; }
expect '^tiny> ' >/dev/null

echo "Shell:"
run help 'kill <pid> +terminate a task'
run 'echo hello world' '^hello world$'
run uptime '^up [0-9]+\.[0-9]{2} seconds'
run mem '^heap: '
heap_before=$(log | grep '^heap: ' | tail -1)

echo "User programs:"
run 'run hello' "Hello from user space! I'm pid [0-9]+, running in ring 3"
run 'run primes' 'found 25 in [0-9]+ ms'
run 'run counter' '\] tick 5/5'
run 'run exit42' 'exited with status 42'

echo "Protection:"
run 'run segv' 'segv \(pid [0-9]+\) killed: Page fault .*write to kernel-only address 00100000'
run 'run null' 'null \(pid [0-9]+\) killed: Page fault .*read from unmapped address 00000000'
run 'run cli' 'cli \(pid [0-9]+\) killed: General protection fault'
run 'run badptr' 'write\(\) returned -14 -- the kernel refused'

echo "Multitasking:"
run 'run spin &' '^\[[0-9]+\] spin running in background'
spin_pid=$(log | sed -nE 's/^\[([0-9]+)\] spin running in background/\1/p' | tail -1)
expect "\[spin $spin_pid\] busy loop round 1"
run ps "^ +$spin_pid +(ready|running) +user .*spin$"
run "kill $spin_pid" "^killed $spin_pid$"
run ps '^ +1 +running +kernel .*shell$'
if log | tail -5 | grep -q ' spin$'; then
    printf '  \033[31mFAIL\033[0m  spin still listed after kill\n'
    failures=$((failures + 1))
fi
run 'kill 1' "can't kill pid 1"

send 'run spin'
expect 'busy loop round 1' 2
printf '\003' >&3
expect 'exited with status 130'
prompt_count=$((prompt_count + 1))

echo "Cleanup:"
run mem '^heap: '
heap_after=$(log | grep '^heap: ' | tail -1)
if [ "$heap_before" = "$heap_after" ]; then
    printf '  \033[32mPASS\033[0m  heap usage back to baseline (%s)\n' "${heap_before#heap: }"
else
    printf '  \033[31mFAIL\033[0m  heap leak: "%s" -> "%s"\n' "$heap_before" "$heap_after"
    failures=$((failures + 1))
fi

send shutdown
for ((i = 0; i < 50; i++)); do
    kill -0 $qpid 2>/dev/null || break
    sleep 0.1
done
if kill -0 $qpid 2>/dev/null; then
    printf '  \033[31mFAIL\033[0m  QEMU still running after shutdown\n'
    failures=$((failures + 1))
else
    printf '  \033[32mPASS\033[0m  shutdown powered off the VM\n'
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "All tests passed."
else
    echo "$failures test(s) failed. Serial log:"
    log
    exit 1
fi
