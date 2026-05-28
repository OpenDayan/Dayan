// Hello World - Single-core boot test
// Boots hart 0, outputs via UART, then spins

#define UART_BASE    0x40600000
#define UART_TX      (UART_BASE + 0x4)

    .section .text.init, "ax", @progbits
    .globl _start
_start:
    csrr a0, mhartid

    // Only hart 0 proceeds, others spin
    li   t0, 0
    bne  a0, t0, .Lspin_invalid

    // Hart 0: print "Hello World!\n"
    la   a0, .Lstr_hello
    jal  ra, uart_print

    // Done, spin
.Lspin0:
    nop
    j    .Lspin0

.Lspin_invalid:
    nop
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
.Lstr_hello:
    .asciz "Hello World!\n"
