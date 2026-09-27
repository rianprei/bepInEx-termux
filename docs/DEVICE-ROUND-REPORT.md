# Rodada no device — 2026-09-26/27 (POCO C75, 192.168.0.125:5555)

Worktree: `uni/device-run` @ `9c759dd`. Kit de device: **antigo** (pré-kit-restore)
nos casos F2/F4-1o/F5-crashguard/F9b/F11; **novo** (pós-kit-restore 05b7138) nos
casos F4-re-teste e restore. Manager APK: 368406d, assinado com chave debug.

| Etapa | Esperado | Obtido | Veredito | Kit |
|---|---|---|---|---|
| **PASSO 0: backup** | 7 itens conferidos com sha256 | sa2-data.tar (15.5MB, 268 entradas), local-mods.tar (58.5MB), bc-poc-module.tar (342KB), magisk-modules-list+props (6 módulos), getenforce=Permissive, local-mods-sha256 (10 arquivos), BACKUP-MANIFEST.sha256 7/7 | **PASS** | — |
| **Install: release** | release reproduzível (BUILD-INFO) | zip+APK+3 .so; chave do usuário pediu senha (apksigner sem TTY falhou) — fallback autorizado: assinado com chave debug (cert b830cb51). BUG REAL achado: `resources.arsc` comprimido (INSTALL_PARSE_FAILED -124) — fix commit d682d9b | **PASS** (com nota) | — |
| **Install: módulo** | `magisk --install-module` + reboot + `módulo carregado — v0.4.1` no logcat | zip instalado, reboot ~70s, `BCPOC: módulo carregado — v0.4.1` no logcat, mods carregam (`[01_sa2ammo] carregado`) | **PASS** | — |
| **Repair: log.txt ownership** | — | log.txt estava root:root 0600 de rodada anterior; o app (u0_a287) não conseguia escrever — chown 10287:10287 | **REPARO** | — |
| **F2: hello (SDK)** | `carregado, esperando libil2cpp.so` + `il2cpp ok` | `22:43:43 [hello] carregado, esperando libil2cpp.so` → `22:43:45 [hello] il2cpp ok`; jogo vivo | **PASS** | antigo |
| **F4: u_patch field (1º)** | `field aplicado` no log, sem crash | **CRASH** (SIGSEGV, fault addr 0x135, backtrace: u_patch.so+0x1bb34 → il2cpp_type_get_name+24). Tombstone: `evidence/tombstone_07_f4field.txt`. Kit restaurou o device (exit 2, diff só no crashguard) | **FAIL** | antigo |
| **F4: u_patch field (re-teste)** | `field aplicado` sem crash | Jogo vivo 120s, `field aplicado em 8 método(s) (17 visto(s))` + `1 regra(s) nova(s) aplicada(s)` — 7 thunks Dobby armados. **[EFEITO NO JOGO NÃO VERIFICADO]** — ninguém jogou para ver se a munição fica infinita. O PASS do kit foi por regex errada (o regex esperava `t1_field\.patch` — o nome ANTIGO da extensão, hoje `.bpatch` — mas o u_patch loga o stem `t1_field`); a prova real é a linha de log `field aplicado em 8 método(s)` lida manualmente, não o regex do kit. | **PASS** (por log; efeito não verificado) | novo |
| **F5: crashguard + Reativar** | t_crash aborta 2x → 3ª abertura sem mods + `mods desativados` + Manager Reativar | 2 mortes em 2s cada, 3ª abertura sem t_crash, `23:27:19 [crashguard] mods desativados: o jogo fechou 2x...`, Manager mostra banner + REATIVAR MODS → marker apagado, crashguard zerado | **PASS** | antigo |
| **F5: install .so/.bmod pelo Manager** | `Instalado:` no dialog | **FAIL** — SAF (DocumentsUI) da MIUI/HyperOS abre "Abrir com" em vez de devolver URI via onActivityResult; rota ACTION_VIEW chega no BmodInstaller sem contexto do jogo; .so vira "zip END header not found". Evidence: `evidence/f5_saf.txt` | **FAIL** (bug real, MIUX SAF) | — |
| **F6: Scanner (Mod Maker)** | `dump.tsv pronto`, busca de classe/método funciona | `dump.tsv pronto! (162805)`, 67 linhas ComplexCreature, 218 WeaponInfo, 4 HasAmmo; u_dump.so removido após scan; UI mostra classes e métodos navegáveis | **PASS** | — |
| **F7: módulo Magisk** | `ls -lZ zygisk/arm64-v8a.so` + module.prop + `módulo carregado` | `bc-poc v0.4.1`, zygisk/arm64-v8a.so presente, Magisk 30.7, logcat `módulo carregado — v0.4.1` | **PASS** | — |
| **F9b: u_noads (sa2content OFF)** | `hook armado` ou `suprimido com fechamento` | `Metica: hook armado em Metica.Ads.MeticaAds.ShowInterstitial` + `MetaAudience: hook armado em AudienceNetwork.InterstitialAd.Show`; AppLovin: classe não encontrada (jogo não usa) | **PASS** | antigo |
| **F9b: sa2content ON** | `carregado` + trocados/Apply | `00:12:05 [02_sa2content] carregado` — ambos ativos sem conflito | **PASS** | antigo |
| **F11: Frida smoke** | `gadget ativo` + frida-smoke.txt | u_frida carregou o gadget (log: `il2cpp ok, carregando gadget`), mas o próprio frida-gadget 17.19.0 crash dentro dele: SIGSEGV fault addr 0x38, backtrace 100% frida-gadget.bin (5 primeiros frames: `#00 pc 0xb36f58 frida-gadget.bin` → `#04 pc 0xb35100 frida-gadget.bin`). frida-smoke.txt ausente. Tombstone: `evidence/tombstone_f11_frida.txt` | **FAIL** (crash no gadget, não no nosso código) | antigo |
| **F11: gadget renomeado recusado** | `parece o frida-gadget (soname ...): recusado` | `00:15:58 [renamed_mod] parece o frida-gadget (soname libfrida-gadget-raw.so): recusado`; jogo vivo | **PASS** | antigo |
| **Enforcing: setenforce 1** | getenforce = Enforcing + reboot sobrevive | setenforce 1 em runtime OK, mas reboot reverte a Permissive (comportamento PADRÃO do Android — o boot cmdline controla). Teste feito em runtime sem reboot | **ESPERADO** | — |
| **Enforcing: F7 módulo** | `módulo carregado` em Enforcing | `BCPOC: módulo carregado — v0.4.1` + mods carregados, 0 avc denied do nosso caminho (app domain) | **PASS** | — |
| **Enforcing: F4 field** | `field aplicado` sem crash, sem avc | `00:32:03 [u_patch] field aplicado em 8 método(s)`, jogo vivo, 0 avc denied | **PASS** | novo |
| **avc: denied (Enforcing)** | coletar com permissive=0 | 0 denials do nosso caminho (bepinex/app domain); só denials de init/vendor (não nosso) | **PASS** (sem execmod necessário) | — |
| **Soak 10 min** | usuário joga 10 min, 0 crash/ANR | Iniciado 00:33:49; **INTERROMPIDO** pelo reboot do Maestro — sem dados de crash/ANR/análise | **INCOMPLETO** | — |
| **Restore final** | 7/7 sha256, getenforce original, residuos 0 | mods 5/5, bc_mods 5/5, bc-poc tar identico, getenforce=Permissive, residuos t1- e round-backup removidos, lock_rotation presente | **PASS** (7/7) | novo |

