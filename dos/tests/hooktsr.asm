; hooktsr.asm - a TSR that owns INT 60h (a user vector nothing calls).
; Modern mode must refuse to free it; classic frees it anyway (and the PC
; would hang if anything then called INT 60h).
; nasm -f bin -o HOOKTSR.COM hooktsr.asm
	org 100h
	mov ax, 2560h		; set INT 60h -> handler (DS = CS in a .COM)
	mov dx, handler
	int 21h
	mov dx, (end - $$ + 100h + 15) / 16
	mov ax, 3100h
	int 21h
handler:
	iret
end:
