# Plano — FEL + xfel: caminho seguro para root/firmware sem risco de brick

> Status: estratégia aprovada em raciocínio (2026-11-10). Nenhuma etapa executada.
> Fonte de verdade para o pivô registrado em `progresso.md` (TENTATIVA 16).

## Contexto e por que este caminho

A via de rede→root do artigo do Louis-E (RE de um CarPlay V831) **não se aplica a este
aparelho**: ela depende de um CGI de update não-autenticado na **porta 80**, que neste
modelo está **FECHADA** (só 5037/8082/9999 abrem; reconfirmado em
`evidence/network/sessao_2026-11-10_video_wifi.md`). Além disso, a técnica do Louis-E é,
por definição, uma **escrita na flash interna** — justamente a operação que pode inutilizar
o aparelho — feita **sem backup prévio**.

## Como o método do Louis-E "liberou" o recebimento de dados (mecanismo)

Não foi exploração mágica: foi **confiança indevida do próprio atualizador do aparelho**.
O endpoint CGI de update aceitava firmware pela rede validando apenas três coisas de forma
insuficiente:
- **sem autenticação** (qualquer cliente na rede Wi-Fi envia);
- **sem assinatura criptográfica** (nada prova que o firmware é do fabricante);
- **só um MD5** — que é checksum, não assinatura: quem modifica o firmware **recalcula o MD5**
  e ele continua "válido".

Fluxo dele: pegar firmware legítimo → injetar script CGI que roda como root no boot →
recalcular MD5 → POST no CGI → aparelho reinicia em recovery, **grava** e dá root.
O que "libera os dados" é o aparelho **gravar na flash** um firmware que ele não distingue
do original. Qualquer analógico (ex.: update por USB) termina no mesmo ponto: **gravar a flash**.

## Princípio de segurança (do qual tudo deriva)

> Só se pode arriscar a flash **depois** de ter um backup verificado + um caminho de
> recuperação que funcione **mesmo com a flash zerada**.

Fato de hardware que torna isso viável com risco praticamente nulo:

> O modo **FEL** mora na **mask ROM (BootROM)** do Allwinner. Mesmo apagando a SPI NOR
> inteira, o chip **ainda entra em FEL** e permite regravar tudo. Enquanto FEL for
> alcançável, o aparelho é **praticamente imbrickável por software**.

## Fluxo em portões (cada etapa só começa quando a anterior passou)

```
PORTÃO 0 — Entrar em FEL (NENHUMA escrita na flash)
   Preferir: boot0 mínimo no SD que escreve o magic de FEL (RTC 0x07000108 = 0x5AA5A55A)
             e reinicia. Reusa o SD-boot JÁ provado no Marco 1; roda em SRAM, não depende
             da DRAM, não toca a flash interna.
   Alternativas: `xfel version` com o aparelho ligado no USB-C / curto no pino CLK no power-on.
   ✔ Critério: `xfel version` responde.   ✘ Se não: parar; nada foi arriscado.
        │
        ▼
PORTÃO 1 — BACKUP só-leitura (ainda NENHUMA escrita)
   `xfel ddr` inicializa a RAM; `xfel spinor read` dumpa os 16 MB.
   Fazer DOIS dumps e comparar SHA-256 — só conta se idênticos.
   ✔ Critério: dois dumps iguais, 16 MB completos, estrutura esperada (eGON/SquashFS).
     → A partir daqui existe BACKUP VERIFICADO.
        │
        ▼
PORTÃO 2 — Confirmar recuperação ANTES de precisar dela
   Provar que `xfel spinor write` é o inverso de `read` e que o FEL volta mesmo com a flash
   vazia. A partir daqui: imbrickável.
        │
        ▼
PORTÃO 3 — (só agora) Modificar + gravar, estilo Louis-E
   Adicionar CGI/telnetd root, regravar via `xfel spinor write`.
   Se algo quebrar: `xfel spinor write <backup>` → aparelho restaurado.
```

## Por que é melhor que as alternativas

- **Não é a via de rede do Louis-E** (que grava a flash sem backup → brick fácil). Aqui o
  **backup vem primeiro e é só-leitura**.
- **Não é a briga da DRAM** (SD-boot / Marco 2, travado 4× no init de DRAM do OpenIPC). O FEL
  inicializa a RAM com o código correto da Allwinner, contornando o problema.
- O **único momento de escrita** (Portão 3) ocorre quando o aparelho já é recuperável.

## Incertezas honestas (nenhuma arrisca a flash)

- **Entrar em FEL** ainda não foi provado neste aparelho (Portão 0). Próximo teste — e é
  **não-destrutivo** (reusa o SD-boot que já funciona).
- **USB-C com linhas de dados** — provável, a confirmar. Sem elas, o FEL não enumera e
  paramos sem risco.
- Essas incertezas fazem o plano **parar cedo e seguro**, nunca brickar.

## Enquadramento de autorização

Coberto pela `autorizacoes/H805D-AUTORIZACAO-PESQUISA-SEGURANCA.md` v2.0 (autorização do
proprietário sobre a própria unidade): RE, endpoints sem auth, firmware modificado com CGI
root e testes de integridade. A **escrita na flash interna** permanece condicionada a backup
verificado + recuperação comprovada (CLAUDE.md §6/§7 e condição comum dos docs de governança).
Conflito pendente entre `governance/AUTHORIZATION.md` (permissivo) e a v2.0 (restritivo sobre
flash) — este plano adota a regra mais restritiva (backup antes de qualquer escrita).

## Confirmações da pesquisa (TENTATIVA 17 — 2026-11-10)

- **Entrada em FEL do V831 CONFIRMADA** (fonte autoritativa `xboot/.../mach-v831/command/cmd-fel.c`,
  mesmo autor do xfel): escrever `0x5AA5A55A` em `0x07000108` (RTC base `0x07000000` + `0x108`),
  confirmar por leitura, e dar **reset por watchdog** (base V831 `0x030090a0`) → BootROM cai em FEL.
  Isso valida o Método A; o Método B (pulo direto na BROM) ficou desnecessário.
- **xfel v1.4.0 suporta o V831** (`chips/v831.c`): `version`, `ddr`, `spinor read/write`. Chip-id
  FEL = `0x00181700`. Binário Windows staged em `tools/fel/` (SHA256 `f00c3ab9…23fbc`).
- **Driver Windows:** FEL enumera como `1f3a:efe8` (padrão Allwinner, a confirmar no teste) → pode
  exigir WinUSB via Zadig.
- **Voltar ao normal após FEL:** `xfel write32 0x07000108 0` + `xfel reset` (ou ciclo de energia) —
  a flag RTC pode persistir (domínio always-on).
- Runbook do teste zero (não-destrutivo): `docs/RUNBOOK_FEL_teste_zero.md`.
