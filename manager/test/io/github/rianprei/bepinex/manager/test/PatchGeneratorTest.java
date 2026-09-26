package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.PatchGenerator;
import io.github.rianprei.bepinex.manager.model.PatchRule;

import java.util.ArrayList;
import java.util.List;

public class PatchGeneratorTest {
    public static void run() {
        testParseAndFormatRules();
        testVariableReference();
        testMalformedIgnored();
        testFieldVerb();
        testFieldMalformedIgnored();
        System.out.println("  [OK] PatchGeneratorTest (C4)");
    }

    private static void testParseAndFormatRules() {
        String patchContent = "# Mod declarative rules\n" +
                "return ComplexCreature HasAmmo 0 bool true\n" +
                "mul PlayerController GetSpeed 0 float 2.5\n" +
                "static GameConfig GodMode bool true\n";

        List<PatchRule> rules = PatchGenerator.parse(patchContent);
        if (rules.size() != 3) throw new AssertionError("esperado 3 regras, obteve " + rules.size());

        PatchRule r1 = rules.get(0);
        if (!"return".equals(r1.action) || !"ComplexCreature".equals(r1.targetClass) || !"HasAmmo".equals(r1.member) || r1.nargs != 0 || !"bool".equals(r1.valueType) || !"true".equals(r1.value)) {
            throw new AssertionError("r1 incorreto: " + r1.toLine());
        }

        PatchRule r2 = rules.get(1);
        if (!"mul".equals(r2.action) || !"PlayerController".equals(r2.targetClass) || !"GetSpeed".equals(r2.member) || r2.nargs != 0 || !"float".equals(r2.valueType) || !"2.5".equals(r2.value)) {
            throw new AssertionError("r2 incorreto: " + r2.toLine());
        }

        PatchRule r3 = rules.get(2);
        if (!"static".equals(r3.action) || !"GameConfig".equals(r3.targetClass) || !"GodMode".equals(r3.member) || !"bool".equals(r3.valueType) || !"true".equals(r3.value)) {
            throw new AssertionError("r3 incorreto: " + r3.toLine());
        }

        String formatted = PatchGenerator.format(rules);
        if (!formatted.contains("return ComplexCreature HasAmmo 0 bool true")) {
            throw new AssertionError("format falhou na regra return");
        }
        if (!formatted.contains("mul PlayerController GetSpeed 0 float 2.5")) {
            throw new AssertionError("format falhou na regra mul");
        }
        if (!formatted.contains("static GameConfig GodMode bool true")) {
            throw new AssertionError("format falhou na regra static");
        }
    }

    private static void testVariableReference() {
        List<PatchRule> list = new ArrayList<>();
        list.add(PatchRule.makeMul("Hero", "GetDamage", 1, "float", "$damage_mult"));
        list.add(PatchRule.makeReturn("Hero", "IsDead", 0, "bool", "$invincible"));

        String text = PatchGenerator.format(list);
        List<PatchRule> parsed = PatchGenerator.parse(text);
        if (parsed.size() != 2) throw new AssertionError("esperado 2 regras com variaveis");
        if (!"$damage_mult".equals(parsed.get(0).value)) throw new AssertionError("variavel $damage_mult perdida");
        if (!"$invincible".equals(parsed.get(1).value)) throw new AssertionError("variavel $invincible perdida");
    }

    private static void testMalformedIgnored() {
        String bad = "# comentario\n" +
                "invalid line\n" +
                "return OnlyClass\n" +
                "static OnlyTwo\n" +
                "return ValidClass ValidMethod 0 bool true\n";

        List<PatchRule> rules = PatchGenerator.parse(bad);
        if (rules.size() != 1) throw new AssertionError("deveria ignorar linhas invalidas e ler 1 regra");
    }