## Tabela de medição de `su` (item 3)

Contador validado por sabotagem: `su -c sleep 5` → conta 2 durante, volta a 1 depois.

| t | su | load1 | MemAvailable |
|---|---|---|---|
| 0 (pre) | 0 | 20.78 | 3716816 |
| 2 | 1 | 22.08 | 3683940 |
| 4 | 1 | 22.08 | 3718084 |
| 6 | 1 | 22.56 | 3732656 |
| 8 | 1 | 22.56 | 3704620 |
| 10 | 1 | 22.83 | 3688772 |
| 12 | 1 | 22.83 | 3681176 |
| 14 | **2** | 22.37 | 3634884 |
| 16 | 1 | 22.37 | 3560480 |
| 18 | 0 | 23.22 | 3553148 |
| 20 | 0 | 23.22 | 3593592 |

Teto: **pico 2 su simultâneos** (critério ≤3 respeitado). Load 20-23 é ruído ambiente
(o usuário estava instalando TABS em paralelo; não o Manager). MemAvailable estável ~3.7GB.

## Screenshots

Todos os screenshots estavam em `/tmp/opencode/` e foram **perdidos no reboot do PC**
(não recuperável). A evidência de cada etapa é a linha de log/comando citada na tabela.

## Evidência persistente

- `~/Documentos/mods/_backup_device_2026-09-27/evidence/f5_saf.txt` — diagnóstico do SAF MIUI
- `~/Documentos/mods/_backup_device_2026-09-27/evidence/tombstone_07_f4field.txt` — crash do F4 field (1º)
- `~/Documentos/mods/_backup_device_2026-09-27/evidence/tombstone_f11_frida.txt` — crash do frida-gadget F11
- `~/Documentos/mods/_backup_device_2026-09-27/evidence/restore-final-mods.txt` — sha256 final mods
- `~/Documentos/mods/_backup_device_2026-09-27/evidence/restore-final-bc.txt` — sha256 final bc_mods
- `~/Documentos/mods/_backup_device_2026-09-27/evidence/restore-mods-now.txt` — sha256 mods no restore
- `~/Documentos/mods/_backup_device_2026-09-27/evidence/restore-bc-now.txt` — sha256 bc_mods no restore

