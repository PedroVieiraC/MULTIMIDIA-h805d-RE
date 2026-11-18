#!/usr/bin/env python3
# Constrói um boot0 mínimo para V831 (sun8iw19) cuja única função é ENTRAR EM FEL:
# escreve o magic 0x5AA5A55A no RTC 0x07000108 e dispara o watchdog (0x030090a0).
# A BootROM religa e, vendo o magic, cai em FEL. NÃO toca DRAM nem flash interna.
#
# Método confirmado em xboot/.../mach-v831/command/cmd-fel.c (RTC magic) e
# xfel chips/v831.c chip_reset (watchdog base 0x030090a0). Ver docs/PLAN_FEL_backup_seguro.md.
#
# Reusa o header eGON.BT0 de um boot0 que a BootROM DESTE aparelho já aceitou (Marco 1),
# trocando apenas: length (0x10), checksum (0x0C) e o corpo de código (a partir de 0x60).
#
# Uso:  python -I build_fel_stub.py <header_src.bin> <saida.sunxi>
import sys, struct

HEADER_SRC = sys.argv[1] if len(sys.argv) > 1 else "sdboot/downloads/u-boot-v831-mmc.bin"
OUT        = sys.argv[2] if len(sys.argv) > 2 else "sdboot/downloads/fel-stub-v831.sunxi"
METHOD     = sys.argv[3] if len(sys.argv) > 3 else "wdog"   # wdog | brom | both

STAMP = 0x5F0A6C39      # valor-semente do checksum eGON
CODE_OFF = 0x60         # branch do header salta para cá
IMG_LEN  = 0x2000       # 8 KiB, alinhado; BootROM lê e faz checksum de 'length' bytes

# ---- blocos de código ARM (little-endian), verificados instrução a instrução ----
# (A) magic no RTC: r0=0x07000108 <- r1=0x5AA5A55A (grava e confirma por leitura)
MAGIC = [
    0xE3000108,  # movw r0,#0x0108
    0xE3400700,  # movt r0,#0x0700      ; r0 = 0x07000108 (RTC flag)
    0xE30A155A,  # movw r1,#0xA55A
    0xE3451AA5,  # movt r1,#0x5AA5      ; r1 = 0x5AA5A55A (magic FEL)
    0xE5801000,  # wr: str r1,[r0]
    0xE5904000,  #     ldr r4,[r0]
    0xE1540001,  #     cmp r4,r1
    0x1AFFFFFB,  #     bne wr
]
# (B) reset por watchdog (0x030090a0) — igual ao xfel chip_reset do V831
WDOG = [
    0xE30920A0,  # movw r2,#0x90A0
    0xE3402300,  # movt r2,#0x0300      ; r2 = 0x030090a0
    0xE5923018,  # ldr  r3,[r2,#0x18]
    0xE3C330F0,  # bic  r3,r3,#0xF0
    0xE3833011,  # orr  r3,r3,#0x11
    0xE5823018,  # str  r3,[r2,#0x18]
    0xE30134AF,  # movw r3,#0x14AF
    0xE5823010,  # str  r3,[r2,#0x10]   ; dispara reset
    0xEAFFFFFE,  # hang: b hang
]
# (C) salto direto p/ entrada da BootROM = método fel-sdboot do sunxi-tools (entra em FEL)
BROM = [
    0xEE110F10,  # mrc p15,0,r0,c1,c0,0  ; lê SCTLR
    0xE3100C20,  # tst r0,#0x2000        ; bit V (high/low vectors)
    0x03A0E020,  # moveq lr,#0x20        ; low  -> BROM entry 0x20
    0x1300E020,  # movne lr,#0x0020
    0x134FEFFF,  # movtne lr,#0xFFFF     ; high -> 0xFFFF0020
    0xE12FFF1E,  # bx lr                 ; re-entra na BootROM -> FEL
]
CODE = {"wdog": MAGIC + WDOG, "brom": BROM, "both": MAGIC + BROM}[METHOD]

with open(HEADER_SRC, "rb") as f:
    header = bytearray(f.read(CODE_OFF))
assert header[4:12] == b"eGON.BT0", "header de origem não é eGON.BT0"

img = bytearray(IMG_LEN)
img[:CODE_OFF] = header
for i, w in enumerate(CODE):
    struct.pack_into("<I", img, CODE_OFF + 4*i, w)

# branch em 0x00 deve apontar para 0x60 (preservado do header de origem): b (0x60-8)/4 = 0x16
assert struct.unpack_from("<I", img, 0)[0] == 0xEA000016, "branch do header não aponta p/ 0x60"

struct.pack_into("<I", img, 0x10, IMG_LEN)          # length
struct.pack_into("<I", img, 0x0C, STAMP)            # semente antes de somar
s = sum(struct.unpack_from("<I", img, i)[0] for i in range(0, IMG_LEN, 4)) & 0xFFFFFFFF
struct.pack_into("<I", img, 0x0C, s)                # checksum final

with open(OUT, "wb") as f:
    f.write(img)
print(f"OK: {OUT}  length=0x{IMG_LEN:x}  checksum=0x{s:08x}  code_words={len(CODE)}")
