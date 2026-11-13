# Userspace prototype of a mixed crc32q + VPCLMULQDQ CRC32C, benchmarked
# against the kernel's own x86 CRC32C assembly taken from KDIR unmodified.

KDIR   ?= $(HOME)/linux
CC     ?= gcc
CFLAGS ?= -O2 -march=native -Wall -Wextra
CPU    ?= 2

KX86 := $(KDIR)/lib/crc/x86
OBJS := impls.o crc32c_mixed.o k_crc32_pclmul.o k_crc32c_3way.o
HDRS := crc32c_mixed.h impls.h variants.h
KASM := -c -Ishim -D__ASSEMBLY__ -Wa,--noexecstack

# If the kernel tree has the assembly version of the mixed code, test it too.
ifneq ($(wildcard $(KX86)/crc32c-mixed.S),)
OBJS += k_crc32c_mixed.o
IMPL_DEFS := -DHAVE_KERNEL_MIXED
IMPL_DEPS := $(KX86)/crc32c-mixed-consts.h
endif

all: test bench

k_crc32_pclmul.o: $(KX86)/crc32-pclmul.S $(KX86)/crc-pclmul-template.S
	$(CC) $(KASM) -I$(KX86) -o $@ $<

k_crc32c_3way.o: $(KX86)/crc32c-3way.S
	$(CC) $(KASM) -o $@ $<

k_crc32c_mixed.o: $(KX86)/crc32c-mixed.S
	$(CC) $(KASM) -o $@ $<

# Optional A/B: a second copy of the assembly, from ALT_S, as "k-alt".
# It must take the same arguments as the version in the kernel tree.
ifneq ($(ALT_S),)
OBJS += k_alt.o
IMPL_DEFS += -DHAVE_ALT
k_alt.o: $(ALT_S)
	$(CC) $(KASM) -Dcrc32c_mixed_avx512=crc32c_mixed_alt -o $@ $<
endif

impls.o: impls.c $(HDRS) $(KX86)/crc-pclmul-consts.h $(IMPL_DEPS)
	$(CC) $(CFLAGS) $(IMPL_DEFS) -I$(KX86) -c -o $@ $<

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

test: test.o $(OBJS)
	$(CC) -o $@ $^

bench: bench.o $(OBJS)
	$(CC) -o $@ $^

check: test
	./test

run: bench
	taskset -c $(CPU) ./bench

clean:
	rm -f *.o test bench

.PHONY: all check run clean
