# u_patch — motor declarativo (F4)

Lê todo `*.bpatch` (+ `<id>.conf`) de `/data/local/tmp/mods/<pkg>/` e aplica
as regras C4 em runtime. Regra que não resolve vira log, nunca crash.

```
return  <Classe>  <Método>  <nargs>  <bool|int|float>  <valor>   # patch arm64 (mov x0/s0 + ret)
mul     <Classe>  <Método>  <nargs>  <int|float>       <fator>   # DobbyHook (pool de 24 thunks)
static  <Classe>  <campo>   <bool|int|float>           <valor>   # fixa e reaplica a cada 2s
```

Exemplo (SA2): `return ComplexCreature HasAmmo 0 bool true`.

Log: logcat `u_patch` + `/data/data/<pkg>/files/bepinex/log.txt`.
