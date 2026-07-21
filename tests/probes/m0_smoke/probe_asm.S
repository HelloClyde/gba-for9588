    .set noreorder
    .text
    .align 2

    .globl probe_asm_magic
    .type probe_asm_magic, @function
probe_asm_magic:
    lui     $v0, 0x9588
    jr      $ra
    ori     $v0, $v0, 0x4740
    .size probe_asm_magic, .-probe_asm_magic

    .globl probe_cp0_count
    .type probe_cp0_count, @function
probe_cp0_count:
    mfc0    $v0, $9
    jr      $ra
    nop
    .size probe_cp0_count, .-probe_cp0_count
