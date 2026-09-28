# u_patch — motor declarativo (F4)

Lê todo `*.bpatch` (+ `<id>.conf`) de `/data/adb/bepinex/mods/<pkg>/` (pai root-only; o conteúdo chega pelo socket do companion) e aplica
as regras C4 em runtime. Regra que não resolve vira log, nunca crash.

```
return  <Classe>  <Método>  <nargs>  <bool|int|float>  <valor>   # patch arm64 (mov x0/s0 + ret)
mul     <Classe>  <Método>  <nargs>  <int|float>       <fator>   # DobbyHook (pool de 24 thunks)
static  <Classe>  <campo>   <bool|int|float>           <valor>   # fixa e reaplica a cada 2s
```

Exemplo (SA2): `return ComplexCreature HasAmmo 0 bool true`.

Log: logcat `u_patch` + `/data/data/<pkg>/files/bepinex/log.txt`.

Em processos ARM32 este módulo registra no log que não há suporte e sai sem
aplicar regras: os thunks `return`, `mul` e `field` ainda emitem instruções
AArch64. `static` também fica desativado junto com o motor.
