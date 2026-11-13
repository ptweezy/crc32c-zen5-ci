/* Userspace stand-in for the kernel's <linux/linkage.h>, just enough to
 * assemble the files in lib/crc/x86 unmodified for benchmarking. */
#ifndef SHIM_LINUX_LINKAGE_H
#define SHIM_LINUX_LINKAGE_H

#define SYM_FUNC_START(name) .globl name; .type name, @function; .balign 16; name:
#define SYM_FUNC_END(name) .size name, . - name
#define RET ret

#endif
