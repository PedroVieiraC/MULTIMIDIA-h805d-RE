# H805D — engenharia reversa de uma multimídia automotiva

Multimídia automotiva CarPlay/DVR genérica (marcas ESSGOO/GOODOCOM, modelo interno H805D/F900),
baseada em SoC **Allwinner V851/V853** (ARM Cortex-A7, 256 MB RAM, Linux 4.9 + musl, flash SPI NOR
16 MB). Não roda Android: a interface é nativa em **LVGL**, com CarPlay/AndroidAuto via uma stack
proprietária do fabricante.

Este repositório documenta a engenharia reversa do aparelho (feita em parceria com o Claude Code)
e guarda o código final que escrevemos a partir disso: um launcher próprio em LVGL/C e as
ferramentas Python usadas para operar o aparelho pela rede. **O passo a passo foi reproduzido numa
unidade própria, autorizada pelo dono, e é totalmente reversível** (sempre com backup verificado da
flash antes de qualquer escrita).

## O que tem aqui
- [`docs/COMO_CONSEGUIMOS_ROOT.md`](docs/COMO_CONSEGUIMOS_ROOT.md) — a jornada: do modo de
  recuperação USB (FEL) da BootROM até o shell root persistente pela rede.
- [`docs/SISTEMA_NATIVO.md`](docs/SISTEMA_NATIVO.md), [`docs/FEL_BACKLOG.md`](docs/FEL_BACKLOG.md),
  [`docs/PLAN_FEL_backup_seguro.md`](docs/PLAN_FEL_backup_seguro.md) — notas técnicas sobre a
  arquitetura do sistema original e o plano de backup/gravação por FEL.
- [`launcher/`](launcher/) — launcher próprio em C (LVGL) que substitui a home original, mantendo o
  app de fábrica vivo por trás como motor de CarPlay/DVR/câmera.
- [`tools/`](tools/) — scripts Python para shell root, transferência de arquivos e controle do Wi-Fi
  pela rede.

## Por que
Curiosidade e estudo: entender um aparelho fechado, sem documentação pública, até o ponto de poder
auditá-lo e customizá-lo com segurança. Um vídeo mostrando o resultado final rodando no carro
complementa este repositório.

## Aviso
Os IPs e a senha do ponto de acesso Wi-Fi do próprio aparelho mudam pouco entre unidades do mesmo
modelo, mas os exemplos aqui usam placeholders; ajuste para o seu aparelho. Nenhum dado de rede
doméstica, chave de licenciamento do aparelho ou hash de senha extraído do firmware está neste
repositório — ver nota de segurança no fim do doc de root.
