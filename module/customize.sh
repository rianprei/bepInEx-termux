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
mkdir -p /data/local/tmp/mods /data/local/tmp/bc_mods
chmod 755 /data/local/tmp/mods /data/local/tmp/bc_mods

ui_print "Reinicie o celular para o Zygisk carregar o modulo."
