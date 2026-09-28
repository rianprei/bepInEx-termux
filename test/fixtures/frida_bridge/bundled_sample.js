// Fixture: um bundle JA empacotado, na forma que o tools/bundle_frida_script.sh
// produz. Pequeno de proposito — o bundle real da ponte 0.14.0 tem ~168 KB e o
// gate nao baixa nada; o que o gate precisa provar e a FORMA do arquivo, e ela
// e a mesma em qualquer tamanho.
//
// Proveniencia: gerado com a mesma ferramenta, da ponte pinada em
// tools/frida_il2cpp_bridge.lock (frida-il2cpp-bridge 0.14.0, MIT,
// sha256 do tarball 7329b73839edc1c7dadb407a4267d036fced4eff78ebf5a6840193111203731b).
// A licença completa está em tools/frida_il2cpp_bridge.LICENSE.
// As duas linhas de baixo são o começo de `globalThis.Il2Cpp = Il2Cpp;`, que é
// como a ponte se autoinstala — por isso o bundle não precisa de shim.
(function (root) {
    "use strict";
    var __defProp = Object.defineProperty;
    var Il2Cpp = {};
    Il2Cpp.version = "0.14.0";
    Il2Cpp.perform = function (fn) { return { ran: true, called: typeof fn === "function" }; };
    Il2Cpp.Module = {};
    root.Il2Cpp = Il2Cpp;
})(globalThis);

// ---- script do usuario: exemplo.js ----
if (typeof Il2Cpp === "undefined") {
    throw new Error("Il2Cpp ausente: o bundle nao embutiu a ponte");
}
var r = Il2Cpp.perform(function () { return 1; });
if (!r || r.ran !== true || r.called !== true) {
    throw new Error("Il2Cpp.perform nao rodou como o script espera");
}
console.log("il2cpp-bridge fixture: perform() respondeu, ran=" + r.ran);
