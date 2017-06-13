; Really simple & stupid PICA200 shader
; Also serves as an example of picasso syntax

; Uniforms
.fvec projMtx[4], mdlvMtx[4]

; Constants
.constf myconst(0.0, 1.0, -1.0, 0.0)
.alias zeros myconst.xxxx
.alias ones myconst.yyyy
.alias negones myconst.zzzz
.alias dummytcoord myconst.xxxy ; (0,0,0,1)

; Outputs
.out outpos position
.out outtc0 texcoord0
.out outtc1 texcoord1
.out outtc2 texcoord2
.out outclr color

; Inputs
.alias inpos v0
.alias intex v1
.alias inarg v2

.proc main
	; r0 = (inpos.x, inpos.y, inpos.z, 1.0)
	mov r0.xyz, inpos
	mov r0.w, ones
	
	; r1 = mdlvMtx * r0
	dp4 r1.x, mdlvMtx[0], r0
	dp4 r1.y, mdlvMtx[1], r0
	dp4 r1.z, mdlvMtx[2], r0
	dp4 r1.w, mdlvMtx[3], r0
	
	; outpos = projMtx * r1
	dp4 outpos.x, projMtx[0], r1
	dp4 outpos.y, projMtx[1], r1
	dp4 outpos.z, projMtx[2], r1
	dp4 outpos.w, projMtx[3], r1
	
	; Set texcoords
	mov outtc0, intex
	mov outtc1, dummytcoord
	mov outtc2, dummytcoord
	
	; Set vertex color
	mov outclr.xyz, inarg
	mov outclr.w, ones

	; Random raw encoded nop
	.opdesc nop_opdesc 0x00000000 0x00FFFF00 ; Adds an opdesc with value 0 and mask 0x00FFFF00 (last argument is optional)
	.opdesc nop_opdesc2 0x12345678 ; Adds an opdesc with value 0 and mask 0xFFFFFFFF
	.inst 0x84000000 nop_opdesc; nop but lower bits will become index of opdesc
	.inst 0x84000000 ; nop with opdesc 0
	
	end
.end
