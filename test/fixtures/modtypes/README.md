# Corpus de tipos (ModTypeMatrixTest, Contrato C7)

Arquivos PEQUENOS e DETERMINISTICOS, gerados por `generate.py` (python3 puro,
sem rede, sem random, sem timestamp). O teste `ModTypeMatrixTest` roda cada
arquivo pelo `LooseModInstaller.probe()` + `ModContentDetector.detect()` e
exige Kind + trecho da explicação PT-BR (simples, acentuada, sem jargão nem
código de roadmap — a varredura de textos reprova o contrário). Regenerar:
`python3 generate.py` (tem que produzir byte a byte o mesmo corpus).

| arquivo | Kind | instala | por que |
|---|---|---|---|
| mod_arm64.so | ELF_ARM64 | sim | ELF64 arm64 ET_DYN coerente |
| mod_arm32.so | ELF_OTHER_ARCH | nao | arm de 32 bits nao roda no loader arm64 |
| mod_x86_64.so | ELF_OTHER_ARCH | nao | x86-64 nao roda em Android arm64 |
| frida-gadget-raw.so | FRIDA_GADGET | nao | gadget (SONAME+marca): vira frida-gadget.bin, nunca .so |
| mod_pcinho.dll | DOTNET_MONO | nao | assembly .NET de PC (F13, nao implementado) |
| mod_il2cpp.dll | DOTNET_IL2CPP | nao | assembly .NET IL2CPP (F12, nao implementado) |
| dll_nativo.dll | PE_NATIVE | nao | PE de Windows sem runtime .NET |
| hackeador.exe | PE_NATIVE | nao | executavel de Windows |
| regras_boas.bpatch | PATCH | sim | regras C4 validas |
| regras_quebradas.bpatch | TEXT_OTHER | nao | nenhuma linha da gramatica C4 |
| regras_ext_antiga.patch | PATCH | sim | regras C4 validas com a extensao ANTIGA: o detector decide pelo CONTEUDO e instala como `<id>.bpatch` |
| regras_sem_extensao | PATCH | sim | idem, sem extensao nenhuma |
| script_frida.js | FRIDA_JS | sim | script Frida (roda com gadget na pasta) |
| script_gg.lua | LUA_GG | nao | GameGuardian (F10, nao implementado) |
| pacote_ok.bmod | BMOD | sim | zip com manifest.json do formato C2 |
| zip_slip.bmod | BMOD (detecta) | instalacao recusada | entrada `../evil.so`: zip-slip, BmodInstaller barra |
| bepinex_pc.zip | BEPINEX_PC | nao | mod da versão de PC (layout BepInEx): o .dll de dentro não roda no celular; em breve, conversão de mods simples |
| jogo.apk | ZIP_GAME_CONTAINER | nao | pacote do jogo: nunca se modifica |
| expansao.obb | ZIP_GAME_CONTAINER | nao | expansao do jogo: nunca se modifica |
| pacote.xapk | ZIP_GAME_CONTAINER | nao | XAPK (manifest.json la dentro NAO e o C2) |
| dados.pak | GAME_DATA | nao | pacote de dados do Unreal |
| asset.bundle | GAME_DATA | nao | asset Unity (magic UnityFS) |
| save_do_jogo.json | SAVE_GAME | nao | save do jogador: nao e mod |
| leia_me.txt | TEXT_OTHER | nao | texto sem regra C4 nem script |
| vazio_sem_extensao | BINARY_UNKNOWN | nao | arquivo vazio: extensao nenhuma inventa tipo |
| so_zero.so | BINARY_UNKNOWN | nao | 0 byte com nome de .so: download que falhou |
| imagem_falsa.png | ELF_ARM64 | sim | MENTIROSO: ELF arm64 com nome .png (conteudo manda) |
| compactado_mentiroso.so | ZIP_PLAIN | nao | MENTIROSO: zip com nome .so (conteudo manda) |
