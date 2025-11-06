# # --- Kernel module target ---
# obj-m += read_core_energy.o

# KBUILD_CFLAGS += -g -O2

# # --- Paths ---
# LIBBPF_DIR ?= /usr/lib
# BPFTOOL    ?= bpftool
# CLANG         ?= clang
# BPF_CFLAGS = -O2 -g -Wall -target bpf -D__TARGET_ARCH_x86
# CFLAGS_USER = -g -O2 -Wall -I/usr/include -I$(LIBBPF_DIR)/include -lelf -lz -lbpf

# # --- File names ---
# BPF_OBJ = test.bpf.o
# BPF_SKEL = test.skel.h
# USER_APP = test

# # --- Default target ---
# all: $(BPF_OBJ) $(USER_APP)

# # --- Compile eBPF program to .o ---
# $(BPF_OBJ): test.bpf.c
# 	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# # --- Generate BPF skeleton header ---
# $(BPF_SKEL): $(BPF_OBJ)
# 	$(BPFTOOL) gen skeleton $< > $@

# # --- Build user-space loader ---
# $(USER_APP): test.c $(BPF_SKEL)
# 	$(CLANG) $(CFLAGS_USER) test.c -o $(USER_APP)

# # --- Clean everything ---
# clean:
# 	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
# 	rm -f $(BPF_OBJ) $(BPF_SKEL) $(USER_APP)

# .PHONY: all clean

# Define the compiler and the flags
CLANG ?= clang
CFLAGS = -O2 -target bpf -c -g
USERSPACE_CFLAGS = -O2
USERSPACE_LINKER_FLAGS = -lbpf

# Userspace programs
USERSPACE_SRC = test.c
USERSPACE_BIN = $(USERSPACE_SRC:.c=.out)
# Define the BPF program source and the output object file
BPF_SRC = test.bpf.c
BPF_OBJ = $(BPF_SRC:.c=.o)

# Get Clang's default includes on this system. We'll explicitly add these dirs
# to the includes list when compiling with `-target bpf` because otherwise some
# architecture-specific dirs will be "missing" on some architectures/distros -
# headers such as asm/types.h, asm/byteorder.h, asm/socket.h, asm/sockios.h,
# sys/cdefs.h etc. might be missing.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
CLANG_BPF_SYS_INCLUDES ?= $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')


all: $(BPF_OBJ) $(USERSPACE_BIN)

$(BPF_OBJ): $(BPF_SRC) vmlinux.h
	$(CLANG) $(CFLAGS) $(CLANG_BPF_SYS_INCLUDES) $(BPF_SRC) -o $(BPF_OBJ)

$(USERSPACE_BIN): $(USERSPACE_SRC)
	$(CLANG) $(USERSPACE_CFLAGS) $(USERSPACE_SRC) -o $(USERSPACE_BIN) $(USERSPACE_LINKER_FLAGS)

vmlinux.h:
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

clean:
	rm -f $(BPF_OBJ) $(USERSPACE_BIN)

cleanall: clean
	rm -f vmlinux.h

.PHONY: all clean