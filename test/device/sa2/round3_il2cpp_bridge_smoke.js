// Device-round-3 probe: exercise the bundled bridge without invoking game APIs.
(function () {
    var marker = "/data/data/com.hyperdotstudios.swampattack2/files/bepinex/round3_il2cpp_bridge.txt";
    Il2Cpp.perform(function () {
        File.writeAllText(marker, "Il2Cpp.perform round3 ok\n");
    });
})();
