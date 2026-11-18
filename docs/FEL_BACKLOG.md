# Backlog de FEL — mudanças que exigem gravação na flash

> Acumule aqui tudo que precisa de FEL. Quando houver itens que valham a pena,
> rodamos **um lote só** com log (`tools/fel_flash.py`), para saber exatamente
> onde parou se algo falhar. Gerado em 2026-11-11.

## Método de entrada em FEL

### ❌ FEL pela rede (sem SD): TESTADO e NÃO funciona neste kernel (2026-11-11)
Investigação completa. O **mecanismo existe no boot0** (strings `eraly jump fel` /
`carshdump mode , jump fel` no `mtd0`): o boot0 lê o **RTC GPR índice 2** (`0x07090108`)
procurando o magic **`0x5AA5A55A`** e pula pra FEL. Mas **não conseguimos acionar pela rede**:
- `felreboot efex` (reboot(2) RESTART2 "efex") → **boot normal**, não entra em FEL.
- `felreboot fastboot` → **boot normal**, não entra em fastboot.
  → o kernel deste BSP ignora a string de modo do RESTART2 (só reinicia).
- Escrever o RTC direto: **impossível pelo userspace** — `/dev/mem` desabilitado no kernel
  (mknod cria o nó mas abrir dá "No such device"); o GPR do RTC **não** é exposto como nvmem
  (só o SID). Módulo de kernel pra este BSP exato = inviável (sem headers/fonte).
- O kernel escreve modo no GPR índice **6** (`gpr_cur_pos=6`, `0x07090118`), mas o boot0 checa o
  índice **2** → mismatch. Por isso nenhuma string de modo leva a FEL.
- Ferramentas: `felreboot` (fonte `tools/felreboot.c`, binário `artifacts/felreboot`, persistente em
  `/data/felreboot`) faz `reboot(2)` RESTART2 com qualquer string. `fw_setenv` existe (edita env do u-boot).

**Candidato não testado (com risco):** `fw_setenv bootcmd fastboot` → reboot → u-boot roda o comando
`fastboot` (o env tem `boot_fastboot=fastboot`). Risco: se o driver fastboot do Windows não pegar, o
aparelho fica **preso em fastboot** (bootcmd persistente) e só se recupera via **SD-FEL**. Só vale com
Zadig/driver fastboot preparado e disposição pra 1 recuperação por SD.

### ✅ Método confiável: microSD (comprovado)
Stub FEL no setor 16 do SD (`tools/write_sd.ps1`), inserir, ligar segurando RESET.
Ver `docs/REPRODUZIR_ACESSO_COMPLETO.md`.

### 💡 O ponto que importa: a MAIORIA das atualizações NÃO precisa de FEL
Temos **root pela rede** (bind-shell) e `/data` **gravável**. Então features, scripts, binários,
configs e ajustes vão pra `/data` e rodam pela rede — **sem SD, sem FEL** (foi assim que o Wi-Fi inteiro
foi feito). FEL/SD só é necessário para mexer no que é **read-only**: o rootfs (squashfs), o kernel ou o
u-boot — o que é raro. Para tornar QUALQUER feature futura 100% pela rede, a próxima (e última) gravação
por SD pode instalar um delegador genérico no `rc.final` (`[ -f /data/boot.sh ] && sh /data/boot.sh &`).

## Passos físicos que permanecem por sessão (mínimos)
1. Pôr a multimídia em **modo Wi-Fi** (sobe o AP F900 → PC conecta → eu tenho shell).
2. Conectar o **cabo USB-C** PC↔aparelho (para o xfel no FEL).
Depois disso, o resto é pela rede.

---

## Itens acumulados para o próximo lote de FEL
> Status: **candidatos, sem urgência**. O **delegador genérico já foi gravado** (2026-11-11) → features futuras
> são só editar `/data/boot.sh` **pela rede**, sem FEL.

| # | Item | Arquivo/offset | Motivo | Status |
|---|------|----------------|--------|--------|
| ✅ | Delegador genérico no rc.final | `artifacts/rootfs_mod_generic.squashfs` @ `0x2E0000` | features futuras pela rede | **GRAVADO 2026-11-11** |
| 1 (opcional) | Desligar UHS do SD no device tree (`no-1-8-v` / sem `sd-uhs-*` no nó `sdmmc@4020000`) | DTB na partição `boot` (`0xA0000`) — localizar e extrair antes | SD some após `reboot` por software (cartão preso em 1,8 V, slot sem corte de energia). Boot frio e RESET já funcionam — só vale se reboots por software virarem rotina. Ver `docs/launcher/ETAPAS.md` | candidato |
| 2 (candidato) | `h805d-ui` + `touchfix.so` dentro do squashfs (rootfs); `boot.sh` continua preferindo a cópia em `/data` | rootfs `0x2E0000` (~436 KB de sobra na partição) | liberar o `/data` (512 KB, pouco espaço; medir com `df -k /data`); gravar quando a home estabilizar ou o livre cair abaixo de ~40 KB | candidato |
| 3 (candidato) | SSH pequeno (dropbear estático musl, recompilado por nós) no lugar do bindshell 2323 sem senha | rootfs / `/data` | segurança do acesso | candidato |
| 4 (candidato) | Porta 5037 (adbd) escutando em 0.0.0.0: desligar ou prender ao USB | init/rootfs | exposição na rede (não testado se entrega shell) | candidato |

### Como adicionar uma feature de boot AGORA (sem FEL)
Edite `/data/boot.sh` no aparelho (pela rede, via bind-shell). Ele roda no boot pelo delegador.
Exemplo atual: sobe o watchdog de Wi-Fi só se `/data/wifi.conf` existir.

### Candidato antigo (obsoleto)
- `artifacts/rootfs_mod_wd.squashfs` — hook específico de Wi-Fi. **Superado** pelo delegador genérico.

## Estado atual da flash (para recuperação)
- Gravado em `0x2E0000`: `artifacts/rootfs_mod_generic.squashfs` (rootfs + bind-shell + delegador `/data/boot.sh`). **Conferido em 2026-11-13** pelo md5 dos primeiros 12.660.736 bytes de `/dev/mtdblock3` (`95484d11…`), igual ao arquivo. As imagens `wifi`, `wd` e `full2` estão superadas.
- Restore total anti-brick: `artifacts/firmware_backup/firmware_original_16MB.bin` (16 MB de fábrica).
- Backup pré-Wi-Fi (estado com bind-shell, sem hook): dump de 16 MB feito em 2026-11-11 (scratchpad `predump_before_wifi.bin`).

## Como rodar o lote (quando houver itens)
1. Preencher `tools/fel_plan.txt` com linhas `  <offset_hex> <arquivo> ` (ver exemplo no arquivo).
2. Entrar em FEL (só por microSD; ver acima).
3. `python tools/fel_flash.py` → faz backup, grava cada item, **verifica por hash** e salva log em `logs/fel-AAAAMMDD-HHMMSS.log`.
4. Reboot normal e validar.
