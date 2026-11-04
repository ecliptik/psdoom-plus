; chaintsr.asm - a TSR that chains INT F1h: it keeps the previous handler
; (HOOKTSR's, in the tests) and jumps there. Once it is loaded the IVT points
; only at CHAINTSR, and HOOKTSR is reachable only through CHAINTSR's saved
; pointer: modern mode must still protect both (the Gateway 2000 showed
; UNIVBE, DOSKEY and MSCDEX in exactly that position).
; nasm -f bin -o CHAINTSR.COM chaintsr.asm
	org 100h
	jmp install
old:	dd 0
handler:
	jmp far [cs:old]
install:
	mov ax, 35F1h		; ES:BX = current INT F1h
	int 21h
	mov [old], bx
	mov [old+2], es
	mov ax, 25F1h		; INT F1h -> handler
	mov dx, handler
	int 21h
	mov dx, (install - $$ + 100h + 15) / 16
	mov ax, 3100h
	int 21h
