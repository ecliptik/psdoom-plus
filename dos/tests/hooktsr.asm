; hooktsr.asm - a TSR that owns INT F1h (unused on PCs without ROM BASIC;
; not 60h-80h, where packet drivers live).
; Modern mode must refuse to free it; classic frees it anyway (and the PC
; would hang if anything then called INT F1h).
; nasm -f bin -o HOOKTSR.COM hooktsr.asm
	org 100h
	mov ax, 25F1h		; set INT F1h -> handler (DS = CS in a .COM)
	mov dx, handler
	int 21h
	mov dx, (end - $$ + 100h + 15) / 16
	mov ax, 3100h
	int 21h
handler:
	iret
end:
