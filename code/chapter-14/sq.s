	.build_version macos, 26, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_sq
	.p2align	2
_sq:
	.cfi_startproc
	fmul	d0, d0, d0
	ret
	.cfi_endproc

	.globl	___pyxc.user_main
	.p2align	2
___pyxc.user_main:
	.cfi_startproc
	stp	x29, x30, [sp, #-16]!
	.cfi_def_cfa_offset 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	fmov	d0, #3.00000000
	bl	_sq
	bl	_printd
	movi	d0, #0000000000000000
	ldp	x29, x30, [sp], #16
	ret
	.cfi_endproc

	.globl	_main
	.p2align	2
_main:
	.cfi_startproc
	stp	x29, x30, [sp, #-16]!
	.cfi_def_cfa_offset 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	bl	___pyxc.user_main
	mov	w0, wzr
	ldp	x29, x30, [sp], #16
	ret
	.cfi_endproc

.subsections_via_symbols
