#!/system/bin/sh
# Instalador do módulo (customize.sh).
#
# O Magisk/KernelSU NÃO executa este script: ele é *sourced* pelo
# module_installer.sh DEPOIS de extrair tudo pra $MODPATH, com as permissões e
# o secontext padrão já aplicados. Por isso:
#   - $MODPATH é o diretório do módulo (/data/adb/modules/<id>), não $0/$MODDIR;
#   - NÃO chamar exit no final (a doc oficial manda não, porque o instalador
#     ainda tem cleanup a fazer depois).
# Doc: https://topjohnwu.github.io/Magisk/guides.html (Customization) e
# https://kernelsu.org/guide/module.html (KernelSU, mesmas variáveis).

# --- Manager (F5), opcional -----------------------------------------------------
# Se o build não tinha manager/out/*.apk, o zip não traz o APK. Quando
# traz, instala agora e apaga do diretório do módulo (senão sobra um .so/apk
# Strange dentro de /data/adb/modules).
if [ -f "$MODPATH/manager.apk" ]; then
    ui_print "Instalando o bepInEx Manager..."
    # Copia pro TMPDIR antes: pm precisa abrir o arquivo, e o diretório do
    # módulo não é o lugar mais confortável pra isso.
    if cp -f "$MODPATH/manager.apk" "$TMPDIR/bepinex-manager.apk" &&
        pm install -r "$TMPDIR/bepinex-manager.apk"; then
        ui_print "bepInEx Manager instalado."
        # Só apaga o apk depois de instalado: no caminho de falha ele é o
        # caminho de retry do usuário, apagá-lo ali jogaria fora a única
        # cópia do Manager.
        rm -f "$MODPATH/manager.apk"
    else
        ui_print "AVISO: nao consegui instalar o Manager automaticamente."
        ui_print "O apk continua em $MODPATH/manager.apk. Instale depois com:"
        ui_print "  pm install -r $TMPDIR/bepinex-manager.apk"
        ui_print "(ou pelo app Magisk, segurando o arquivo na pasta do modulo)"
    fi
else
    ui_print "Zip sem manager.apk — so o loader (Manager ainda nao mergeado)."
fi

# --- Pastas do contrato C1 ------------------------------------------------------
# O post-fs-data.sh tambem cria estas no boot (e aplica o chcon do tipo novo,
# que so existe DEPOIS que a politica do modulo foi aplicada). Aqui e so para
# o usuario ja ter a pasta assim que termina a instalacao.
# Raiz root-only: /data/adb e 0700 root:root, entao so o root escreve aqui.
# A arvore antiga (/data/local/tmp) e migrada uma vez pelo post-fs-data.
mkdir -p /data/adb/bepinex/mods /data/adb/bepinex/bc_mods
chmod 755 /data/adb/bepinex/mods /data/adb/bepinex/bc_mods

# --- Permissoes de execucao: o descompactador NAO preserva o modo ---------------
#
# ACHADO (rodada 3 no POCO C75, 2026-09-29, base c1513a5): o zip carrega 0755 em
# todo executavel (create_system=3, unix_mode=0o755) e o git mode e 100755, mas
# depois de `magisk --install-module` TODO arquivo volta como -rw-r--r--. O
# Magisk extrai o zip e o manager nao repõe o bit +x. Consequencia medida no
# aparelho: post-fs-data.sh nunca rodou (sem exec), o migrador nunca rodou, a
# arvore /data/adb/bepinex ficou sem o contexto bepinex_mod_file e a migracao do
# caminho velho nao aconteceu. A versao v0.4.1 nao tinha set_perm tambem — ela
# "funcionava" por sorte, nao por garantia.
#
# O gamepad sintoma mais visivel: o companion executa
# /data/adb/modules/bc-poc/termux-console/bepin-console (BC_CONSOLE_PATH em
# jni/companion.cpp) e o Termux:API recusou com
#   FileUtils Error (150): The executable regular file not found at path ...
#   /data/adb/modules/bc-poc/termux-console/bepin-console
# porque o arquivo existia mas nao era executavel. bepin-console NAO tem extensao
# .sh, entao um `chmod 755 *.sh` manual nao o alcanca.
#
# A lista abaixo e explicita, nao um `find`/`chmod -R`: chmod -R em /data/adb
# atravessa a arvore de mods do usuario e mudaria coisa que nao e do modulo. Cada
# item esta aqui porque o build o empacota (ver tools/build_module.sh) E o git o
# marca 100755. Um executavel novo entra aqui no mesmo commit que o cria, e o
# gate (test/module_perm_check.sh) falha se um 100755 do repo nao estiver na
# lista nem tiver chmod correspondente.
set_perm 0755 "$MODPATH/post-fs-data.sh"
set_perm 0755 "$MODPATH/action.sh"
set_perm 0755 "$MODPATH/uninstall.sh"
set_perm 0755 "$MODPATH/customize.sh"
# O migrador e sourced pelo post-fs-data (ver a linha ". ... migrate-mods-tree.sh"),
# entao precisa ser legivel; 0755 para ficar executavel tambem quando o proprio
# post-fs-data rodar como script.
set_perm 0755 "$MODPATH/migrate-mods-tree.sh"
# O console do Termux e executado pelo companion via Termux:API RunCommand, que
# exige o bit +x E o shebang. Sem extensao .sh — e exatamente o arquivo que o
# hotfix manual precisou corrigir no aparelho.
set_perm 0755 "$MODPATH/termux-console/bepin-console"
# O cliente NAO e executado: o Termux o invoca como `python3 <caminho>`, e o
# Termux:API exige um executavel — um .py com 0755 sem shebang seria recusado.
# Por isso 0644: e dado, e o modo certo deixa isso explicito.
set_perm 0644 "$MODPATH/termux-console/termux_client.py"
# O update-binary e o instalador do Magisk; o Magisk ja o executa, mas o modo
# correto evita o mesmo defeito se o manager mudar de rumo.
set_perm 0755 "$MODPATH/META-INF/com/google/android/update-binary"
# sepolicy.rule e dados, nunca executavel.
set_perm 0644 "$MODPATH/sepolicy.rule"

ui_print "Reinicie o celular para o Zygisk carregar o modulo."

