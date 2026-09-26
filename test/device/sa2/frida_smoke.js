// T1 frida_smoke.js — smoke do u_frida SEM bridge (só API built-in).
// Prova execução ESCREVENDO arquivo (console.log vai pra /dev/null no app,
// não aparece no logcat) e conta il2cpp_runtime_invoke por 5s.
// File API (estática e síncrona): https://frida.re/docs/javascript-api/
//   File.writeAllText(path, text) — string vira UTF-8 no arquivo.
(function () {
    var pkg = "com.hyperdotstudios.swampattack2";
    var dir1 = "/data/data/" + pkg + "/files/bepinex/";
    var dir2 = "/data/data/" + pkg + "/files/";
    function writeTxt(name, text) {
        try { File.writeAllText(dir1 + name, text); return true; }
        catch (e) { try { File.writeAllText(dir2 + name, text); return true; }
                    catch (e2) { return false; } }
    }
    writeTxt("frida_ok.txt", "frida_smoke ok pid=" + Process.id + " t=" + Date.now() + "\n");
    var addr = Module.getGlobalExportByName("il2cpp_runtime_invoke");
    var n = 0;
    Interceptor.attach(addr, { onEnter: function () { n++; } });
    setTimeout(function () {
        writeTxt("frida_count.txt", "il2cpp_runtime_invoke em 5s: " + n + "\n");
    }, 5000);
})();
