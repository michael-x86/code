; =============================================================================
; sound.inc — PC Speaker sound helper for x86 OS userland programs
; =============================================================================
; Provides wrappers for the kernel's sys_sound syscall (48):
;   eax = 48, ebx = frequency (Hz), ecx = duration (10ms ticks)
; =============================================================================

%ifndef SOUND_INC
%define SOUND_INC

%ifndef SYS_SOUND
%define SYS_SOUND 48
%endif

; sound_beep — play a tone via the PC speaker
; Input:  ebx = frequency in Hz (0 = silence)
;         ecx = duration in 10ms ticks (e.g., 10 = 100ms)
; Clobbers: eax
sound_beep:
    mov eax, SYS_SOUND
    int 0x80
    ret

; sound_silence — stop the speaker for a given duration
; Input:  ecx = duration in 10ms ticks
; Clobbers: eax, ebx
sound_silence:
    xor ebx, ebx        ; frequency 0 = silence
    mov eax, SYS_SOUND
    int 0x80
    ret

%endif
