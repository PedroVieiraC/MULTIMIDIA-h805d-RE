# Como conseguimos acesso root no H805D

> Aparelho próprio, pesquisa autorizada pelo dono. Tudo abaixo é reversível: nunca escrevemos na
> flash sem um backup verificado em mãos primeiro.

## 1. Entrar em FEL (modo de recuperação USB da BootROM)

A BootROM do SoC Allwinner só cai no modo FEL (recuperação via USB) se o boot normal falhar —
apertar o botão de reset não adianta, ele só reinicia o Linux. A saída foi montar um **boot0
mínimo num cartão microSD** cujo único código salta direto para o ponto de entrada da BootROM
(técnica conhecida como `fel-sdboot`, sem "magic" nem watchdog):

```
mrc p15,0,r0,c1,c0,0   ; lê o registrador de controle do sistema (SCTLR)
tst r0,#0x2000         ; testa o bit V (vetores altos/baixos)
moveq lr,#0x20         ; vetores baixos -> entrada da BootROM em 0x00000020
movne lr,#0x0020        ; vetores altos -> ...
movtne lr,#0xFFFF       ; ... 0xFFFF0020
bx lr                   ; salta pra BootROM, que cai em FEL
```

O estratagema reaproveita o cabeçalho `eGON.BT0` de um boot0 genérico que a BootROM deste
aparelho já aceita, troca só o corpo do código e recalcula o checksum. Gravado no cartão (offset
8192 = setor 16) com um script PowerShell que confere o número do disco antes de escrever, para
nunca gravar no disco errado.

Com o cartão inserido e a multimídia ligada apertando reset, a tela fica preta (é o FEL — não
inicializa vídeo) e o Windows enumera um dispositivo USB novo. Depois de instalar o driver WinUSB
nesse dispositivo (com o Zadig), a ferramenta `xfel` confirma o chip e abre leitura/escrita da
flash SPI NOR inteira por USB.

**Tentativas que não funcionaram:** stubs com "magic" gravado na RTC + watchdog, ou com um laço
esperando a RTC devolver o valor gravado — travavam num laço infinito porque a RTC não devolvia o
valor esperado na leitura. A lição foi evitar qualquer laço de espera no stub e preferir o salto
direto (`fel-sdboot`), que é mais confiável.

## 2. Backup verificado antes de qualquer escrita

Com FEL ativo, a flash de 16 MB foi lida **duas vezes** e as duas leituras comparadas por hash
(SHA-256) — só seguimos adiante quando as duas leituras bateram. A partir daí o aparelho é
"imbrickável por software": o modo FEL mora numa ROM que não se apaga, então mesmo que a flash
principal seja corrompida, dá pra regravar o backup e voltar ao estado original.

## 3. Entendendo o firmware

A partição de rootfs é um SquashFS (XZ) com um Linux minimalista estilo OpenWrt. A análise estática
(sem executar nada, só extraindo o squashfs) mostrou que o sistema de fábrica já vinha com
`adbd` habilitado no boot (rodando como root) e com um daemon de telnet presente mas desativado por
padrão — ambos caminhos possíveis para obter um shell, mas nenhum dos dois foi o que usamos no
final.

> **Nota de segurança:** a mesma análise expôs, no arquivo `/etc/shadow` do firmware, o hash da
> senha de root de fábrica. Esse hash não está neste repositório nem em nenhum outro lugar público:
> é um dado do fabricante, válido (em tese) para qualquer unidade do mesmo modelo, então publicá-lo
> ajudaria a comprometer o aparelho de terceiros, não só o nosso. Se você está fazendo essa mesma
> pesquisa no seu próprio aparelho, extraia e analise o seu próprio dump.

## 4. Shell root persistente pela rede

Como o aparelho não tem `telnetd` nem `nc` ativos e o `adbd` não sobe de fato em uso normal (o
USB-C fica em modo host, não device), compilamos nossas próprias ferramentas estáticas
(musl-armhf) dentro de um container Docker com QEMU emulando ARM: um bind-shell minúsculo que abre
uma porta TCP e entrega um shell root, e um busybox completo (396 applets) para ter ferramentas
básicas no aparelho.

Essas ferramentas foram injetadas no SquashFS do rootfs (repack preservando permissões, sempre
dentro de um container Linux — no Windows/NTFS as permissões Unix se perdem) e o `init.d` do
aparelho foi editado para, no final do boot, subir o bind-shell numa porta fixa. A partir daí, com
o aparelho no seu próprio modo de ponto de acesso Wi-Fi, um script Python (`tools/rsh.py` neste
repositório) conecta nessa porta e executa qualquer comando como root.

## 5. Da raiz ao launcher próprio

Com shell root e escrita de flash resolvidos, o próximo passo foi técnico de produto: ao invés de
substituir o aplicativo de fábrica (que concentra toda a lógica de CarPlay/DVR/câmera num binário
monolítico difícil de re-implementar), mantivemos esse aplicativo rodando **por trás** e
construímos uma home própria em LVGL por cima, com um filtro de toque customizado e um fluxo de
boot que liga os dois na ordem certa. É o código em [`launcher/`](../launcher/).

## Ferramentas usadas
- [xfel](https://github.com/xboot/xfel) — leitura/escrita da flash via FEL (USB).
- [Zadig](https://zadig.akeo.ie/) — driver WinUSB para o dispositivo FEL no Windows.
- Docker + QEMU (emulação ARM) — compilar estaticamente para o aparelho e repackar o SquashFS sem
  perder permissões.
