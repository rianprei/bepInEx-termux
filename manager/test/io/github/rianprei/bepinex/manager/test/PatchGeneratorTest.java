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
}
