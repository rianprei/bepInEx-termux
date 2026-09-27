# Formato dos mods (`.bmod`, `.conf`, `.patch`, `dump.tsv`)

Pra quem **faz** mod. Pra instalar mods como usuário, o README do repo
basta; aqui é o contrato dos arquivos (C2–C5 e C7 do
[ROADMAP-UNIVERSAL.md](ROADMAP-UNIVERSAL.md)) do ponto de vista de quem
empacota e distribui.

Estado honesto dos motores (2026-09-27): mods **nativos** (`.so`) rodam hoje
e estão validados no device (Swamp Attack 2). O `u_patch` (motor das regras
`.patch`, base do Mod Maker) já está mergeado (F4); o `u_dump`
(scanner que gera o `dump.tsv`) já está mergeado (F3). O Manager (app) já
existe e instala `.bmod`/`.so`/`.patch`/`.js` — a instalação pelo Manager
no celular foi testada (Xiaomi) mas o seletor não devolveu o arquivo
(SAF MIUI); a opção **Escolher da pasta Download** foi criada por isso
mas ainda não foi testada no celular (experimental). O
`tools/deploy_mod.sh` continua como caminho alternativo.

## Onde cada coisa vive no device (C1)

```
/data/local/tmp/mods/<pacote-do-jogo>/
    <id>.so        mod nativo (SDK C++)          ← carregado pelo loader
    <id>.patch     regras declarativas (u_patch)
    <id>.conf      opções key=value (gerado do manifest)
    <id>.json      manifest instalado (o Manager lista a partir dele)
    u_patch.so     motor de regras (copiado 1x por jogo quando precisa)
    u_dump.so      scanner (temporário; o Mod Maker copia e remove)
/data/data/<pacote-do-jogo>/files/bepinex/
    log.txt        log de todos os mods ("HH:MM:SS [mod] mensagem")
    dump.tsv       saída do u_dump
```

- Pasta de mods é do root (`755`), arquivos `644`. Quem instala é o Manager
  (via su) ou adb — o jogo só lê.
- Mod desligado = sufixo `.off` (`foo.so.off`): o loader só carrega `*.so`.
- Copia manual depois do boot precisa do rótulo SELinux:
  `chcon u:object_r:bepinex_mod_file:s0 <arquivo>` (o `deploy_mod.sh`
  já faz; sem isso, em SELinux Enforcing o jogo não lê o arquivo).

## `.bmod` (C2) — o pacote distribuível

Zip com dois arquivos:

```
manifest.json
mod.so        (type=native)   |   mod.patch   (type=patch)
```

`manifest.json` completo, com opção:

```json
{
  "format": 1,
  "id": "sa2-infinite-ammo",
  "name": "Munição infinita",
  "version": "1.0",
  "author": "fulano",
  "description": "Nunca acaba a munição.",
  "game": "com.hyperdotstudios.swampattack2",
  "engine": "unity-il2cpp",
  "type": "patch",
  "options": [
    {"key": "mult", "label": "Multiplicador", "type": "float", "default": 2, "min": 1, "max": 10}
  ]
}
```

Regras (o `tools/pack_bmod.sh` valida na hora de empacotar):

- `format`: sempre `1`.
- `id`: `[a-z0-9-]{3,48}` — vira nome de arquivo no device. Minúsculo,
  sem acento, sem espaço.
- `game`: pacote completo (`com.hyperdotstudios.swampattack2`) ou `"*"`
  (qualquer jogo do mesmo engine).
- `engine`: `unity-il2cpp` | `unity-mono` | `cocos2dx` | `native`.
- `type`: `native` (o zip tem `mod.so`, arm64-v8a) ou `patch`
  (o zip tem `mod.patch`).
- `options` (opcional): lista de chaves que o Manager vira sliders/switches.
  `type`: `bool` | `int` | `float` | `choice` (com `"choices": [..]`).
  `min`/`max` opcionais pra int/float.

Na instalação: `mod.so` → `<id>.so`, `mod.patch` → `<id>.patch`, manifest →
`<id>.json`, valores default das options → `<id>.conf`.

## `.conf` (C3) — opções

Uma linha por chave, `key=value`, `#` é comentário. Escrito pelo Manager
(quando existir), lido pelo mod no boot:

```
# multiplicador de dano
mult=2.5
godmode=false
```

API de leitura no mod nativo (`mods/common/mod_common.h`):
`mod_conf_get(id, key, default)` pra string e `mod_conf_bool/int/float`
pra tipos. Valor ausente ou que não parseia vira o default — regra ruim
não derruba mod.

## `.patch` (C4) — regras declarativas (u_patch)

> **MOTOR JÁ MERGEADO (F4):** o `u_patch` já mergeou — esta seção
> documenta o **contrato fixo** do roadmap (C4), mas nada aqui é operacional
> hoje. Não empacote nem distribua `.patch`/`type:patch` até o merge; o
> passo a passo de instalação entra aqui quando isso acontecer.

Uma regra por linha, campos separados por espaço, `#` é comentário.
`<Classe>` = `Namespace.Nome` ou só `Nome` (o último `.` separa).
`<valor>`/`<fator>` pode ser `$key`, que vem do `<id>.conf`.

```
return  <Classe>  <Método>  <nargs>  <bool|int|float>  <valor>
mul     <Classe>  <Método>  <nargs>  <int|float>       <fator>
static  <Classe>  <campo>   <bool|int|float>           <valor>
field   <Classe>  <campo>   <bool|int|float>           <valor>  [<Método> <nargs>]
```

