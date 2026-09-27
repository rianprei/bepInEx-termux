// Casos de teste para o tradutor dll2patch.
// Cada classe tem patches Harmony que devem (ou não) ser traduzidos para C4.
using HarmonyLib;

namespace Dll2PatchFixture
{
    // === CASOS TRADUZÍVEIS ===

    // Caso A: Prefix com __result = CONST; return false;
    [HarmonyPatch(typeof(GameClass), "GetHealth")]
    public static class PrefixResultConst
    {
        [HarmonyPrefix]
        public static bool Prefix(ref int __result)
        {
            __result = 100;
            return false;
        }
    }

    // Caso B: Postfix com __result = CONST;
    public static class PostfixResultConst
    {
        [HarmonyPatch(typeof(GameClass), "GetMana")]
        [HarmonyPostfix]
        public static void Postfix(ref int __result)
        {
            __result = 50;
        }
    }

    // Caso C: Postfix com __result *= K
    [HarmonyPatch(typeof(GameClass), "GetDamage")]
    public static class PostfixMul
    {
        [HarmonyPostfix]
        public static void Postfix(ref int __result)
        {
            __result *= 2;
        }
    }

    // Caso D: Prefix/Postfix com T.CampoEstatico = CONST
    [HarmonyPatch(typeof(GameClass), "GetScore")]
    public static class StaticFieldAssign
    {
        [HarmonyPrefix]
        public static bool Prefix()
        {
            GameClass.MaxScore = 9999;
            return true;
        }
    }

    // === CASOS NÃO TRADUZÍVEIS ===

    // Caso E: Transpiler (edita IL, não traduzível)
    [HarmonyPatch(typeof(GameClass), "GetAmmo")]
    public static class TranspilerCase
    {
        [HarmonyTranspiler]
        public static void Transpiler()
        {
            if (GameClass.Noise == 1) GameClass.Noise = 2;
            if (GameClass.Noise == 2) GameClass.Noise = 3;
            if (GameClass.Noise == 3) GameClass.Noise = 4;
            if (GameClass.Noise == 4) GameClass.Noise = 5;
            if (GameClass.Noise == 5) GameClass.Noise = 6;
            if (GameClass.Noise == 6) GameClass.Noise = 7;
            if (GameClass.Noise == 7) GameClass.Noise = 8;
            if (GameClass.Noise == 8) GameClass.Noise = 9;
        }
    }

    // Caso F: Prefix com if (tem branch)
    [HarmonyPatch(typeof(GameClass), "GetShield")]
    public static class IfCase
    {
        [HarmonyPrefix]
        public static bool Prefix(ref int __result)
        {
            if (__result < 0)
            {
                __result = 0;
            }
            return true;
        }
    }

    // Caso G: Postfix que chama outro método
    [HarmonyPatch(typeof(GameClass), "GetSpeed")]
    public static class MethodCallCase
    {
        [HarmonyPostfix]
        public static void Postfix(ref int __result)
        {
            __result = Helper.Double(__result);
        }
    }

    // Caso H: Prefix com string (tipo não suportado)
    [HarmonyPatch(typeof(GameClass), "GetName")]
    public static class StringCase
    {
        [HarmonyPrefix]
        public static bool Prefix(ref string __result)
        {
            __result = "Patched";
            return false;
        }
    }

    // Caso I: Overload ambíguo (dois métodos com mesmo nome)
    [HarmonyPatch(typeof(GameClass), "GetStat")]
    public static class OverloadCase
    {
        [HarmonyPrefix]
        public static bool Prefix(ref int __result)
        {
            __result = 42;
            return false;
        }
    }

    // Classe alvo dos patches
    public class GameClass
    {
        public static int MaxScore;
        public static int Noise;
        public int health;
        public int mana;
        public int damage;
        public int score;
        public int ammo;
        public int shield;
        public int speed;
        public string name;
        public int stat;

        public int GetHealth() { return health; }
        public int GetMana() { return mana; }
        public int GetDamage() { return damage; }
        public int GetScore() { return score; }
        public int GetAmmo() { return ammo; }
        public int GetShield() { return shield; }
        public int GetSpeed() { return speed; }
        public string GetName() { return name; }
        public int GetStat() { return stat; }
        public int GetStat(int bonus) { return stat + bonus; }
    }

    public static class Helper
    {
        public static int Double(int x) { return x * 2; }
    }
}
