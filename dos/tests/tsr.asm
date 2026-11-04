; tsr.asm - the smallest TSR: stays resident, hooks nothing. A psdoom-plus
; test target: freeing it (classic or modern) is harmless.
; nasm -f bin -o TSR.COM tsr.asm
	org 100h
	mov dx, (end - $$ + 100h + 15) / 16	; paragraphs: PSP + code
	mov ax, 3100h				; terminate and stay resident
	int 21h
end:
