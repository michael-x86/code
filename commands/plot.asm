[bits 32]
[org 0x00000000]

_start:
    call .get_base
.get_base:
    pop ebp
    sub ebp,.get_base

    mov eax,37         ; sys_plot
    int 0x80        
    ret

; [ 0xC00B8001 ] ->  Color Attribute 1 Byte

; 8-bit ASCII character
; 0x0C = Black background, Light Red foreground
; 0x0A = Black background, Light Green foreground
; 0x09 = Black background, Light Blue foreground
; 0xDB = Full block character '█'

;8-bit attribute byte:
;High Nibble (Bits 4-7): Background Color 
;Low Nibble  (Bits 0-3): Foreground Color

;Hex   Color Brightness
;0x0   Black    Dark
;0x1   Blue     Dark
;0x2   Green    Dark
;0x3   Cyan     Dark
;0x4   Red      Dark
;0x5   Magenta  Dark
;0x6   Brown    Dark
;0x7   LGray    Dark
;0x8   DGray    Bright
;0x9   LBlue    Bright
;0xA   LGreen   Bright
;0xB   LCyan    Bright
;0xC   LRed     Bright
;0xD   LMagenta Bright
;0xE   Yellow   Bright
;0xF   White    Bright
