; ==============================================================================
; svc.inc - TRSDOS 6.3.1 / LS-DOS 6.3 Supervisor Call (SVC) Equates and Macros
; Target Platform: Olimex Agon Light 2 (Zilog eZ80 in Z80 Mode)
;
; In TRSDOS 7 / LS-DOS on eZ80, user supervisor calls (0-127) are invoked via:
;   LD A, <svc_number>
;   RST 28h
; ==============================================================================

; ------------------------------------------------------------------------------
; Supervisor Call Numbers (0 - 127)
; ------------------------------------------------------------------------------
SVC_IPL         EQU     0       ; System reset / re-IPL
SVC_KEY         EQU     1       ; Wait for keyboard character -> A
SVC_DSP         EQU     2       ; Display character in C to console
SVC_GET         EQU     3       ; Get character from DCB in DE -> A
SVC_PUT         EQU     4       ; Put character in C to DCB in DE
SVC_CTL         EQU     5       ; Device control call
SVC_PRT         EQU     6       ; Print character in C to printer
SVC_WHERE       EQU     7       ; Resolve runtime memory address
SVC_KBD         EQU     8       ; Scan keyboard (immediate / non-blocking) -> A
SVC_KEYIN       EQU     9       ; Line input into buffer in HL (B = max len)
SVC_DSPLY       EQU     10      ; Display ETX (03h) terminated string in HL
SVC_LOGER       EQU     11      ; Log message to system log
SVC_LOGOT       EQU     12      ; Log message to system log and console
SVC_MSG         EQU     13      ; Route message to device
SVC_PRINT       EQU     14      ; Print ETX-terminated string in HL to printer
SVC_VDCTL       EQU     15      ; Video display control (screen configuration)
SVC_PAUSE       EQU     16      ; Pause execution for BC clock ticks
SVC_DATE        EQU     18      ; Get/set system date string
SVC_TIME        EQU     19      ; Get/set system time string
SVC_CHNIO       EQU     20      ; Chain I/O
SVC_ABORT       EQU     21      ; Abort program with error code in A
SVC_EXIT        EQU     22      ; Clean exit back to LS-DOS Ready prompt
SVC_CMNDI       EQU     24      ; Execute command string interpretively
SVC_CMNDR       EQU     25      ; Execute command string directly
SVC_ERROR       EQU     26      ; Display error message for code in A
SVC_DEBUG       EQU     27      ; Enter system debugger
SVC_CKTSK       EQU     28      ; Check status of periodic background task
SVC_ADTSK       EQU     29      ; Add background task to system task scheduler
SVC_CKDRV       EQU     33      ; Check drive status and media readiness
SVC_DODIR       EQU     34      ; Directory display utility
SVC_PARAM       EQU     39      ; Parse command-line parameters
SVC_SLCT        EQU     41      ; Select disk drive in C
SVC_DCINIT      EQU     42      ; Disk controller re-initialization
SVC_DCRES       EQU     43      ; Disk controller hardware reset
SVC_RSTOR       EQU     44      ; Restore drive in C to cylinder 0
SVC_STEPI       EQU     45      ; Step head in one cylinder
SVC_SEEKCYL     EQU     46      ; Seek to cylinder in D on drive in C
SVC_RSLCT       EQU     47      ; Reselect busy drive
SVC_RDHDR       EQU     48      ; Read sector header
SVC_RDSEC       EQU     49      ; Read 256-byte disk sector
SVC_DCSTAT      EQU     50      ; Check disk controller status
SVC_WRSEC       EQU     53      ; Write 256-byte disk sector
SVC_WRSSC       EQU     54      ; Write system sector
SVC_OPEN        EQU     56      ; Open file control block in DE
SVC_CLOSE       EQU     60      ; Close file control block in DE
SVC_BKSP        EQU     61      ; Backspace disk file pointer
SVC_CKEOF       EQU     62      ; Check end-of-file condition
SVC_LOAD        EQU     76      ; Load command module into memory
SVC_RUN         EQU     77      ; Load and execute command module
SVC_GTDCT       EQU     81      ; Get DCT address for logical drive in C -> HL
SVC_GTDCB       EQU     82      ; Get DCB address for device name in DE -> HL
SVC_GTMOD       EQU     83      ; Get module address in memory -> HL
SVC_RDSSC       EQU     85      ; Read system sector
SVC_DIRRD       EQU     87      ; Directory sector read
SVC_MUL8        EQU     90      ; 8-bit multiply: H * E -> HL
SVC_MUL16       EQU     91      ; 16-bit multiply: BC * DE -> DE:HL
SVC_DIV8        EQU     93      ; 8-bit divide: HL / C -> A remainder, L quotient
SVC_DIV16       EQU     94      ; 16-bit divide: DE:HL / BC
SVC_FSPEC       EQU     95      ; Parse filespec string in HL -> FCB in DE
SVC_DECHEX      EQU     96      ; Convert decimal ASCII string to binary
SVC_HEXDEC      EQU     97      ; Convert binary to decimal ASCII string
SVC_FLAGS       EQU     101     ; Query system hardware flags
SVC_BANK        EQU     102     ; Expanded SRAM bank switch (C = bank 0..7)
SVC_BREAK       EQU     103     ; Configure Break key handler
SVC_SOUND       EQU     104     ; Generate sound frequency / duration
SVC_CLS         EQU     105     ; Clear video display screen
SVC_CKBRKC      EQU     106     ; Non-destructively test Break / Ctrl-C latch

