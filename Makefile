CC   := clang
LD   := ld.lld
NM   := nm
QEMU := qemu-system-i386

CFLAGS := -target i686-elf -std=gnu11 -O2 -g -Wall -Wextra \
          -ffreestanding -fno-builtin -nostdlib -fno-stack-protector -fno-pic \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 -Iinclude
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z max-page-size=0x1000

QEMU_FLAGS := -kernel build/kernel.elf -m 128M -no-reboot

SRCS := $(wildcard src/*.c)
OBJS := build/boot.o $(patsubst src/%.c,build/%.o,$(SRCS)) build/user.o
HDRS := $(wildcard src/*.h include/*.h)

.PHONY: all run run-term test clean

all: build/kernel.elf

build:
	mkdir -p build

build/boot.o: src/boot.S | build
	$(CC) -target i686-elf -c $< -o $@

build/%.o: src/%.c $(HDRS) | build
	$(CC) $(CFLAGS) -c $< -o $@

# User code runs in ring 3 and can't call kernel functions; refuse to link if it tries.
build/user.o: user/user.c include/abi.h | build
	$(CC) $(CFLAGS) -c $< -o $@
	@undef="$$($(NM) -u $@)"; if [ -n "$$undef" ]; then \
		echo "error: user code references kernel symbols:"; echo "$$undef"; rm -f $@; exit 1; fi

build/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# QEMU window (VGA + keyboard); the serial console is mirrored to this terminal.
run: build/kernel.elf
	$(QEMU) $(QEMU_FLAGS) -serial stdio

# Everything in this terminal. Quit QEMU with Ctrl-A then X.
run-term: build/kernel.elf
	$(QEMU) $(QEMU_FLAGS) -nographic

# Boot in QEMU, drive the shell over serial, and check the results.
test: build/kernel.elf
	tests/run.sh

clean:
	rm -rf build