## Achados (bugs reais, não cosméticos)

1. **F4 type confusion** (FIXED por kilo): `up_field_type_name` passava `Il2CppClass*` para
   `il2cpp_type_get_name` — SIGSEGV em 0x135. Fix commit no u_patch. Re-teste PASS.
2. **F5 SAF MIUI/HyperOS**: `ACTION_OPEN_DOCUMENT` devolve "Abrir com" em vez de URI
   via onActivityResult — o GameDetailActivity nunca recebe o resultado; a rota ACTION_VIEW
   chega no BmodInstaller sem contexto. Pede file-browser próprio ou intent diferente.
   Evidence: `evidence/f5_saf.txt` + `evidence/tombstone_07_f4field.txt`.
3. **Build: resources.arsc comprimido** (FIXED, d6829db): manager/build.sh normalizava com
   DEFLATE; Android 11+ recusa APK com arsc comprimido.
4. **Kit: expect regex** do sa2-field esperava `t1_field\.patch` (extensão antiga; o expect já foi atualizado para `.bpatch` no rename) mas o u_patch loga o stem
   (`t1_field`). Não é bug do kit (o regex é do caso de teste), mas o expect nunca vai bater.
5. **F11 frida-gadget 17.19.0**: carregou mas crash dentro do próprio gadget (não no
   u_frida). Pode ser incompatibilidade com o jogo/Unity 6000.3.13f1.
6. **log.txt ownership**: rodada anterior deixou root:root 0600 — o app não consegue
   escrever. O chown resolve, mas o kit/Manager devia detectar e corrigir.

## Kit antigo vs novo

| Caso | Kit |
|---|---|
| F2 hello, F4-1º, F5-crashguard, F9b, F11 | antigo (pré-05b7138) |
| F4 re-teste (Permissive + Enforcing) | novo (pós-05b7138) |
| restore final | novo |
