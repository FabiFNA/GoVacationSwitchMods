// Module header for the game's rtld: word 0 is unused, word 1 points at the MOD0 block.
// rtld reads MOD0 to find our dynamic section, bss range and a scratch area for its module object.

.section .text.modstart, "ax", %progbits
.global __module_header
__module_header:
    .word 0
    .word __mod0 - __module_header

.align 2
__mod0:
    .ascii "MOD0"
    .word _DYNAMIC - __mod0
    .word __bss_start - __mod0
    .word __bss_end - __mod0
    .word 0                              // eh_frame_hdr start (unused)
    .word 0                              // eh_frame_hdr end
    .word __nx_module_runtime - __mod0   // rtld's per-module object lives here
