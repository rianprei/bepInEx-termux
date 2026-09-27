# SDK — mod nativo C++ (caminho genérico)

Como escrever um mod `.so` que roda em qualquer jogo Unity IL2CPP suportado
pelo loader (pasta `mods/<pacote>/`). Tudo por **nome** (classe/método/campo)
via API il2cpp exportada — nada de offset fixo, nada de arquivo do jogo
modificado.

Pré-requisitos: NDK (os scripts usam `~/Android/Sdk/ndk/23.2.8568313`, troca
com a env `NDK_BUILD` no `deploy_mod.sh`), `adb` com o celular conectado,
módulo bepInEx-termux instalado (ver README, "Instalar em 3 passos").

## 1. Criar o mod a partir do template

```bash
tools/new_mod.sh hello
```

Gera `mods/hello/`:

```
mods/hello/
├── jni/
│   ├── Android.mk       módulo NDK (dobby pré-built só se você hookar)
│   ├── Application.mk   flags (arm64-v8a, C++17, -Wall -Wextra)
│   └── mod.cpp          o código: boot il2cpp + log
└── manifest.json        metadados do .bmod (id, engine, opções)
```

`mod.cpp` inteiro (é pouco por design):

```cpp
#include <pthread.h>
#include "../../common/mod_common.h"
#include "../../common/il2cpp_min.h"

#define TAG "hello"

static void *worker(void *) {
    mod_log(TAG, "carregado, esperando libil2cpp.so");
    Il2Cpp il;
    if (!il2cpp_boot(il)) {
        mod_log(TAG, "boot IL2CPP falhou; motivo detalhado no log IL2CPP");
        return nullptr;
    }
    // Daqui pra frente: il.find_class("<ns>", "Classe") etc. (il2cpp_min.h).
    mod_log(TAG, "il2cpp ok");
    return nullptr;
}

__attribute__((constructor)) static void mod_template_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
```

O `constructor` roda no `dlopen` (feito pelo loader ainda no zygote), por
isso ele só solta a thread e volta — todo o trabalho pesado espera o
runtime il2cpp subir dentro da thread. O orçamento total do `il2cpp_boot`
é de **240s** para a biblioteca e o runtime juntos, com polling a cada
200ms e logs periódicos e de desistência com o motivo. Em jogo saudável são
poucos segundos. O caminho efetivamente usado para abrir a biblioteca
(`__loader_dlopen` ou fallback `dlopen`) também aparece no log. `u_frida`
usa um teto menor de 10s e carrega o gadget mesmo sem IL2CPP; scripts que
dependem de `Il2Cpp.*` precisam do runtime pronto.

## 2. Build

Sozinho (mesma linha que o `deploy_mod.sh` usa):

```bash
~/Android/Sdk/ndk/23.2.8568313/ndk-build -C mods/hello NDK_PROJECT_PATH=. \
    APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -B -j4
```

Saída: `mods/hello/libs/arm64-v8a/libhello.so`. Zero warnings é regra do
repo (só o `-static-libstdc++` do NDK 23 é benigno).

## 3. Deploy + teste no device

```bash
tools/deploy_mod.sh hello com.hyperdotstudios.swampattack2
```

O script faz a cadeia toda: build → `adb push` (staging) → `su cp` pra
`/data/local/tmp/mods/<pacote>/hello.so` → `chmod 644` →
`chcon u:object_r:bepinex_mod_file:s0` (sem isso o jogo não lê em SELinux
Enforcing) → `am force-stop` (reinicia o processo do jogo).

Abra o jogo e confira o log — dois caminhos:

- **Botão Ação** do módulo no app Magisk: abre o Manager, se instalado;
  senão mostra os mods ativos e as últimas 5 linhas do log do jogo.
- Terminal: `adb shell su -c "tail -5 /data/data/<pacote>/files/bepinex/log.txt"`

Esperado (contrato C1, `HH:MM:SS [mod] mensagem`):

```
15:42:01 [hello] carregado, esperando libil2cpp.so
15:42:03 [hello] il2cpp ok
```

Junto tem a linha do próprio loader (`15:42:00 [hello] carregado`) — se só
ela apareceu, o `dlopen` funcionou e o problema é do seu `worker()`.

## 4. API do `mods/common/mod_common.h`

Header-only; inclua e use. Compila igual no host (pra teste) — o logcat
vira stderr e os paths de device degradam em silêncio.