; ------------------------------------------------------------------------------
; SVC Invocation Macros
; Note: Modern assemblers (ZDS II, LLVM) reserve '@' for directives/attributes.
; We provide both '_NAME' and 'zNAME' prefixes corresponding to '@NAME' calls.
; ------------------------------------------------------------------------------

; General SVC dispatch macro
SVC             MACRO   num
                ld      a, num
                rst     28h
                ENDMAC

; Display ETX-terminated string (HL -> string)
_DSPLY          MACRO
                ld      a, SVC_DSPLY
                rst     28h
                ENDMAC

zDSPLY          MACRO
                ld      a, SVC_DSPLY
                rst     28h
                ENDMAC

; Wait for keypress (returns ASCII in A)
_KEY            MACRO
                ld      a, SVC_KEY
                rst     28h
                ENDMAC

zKEY            MACRO
                ld      a, SVC_KEY
                rst     28h
                ENDMAC

; Non-blocking keyboard scan (returns ASCII in A, or 0 if none)
_KBD            MACRO
                ld      a, SVC_KBD
                rst     28h
                ENDMAC

zKBD            MACRO
                ld      a, SVC_KBD
                rst     28h
                ENDMAC

; Line input (HL -> buffer, B = max length)
_KEYIN          MACRO
                ld      a, SVC_KEYIN
                rst     28h
                ENDMAC

zKEYIN          MACRO
                ld      a, SVC_KEYIN
                rst     28h
                ENDMAC

; Display single character in C
_DSP            MACRO
                ld      a, SVC_DSP
                rst     28h
                ENDMAC

zDSP            MACRO
                ld      a, SVC_DSP
                rst     28h
                ENDMAC

; Clean exit back to LS-DOS prompt
_EXIT           MACRO
                ld      a, SVC_EXIT
                rst     28h
                ENDMAC

zEXIT           MACRO
                ld      a, SVC_EXIT
                rst     28h
                ENDMAC

; Abort program with error code in A
_ABORT          MACRO
                ld      a, SVC_ABORT
                rst     28h
                ENDMAC

zABORT          MACRO
                ld      a, SVC_ABORT
                rst     28h
                ENDMAC

; Display system error message for code in A
_ERROR          MACRO
                ld      a, SVC_ERROR
                rst     28h
                ENDMAC

zERROR          MACRO
                ld      a, SVC_ERROR
                rst     28h
                ENDMAC

; Clear screen
_CLS            MACRO
                ld      a, SVC_CLS
                rst     28h
                ENDMAC

zCLS            MACRO
                ld      a, SVC_CLS
                rst     28h
                ENDMAC

; Parse command line parameters
_PARAM          MACRO
                ld      a, SVC_PARAM
                rst     28h
                ENDMAC

zPARAM          MACRO
                ld      a, SVC_PARAM
                rst     28h
                ENDMAC

; Parse filespec string (HL -> filespec, DE -> FCB buffer)
_FSPEC          MACRO
                ld      a, SVC_FSPEC
                rst     28h
                ENDMAC

zFSPEC          MACRO
                ld      a, SVC_FSPEC
                rst     28h
                ENDMAC

; Switch expanded memory bank (C = bank 0..7)
_BANK           MACRO
                ld      a, SVC_BANK
                rst     28h
                ENDMAC

zBANK           MACRO
                ld      a, SVC_BANK
                rst     28h
                ENDMAC

; Test Break / Ctrl-C latch (NZ = Break pressed, Z = no Break)
_CKBRKC         MACRO
                ld      a, SVC_CKBRKC
                rst     28h
                ENDMAC

zCKBRKC         MACRO
                ld      a, SVC_CKBRKC
                rst     28h
                ENDMAC