- `return`: método sempre devolve `<valor>` (patch de instrução ARM64).
- `mul`: o retorno do método é multiplicado por `<fator>` (hook com thunk).
- `static`: campo **estático** fixado em `<valor>`, reaplicado a cada 2s.
- `field`: campo de **instância** — a cada chamada de `<Método>` (da mesma
  classe), escreve `this.<campo> = <valor>` antes de rodar o original.
  Sem `<Método>`/`<nargs>`, o u_patch escolhe sozinho até 8 métodos de
  instância da classe que passam na guarda de tamanho.

### A lição de device que criou o verbo `field` (Swamp Attack 2)

Quem escreveu o contrato mediu no device antes de escolher o verbo. A regra
que parece a certa:

```
return ComplexCreature HasAmmo 0 bool true
```

**aplicou, rodou e foi chamada** (12x numa sessão, medido no device com
Frida) — e a munição acabou do mesmo jeito. Motivo: o jogo **lê e
decrementa o campo da arma direto**, sem passar por `HasAmmo()` toda vez.
Já o campo é o ponto que o **próprio jogo** consulta
(`ReloadWeaponClip` trata a reserva como 1.000.000 quando a flag liga) —
é o mesmo mecanismo que o mod nativo `mods/sa2ammo` usa e valida hoje:

```
field WeaponInfo unlimitedAmmo bool true
```

Conclusão prática pra quando o u_patch mergear: patcheie método quando o
método é o gargalo; patcheie **campo** quando a lógica espalhada lê o
estado direto. O `dump.tsv` do u_dump mostra os dois (linhas `M` e `F`)
pra você comparar.

Regra que não resolve (classe/método inexistente, método minúsculo demais
pra patchar) vira linha de log clara e não derruba nada: nem o jogo, nem
as outras regras.

## `dump.tsv` (C5) — o inventário do jogo (u_dump)

Gerado pelo `u_dump` na primeira vez que o jogo roda com ele presente
(hoje: `tools/deploy_mod.sh u_dump <pacote>` — ver README; no futuro o Mod
Maker copia e remove sozinho).
Separador **TAB**. Cabeçalho + 3 tipos de linha:

```
# pkg=com.exemplo.jogo il2cpp_size=123456789 unity=6000.3.13f1
C	Assembly-CSharp	GameLogic.ComplexCreature
M	GameLogic.ComplexCreature	HasAmmo	0	System.Boolean	0
F	GameLogic.WeaponInfo	unlimitedAmmo	System.Boolean	0	0x70
```

- `C` classe (`<assembly>`, `<Namespace.Classe>`)
- `M` método (`<Classe>`, `<método>`, `<nargs>`, `<retorno>`, `<static 0|1>`)
- `F` campo (`<Classe>`, `<campo>`, `<tipo>`, `<static 0|1>`, `<offset>`)

Classe aninhada: `Namespace.Externa/Interna`. Pra refazer o dump, apague o
`dump.tsv` e reinicie o jogo. Os nomes aqui são os mesmos que o `.patch`
e o SDK usam — nada de offset mágico.

## O que o Manager aceita de qualquer origem (C7)

O Manager identifica o arquivo **pelo conteúdo**, não
pela extensão:

| Tipo detectado | Como detecta | Roda? |
|---|---|---|
| `.bmod` | zip com `manifest.json` | sim |
| `.so` Android arm64 | ELF, `e_machine=183` (AArch64) | sim (copia pra pasta) |
| `.so` arm32/x86 | ELF de outra arquitetura | não |
| `.patch` | texto nas regras C4 | **em integração** (u_patch, F4) |
| script Frida `.js` | texto JS (`Interceptor`, `Il2Cpp.perform`) | experimental (gadget 17.19.0 fechou o jogo no teste em celular) |
| `.dll` IL2CPP (BepInEx 6/MelonLoader IL2CPP) | PE + CLI + refs `Il2CppInterop`/`UnhollowerBaseLib` | depois (F12) |
| `.dll` Mono em jogo Android **Mono** | AssemblyRefs sem `Il2Cpp*` + engine mono | depois (F13) |
| `.dll` Mono (ex.: TABS PC) em jogo **IL2CPP** | idem + engine il2cpp | **não automático** (recrie com Mod Maker/SDK) |
| `.exe`/`.dylib`/`.CT` | PE sem CLI / Mach-O / XML Cheat Engine | não |
| `.lua` GameGuardian | texto com `gg.` | depois (F10) |

Nenhum caminho modifica arquivo do jogo — tudo é carregado no processo em
runtime.

## Exemplo ponta a ponta: o `sa2-infinite-ammo.bmod`

> Ainda **não operacional**: `type:patch` depende do `u_patch` (F4, em
> integração). O exemplo fica como referência do formato — quando o F4 e o
> Manager mergarem, o `pack_bmod.sh` valida e empacota, o Manager instala
> com um toque e os arquivos viram `sa2-infinite-ammo.patch`/`.json` como
> na seção C1 acima.

```
manifest.json    {"format":1,"id":"sa2-infinite-ammo","name":"Munição
                  infinita","version":"1.0","game":"com.hyperdotstudios.
                  swampattack2","engine":"unity-il2cpp","type":"patch"}
mod.patch        field WeaponInfo unlimitedAmmo bool true
```