| Função | O que faz |
|---|---|
| `mod_pkg()` | Pacote do jogo (`const char*`, ou `nullptr` cedo demais). Fonte: env `BEPINEX_PKG` que o loader seta antes do `dlopen`; fallback `/proc/self/cmdline` **só depois de sair de zygote*** — no constructor ele ainda vale `zygote64`. |
| `mod_dir(out, size)` | `/data/local/tmp/mods/<pacote>/` em `out` (`false` se não souber o pacote). |
| `mod_log(tag, fmt, ...)` | logcat **e** linha C1 no `log.txt` do jogo. Nunca derruba por falha de log. |
| `mod_conf_get(id, key, def)` | Valor string do `<id>.conf` (C3), `def` se não achar. Buffer estático: copie se precisar sobreviver à próxima chamada. |
| `mod_conf_bool/int/float(id, key, def)` | Igual, já convertido (`true/1/yes`, inteiro, double). Sujo/ausente = `def`. |

## 5. Fazer algo de verdade (il2cpp + hook)

`mods/common/il2cpp_min.h` resolve tudo por nome depois do
`il2cpp_boot(il)`:

```cpp
Il2Cpp il;                        // resolvido 1x na thread do mod
void *k = il.find_class("", "ComplexCreature");          // classe (ns, nome)
void *m = il.class_get_method_from_name(k, "HasAmmo", 0); // método (0 args)
void *f = il.class_get_field_from_name(k, "selectedWeapon");
size_t off = il.field_get_offset(f);                       // offset do campo
void *obj = /* qualquer instância */;
il.call(obj, "HasAmmo", nullptr, 0);        // invoca por nome (vira virtual)
void *ptr = il2cpp_method_ptr(il, k, "SelectWeapon", 1);  // código p/ hook
```

Pra hookar, use o Dobby (padrão do `mods/sa2ammo`, o mod de referência —
leia o `jni/sa2ammo_mod.cpp` inteiro, são ~100 linhas):

```cpp
#include "dobby.h"   // + bloco do dobby pré-built no Android.mk (copie do sa2ammo)

static void (*orig_select)(void *, void *, void *);
static void fake_select(void *self, void *w, void *method) {
    // seu código antes do original (w = a arma selecionada)
    orig_select(self, w, method);
}
// na thread, depois de achar o método:
DobbyHook(il2cpp_method_ptr(il, k, "SelectWeapon", 1),
          (void *)fake_select, (void **)&orig_select);
```

Regras do jogo (do roadmap, aprendidas no device):

- **Nunca** chame nada da `Il2Cpp` antes do `il2cpp_boot` retornar
  (`il2cpp_domain_get` antes do `il2cpp_init` crasha o jogo).
- Construtor só solta thread; trabalho fica na thread (o processo ainda
  especializa depois).
- Falha é log + desistir daquele pedaço, nunca crashar o jogo: mod que
  "versão nova do jogo?" no log é o comportamento correto.
- Um mod, um propósito (lição do vault TABS).

## 6. Opções no Manager (manifest + conf)

No `manifest.json` do seu mod:

```json
"options": [
  {"key": "mult", "label": "Multiplicador", "type": "float", "default": 2, "min": 1, "max": 10}
]
```

O Manager vira isso em slider e grava `mult=2` no
`<id>.conf`. No mod: `double mult = mod_conf_float("hello", "mult", 2.0);`.
Formato do `.conf` e regras do manifest: [BMOD-FORMAT.md](BMOD-FORMAT.md).

## 7. Empacotar e distribuir

```bash
tools/pack_bmod.sh hello
```

Valida o `manifest.json` (format/id/engine/type/game) e gera
`mods/hello/hello.bmod` (zip: `manifest.json` + `mod.so`). Quem receber
instala com um toque quando o Manager existir; hoje, o desempacotar manual
está no BMOD-FORMAT.md.

## 8. Testar sem device (o que dá)

O SDK tem núcleos puros testáveis no host:

```bash
g++ -std=c++17 -Wall -Wextra -o /tmp/mod_common_test test/mod_common_test.cpp
/tmp/mod_common_test    # "TODOS PASSARAM (0 falhas)"
```

Cobre o parser `.conf`, a rejeição de `zygote*` no cmdline e os defaults
das conversões. O resto (boot il2cpp, Dobby, game hooks) só existe no
device.
