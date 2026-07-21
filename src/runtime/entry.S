    .set noreorder
    .set noat

    .section .text.bda_entry,"ax",@progbits
    .align 2
    .globl bda_entry
    .type bda_entry, @function
bda_entry:
    addiu   $sp, $sp, -16
    sw      $ra, 12($sp)

    lui     $t0, %hi(__bss_start)
    addiu   $t0, $t0, %lo(__bss_start)
    lui     $t1, %hi(__bss_end)
    addiu   $t1, $t1, %lo(__bss_end)
1:
    sltu    $t2, $t0, $t1
    beqz    $t2, 2f
    nop
    sw      $zero, 0($t0)
    b       1b
    addiu   $t0, $t0, 4
2:
    jal     bda_run_init_array
    nop
    jal     bda_main
    nop

    lw      $ra, 12($sp)
    jr      $ra
    addiu   $sp, $sp, 16
    .size bda_entry, .-bda_entry