    // C4: field <Classe> <campo> <tipo> <valor> [<Método> <nargs>]
    private static void testFieldVerb() {
        String patchContent =
                "field WeaponInfo unlimitedAmmo bool true Update 1\n" +
                "field WeaponInfo cooldown float 0\n" +
                "field PlayerData lives int 99 OnDamage 0\n";

        List<PatchRule> rules = PatchGenerator.parse(patchContent);
        if (rules.size() != 3) throw new AssertionError("esperado 3 regras field, obteve " + rules.size());

        PatchRule comMetodo = rules.get(0);
        if (!"field".equals(comMetodo.action)) throw new AssertionError("action: " + comMetodo.action);
        if (!"WeaponInfo".equals(comMetodo.targetClass) || !"unlimitedAmmo".equals(comMetodo.member)) {
            throw new AssertionError("classe/campo: " + comMetodo.toLine());
        }
        if (!"bool".equals(comMetodo.valueType) || !"true".equals(comMetodo.value)) {
            throw new AssertionError("tipo/valor: " + comMetodo.toLine());
        }
        if (!comMetodo.hasMethod() || !"Update".equals(comMetodo.method) || comMetodo.nargs != 1) {
            throw new AssertionError("metodo/nargs: " + comMetodo.toLine());
        }

        PatchRule semMetodo = rules.get(1);
        if (!"field".equals(semMetodo.action) || semMetodo.hasMethod()) {
            throw new AssertionError("field sem metodo tem que ser hasMethod()==false: " + semMetodo.toLine());
        }
        if (semMetodo.nargs != PatchRule.NO_METHOD) {
            throw new AssertionError("field sem metodo usa nargs = NO_METHOD: " + semMetodo.toLine());
        }

        PatchRule zerArgs = rules.get(2);
        if (!zerArgs.hasMethod() || !"OnDamage".equals(zerArgs.method) || zerArgs.nargs != 0) {
            throw new AssertionError("nargs=0 é método válido: " + zerArgs.toLine());
        }

        // toLine() tem que devolver exatamente a linha do C4 (round-trip).
        String formatted = PatchGenerator.format(rules);
        String[] lines = formatted.split("\n");
        if (lines.length != 4) throw new AssertionError("cabecalho + 3 regras, obteve " + lines.length);
        if (!"field WeaponInfo unlimitedAmmo bool true Update 1".equals(lines[1])) {
            throw new AssertionError("linha field com metodo: " + lines[1]);
        }
        if (!"field WeaponInfo cooldown float 0".equals(lines[2])) {
            throw new AssertionError("linha field sem metodo: " + lines[2]);
        }
        if (!"field PlayerData lives int 99 OnDamage 0".equals(lines[3])) {
            throw new AssertionError("linha field com nargs=0: " + lines[3]);
        }
        if (lines[2].split("\\s+").length != 5) {
            throw new AssertionError("field sem metodo tem que ter 4 campos,+: " + lines[2]);
        }

        // Ida e volta: format -> parse mantém método e nargs.
        List<PatchRule> round = PatchGenerator.parse(formatted);
        if (round.size() != 3) throw new AssertionError("round-trip perdeu regra");
        if (!"Update".equals(round.get(0).method) || round.get(0).nargs != 1) {
            throw new AssertionError("round-trip perdeu metodo/nargs: " + round.get(0).toLine());
        }
        if (round.get(1).hasMethod()) {
            throw new AssertionError("round-trip inventou metodo: " + round.get(1).toLine());
        }
    }

    private static void testFieldMalformedIgnored() {
        String bad =
                "field SoClasse\n" +                                  // 2 campos
                "field Classe campo\n" +                              // 3 campos
                "field Classe campo bool\n" +                         // 4 campos
                "field Classe campo bool true Metodo\n" +             // 6: falta nargs
                "field Classe campo bool true Metodo x\n" +           // 7: nargs nao numerico
                "field Classe campo bool true Metodo -1\n" +          // 7: nargs negativo
                "field Classe campo bool true Metodo 0 Extra\n" +    // 8: sobra token
                "field WeaponInfo unlimitedAmmo bool true Update 1\n";

        List<PatchRule> rules = PatchGenerator.parse(bad);
        if (rules.size() != 1) {
            throw new AssertionError("linhas field invalidas tem que ser ignoradas, leu " + rules.size());
        }
        if (!"Update".equals(rules.get(0).method)) {
            throw new AssertionError("regra valida depois das invalidas: " + rules.get(0).toLine());
        }
    }
}
