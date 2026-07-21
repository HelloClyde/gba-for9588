    .set noreorder
    .text
    .align 2

    .globl bda_cache_sync
    .type bda_cache_sync, @function
bda_cache_sync:
    addiu   $t2, $zero, -32
    and     $t0, $a0, $t2
    addiu   $t1, $a1, 31
    and     $t1, $t1, $t2

1:
    sltu    $t3, $t0, $t1
    beqz    $t3, 2f
    nop
    cache   0x15, 0($t0)
    b       1b
    addiu   $t0, $t0, 32

2:
    sync
    and     $t0, $a0, $t2

3:
    sltu    $t3, $t0, $t1
    beqz    $t3, 4f
    nop
    cache   0x10, 0($t0)
    b       3b
    addiu   $t0, $t0, 32

4:
    sync
    jr      $ra
    nop
    .size bda_cache_sync, .-bda_cache_sync
