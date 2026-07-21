    .set noreorder
    .text
    .align 2

    .globl probe_cp0_count
    .type probe_cp0_count, @function
probe_cp0_count:
    mfc0    $v0, $9
    jr      $ra
    nop
    .size probe_cp0_count, .-probe_cp0_count
