// Hello World - Multi-core boot test
// Boots 4 cores sequentially, each outputs via UART
//
// Boot flow:
//   1. Bootrom wakes all harts via CLINT MSIP
//   2. All harts jump here with a0=hartid, a1=dtb
//   3. Hart 0 prints first, then triggers hart 1 via MSIP
//   4. Hart 1 prints, triggers hart 2, etc.
//   5. Last hart (3) simply stops

#define CLINT_BASE   0x20000
#define UART_BASE    0x40600000
#define UART_TX      (UART_BASE + 0x4)

// MSIP offsets: CLINT_BASE + hartid * 4
#define MSIP_HART0   (CLINT_BASE + 0)
#define MSIP_HART1   (CLINT_BASE + 4)
#define MSIP_HART2   (CLINT_BASE + 8)
#define MSIP_HART3   (CLINT_BASE + 12)

#define N_CORES      4

    .section .text.init, "ax", @progbits
    .globl _start
_start:
    csrr a0, mhartid

    // ---- Hart 0: print first, then wake hart 1 ----
    li   t0, 0
    bne  a0, t0, .Lwait_core0

    // Hart 0: print "Hello World from Core 0\n"
    la   a0, .Lstr_core0
    jal  ra, uart_print

    // Wake hart 1
    li   t0, 1
    li   t1, MSIP_HART1
    sw   t0, 0(t1)

    // Hart 0 done, spin
.Lspin0:
    wfi
    j    .Lspin0

    // ---- Hart 1: wait for MSIP, print, wake hart 2 ----
.Lwait_core0:
    li   t0, 1
    bne  a0, t0, .Lwait_core1

.Lblock1:
    li   t1, MSIP_HART1
    lw   t2, 0(t1)
    beqz t2, .Lblock1

    // Clear own MSIP
    sw   zero, 0(t1)

    // Hart 1: print
    la   a0, .Lstr_core1
    jal  ra, uart_print

    // Wake hart 2
    li   t0, 1
    li   t1, MSIP_HART2
    sw   t0, 0(t1)

    // Hart 1 done, spin
.Lspin1:
    wfi
    j    .Lspin1

    // ---- Hart 2: wait for MSIP, print, wake hart 3 ----
.Lwait_core1:
    li   t0, 2
    bne  a0, t0, .Lwait_core2

.Lblock2:
    li   t1, MSIP_HART2
    lw   t2, 0(t1)
    beqz t2, .Lblock2

    // Clear own MSIP
    sw   zero, 0(t1)

    // Hart 2: print
    la   a0, .Lstr_core2
    jal  ra, uart_print

    // Wake hart 3
    li   t0, 1
    li   t1, MSIP_HART3
    sw   t0, 0(t1)

    // Hart 2 done, spin
.Lspin2:
    wfi
    j    .Lspin2

    // ---- Hart 3: wait for MSIP, print, done ----
.Lwait_core2:
    li   t0, 3
    bne  a0, t0, .Lspin_invalid

.Lblock3:
    li   t1, MSIP_HART3
    lw   t2, 0(t1)
    beqz t2, .Lblock3

    // Clear own MSIP
    sw   zero, 0(t1)

    // Hart 3: print
    la   a0, .Lstr_core3
    jal  ra, uart_print

    // Hart 3 done, spin
.Lspin3:
    wfi
    j    .Lspin3

.Lspin_invalid:
    wfi
    j    .Lspin_invalid

// ---- UART print routine ----
// a0 = pointer to null-terminated string
uart_print:
    mv   t0, a0
.Luart_loop:
    lbu  t1, 0(t0)
    beqz t1, .Luart_done
    li   t2, UART_TX
    sw   t1, 0(t2)
    addi t0, t0, 1
    j    .Luart_loop
.Luart_done:
    ret

    .section .rodata
.Lstr_core0:
    .asciz "Hello World from Core 0\n"
.Lstr_core1:
    .asciz "Hello World from Core 1\n"
.Lstr_core2:
    .asciz "Hello World from Core 2\n"
.Lstr_core3:
    .asciz "Hello World from Core 3\n"
