# test/fixtures/tabs — bytes reais do APK do TABS, só o que o patcher toca

Dois arquivos, extraídos de `com.xd.tabs.google` 1.1.05 (versionCode 81,
build 2026-09-28, backup em
`~/Documentos/mods/_backups/tabs-apk-original-1.1.05/base.apk`,
sha256 `fe23f5e9…d3d4`):

| fixture | bytes no APK | sha256 |
|---|---:|---|
| `AndroidManifest.xml` | 28576 | `5e25308e0503d377d18026d228561aae235d01e6c64b02534cd751fee01b4031` |
| `XDConfig.json` (= `assets/XDConfig.json`) | 1006 | `9ed1fc0f9a74db5bb0a96c8098111059c04b895201e670ad84d490009cf82a9c` |

Por que os bytes REAIS e não um AXML sintético: o patcher mexe em offsets
binários, e um AXML mounts a dedo no teste ensinaria o teste a esperar o
layout que o próprio patcher assume. Com o arquivo real, se um build novo
mudar o layout, o teste quebra — que é o ponto.

O APK inteiro (1,4 GB) **não** entra no git: o patcher roda contra o backup,
e o gate roda contra estes dois arquivos.
