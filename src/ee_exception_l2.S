#define ABI_EABI64 // force all register names to EABI64 (legacy toolchain)
#include "as_reg_compat.h"

.extern el2_stack
.extern el2_regs
.extern el2_c_handler

.text

.set push
.set noreorder  # disable assembler reorder mode, so the code will not be optimized or changed in any way, giving complete instruction order control to the programmer
.set noat       # disable assembler from using register $1 (known as the assembler temporary, or $at register) to hold intermediate values when performing macro expansions

# el2_regs layout (matches ee_registers_t in include/ee_asm.h)
# 0x000 zero  0x010 at    0x020 v0    0x030 v1
# 0x040 a0    0x050 a1    0x060 a2    0x070 a3
# 0x080 t0    0x090 t1    0x0A0 t2    0x0B0 t3
# 0x0C0 t4    0x0D0 t5    0x0E0 t6    0x0F0 t7
# 0x100 s0    0x110 s1    0x120 s2    0x130 s3
# 0x140 s4    0x150 s5    0x160 s6    0x170 s7
# 0x180 t8    0x190 t9    0x1A0 k0    0x1B0 k1
# 0x1C0 gp    0x1D0 sp    0x1E0 fp    0x1F0 ra
# 0x200 hi    0x210 lo

#----------------------------------------------------------------------------
.global el2_asm_handler
.ent    el2_asm_handler
el2_asm_handler:
    # Save full interrupted context into el2_regs.
    # $k1 is used to save the context by the Level 2 exception handler.
    # $k1 is reserved for kernel usage in interrupt and trap handling.
    sq      $k1, -0x20($zero)                       # Store $k1 register in the same place as the kernel does, via kseg3.
    la      $k1, el2_regs
    sq      $zero, 0x000($k1)
    sq      $at, 0x010($k1)
    sq      $v0, 0x020($k1)
    sq      $v1, 0x030($k1)
    sq      $a0, 0x040($k1)
    sq      $a1, 0x050($k1)
    sq      $a2, 0x060($k1)
    sq      $a3, 0x070($k1)
    sq      $t0, 0x080($k1)
    sq      $t1, 0x090($k1)
    sq      $t2, 0x0A0($k1)
    sq      $t3, 0x0B0($k1)
    sq      $t4, 0x0C0($k1)
    sq      $t5, 0x0D0($k1)
    sq      $t6, 0x0E0($k1)
    sq      $t7, 0x0F0($k1)
    sq      $s0, 0x100($k1)
    sq      $s1, 0x110($k1)
    sq      $s2, 0x120($k1)
    sq      $s3, 0x130($k1)
    sq      $s4, 0x140($k1)
    sq      $s5, 0x150($k1)
    sq      $s6, 0x160($k1)
    sq      $s7, 0x170($k1)
    sq      $t8, 0x180($k1)
    sq      $t9, 0x190($k1)
    sq      $k0, 0x1A0($k1)
    sq      $gp, 0x1C0($k1)
    sq      $sp, 0x1D0($k1)
    sq      $fp, 0x1E0($k1)
    sq      $ra, 0x1F0($k1)
    pmfhi   $v0
    sq      $v0, 0x200($k1)
    pmflo   $v0
    sq      $v0, 0x210($k1)
    lq      $v0, -0x20($zero)
    sq      $v0, 0x1B0($k1)                         # $k1 can now be saved properly.

    # Switch to dedicated EL2 stack in kseg0.
    la      $sp, el2_stack + 256 + 0x80000000

    # Call C handler: a0 = &el2_regs.
    jal     el2_c_handler
    move    $a0, $k1

    # Restore context from el2_regs.
    la      $k1, el2_regs
    #lq      $zero, 0x000($k0)
    lq      $at, 0x010($k1)
    lq      $v0, 0x020($k1)
    lq      $v1, 0x030($k1)
    lq      $a0, 0x040($k1)
    lq      $a1, 0x050($k1)
    lq      $a2, 0x060($k1)
    lq      $a3, 0x070($k1)
    lq      $t0, 0x080($k1)
    lq      $t1, 0x090($k1)
    lq      $t2, 0x0A0($k1)
    lq      $t3, 0x0B0($k1)
    lq      $t4, 0x0C0($k1)
    lq      $t5, 0x0D0($k1)
    lq      $t6, 0x0E0($k1)
    lq      $t7, 0x0F0($k1)
    lq      $s0, 0x100($k1)
    lq      $s1, 0x110($k1)
    lq      $s2, 0x120($k1)
    lq      $s3, 0x130($k1)
    lq      $s4, 0x140($k1)
    lq      $s5, 0x150($k1)
    lq      $s6, 0x160($k1)
    lq      $s7, 0x170($k1)
    lq      $t8, 0x180($k1)
    lq      $t9, 0x190($k1)
    #lq      $k0, 0x1A0($k1) # restore these last
    #lq      $k1, 0x1B0($k1) # restore these last
    lq      $gp, 0x1C0($k1)
    lq      $sp, 0x1D0($k1)
    lq      $fp, 0x1E0($k1)
    lq      $ra, 0x1F0($k1)
    lq      $k0, 0x200($k1)
    pmthi   $k0
    lq      $k0, 0x210($k1)
    pmtlo   $k0
    lq      $k0, 0x1A0($k1)
    lq      $k1, 0x1B0($k1)

    # Return from exception.
    eret
.end el2_asm_handler


.set pop
