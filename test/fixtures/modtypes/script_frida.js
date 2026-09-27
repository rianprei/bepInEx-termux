// script Frida: hook no metodo Update do jogo
Java.perform(function () {
  var m = Process.getModuleByName('libunity.so');
  Interceptor.attach(Module.findExportByName('libunity.so', 'Update'), {
    onEnter: function (args) { Memory.writeU32(args[0], 9999); }
  });
});
