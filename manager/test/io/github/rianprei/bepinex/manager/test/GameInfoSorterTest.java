package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.GameInfoSorter;
import io.github.rianprei.bepinex.manager.model.GameInfo;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class GameInfoSorterTest {
    public static void run() {
        testModsFirstThenDeterministicName();
        testNameOnlyOrder();
        testPackageNameBreaksDisplayNameTie();
        testStableExactTies();
        testStoredOrderFallback();
        System.out.println("  [OK] GameInfoSorterTest (ordem e desempates da lista)");
    }

    private static void testModsFirstThenDeterministicName() {
        GameInfo noMods = game("pkg.aaa", "Aardvark", 0);
        GameInfo accent = game("pkg.accent", "Álpha", 1);
        GameInfo lower = game("pkg.lower", "alpha", 1);
        GameInfo upper = game("pkg.upper", "Alpha", 1);
        GameInfo zulu = game("pkg.zulu", "Zulu", 2);
        List<GameInfo> games = new ArrayList<>(Arrays.asList(
                noMods, zulu, accent, lower, upper));

        GameInfoSorter.sort(games, GameInfoSorter.SortOrder.MODS_FIRST);

        expect(games, upper, lower, accent, zulu, noMods);
    }

    private static void testNameOnlyOrder() {
        GameInfo noMods = game("pkg.aaa", "Aardvark", 0);
        GameInfo withMods = game("pkg.zulu", "Zulu", 2);
        List<GameInfo> games = new ArrayList<>(Arrays.asList(withMods, noMods));

        GameInfoSorter.sort(games, GameInfoSorter.SortOrder.NAME_ASC);

        expect(games, noMods, withMods);
    }

    private static void testStableExactTies() {
        GameInfo first = game("pkg.same", "Same", 0);
        GameInfo second = game("pkg.same", "Same", 0);
        List<GameInfo> games = new ArrayList<>(Arrays.asList(first, second));

        GameInfoSorter.sort(games, GameInfoSorter.SortOrder.MODS_FIRST);

        if (games.get(0) != first || games.get(1) != second) {
            throw new AssertionError("Chaves de ordenação idênticas devem preservar a ordem de entrada");
        }
    }

    private static void testPackageNameBreaksDisplayNameTie() {
        GameInfo laterPackage = game("pkg.z-last", "Same", 1);
        GameInfo earlierPackage = game("pkg.a-first", "Same", 1);
        List<GameInfo> games = new ArrayList<>(Arrays.asList(laterPackage, earlierPackage));

        GameInfoSorter.sort(games, GameInfoSorter.SortOrder.MODS_FIRST);

        expect(games, earlierPackage, laterPackage);
    }

    private static void testStoredOrderFallback() {
        if (GameInfoSorter.SortOrder.fromStoredValue(null) != GameInfoSorter.SortOrder.MODS_FIRST
                || GameInfoSorter.SortOrder.fromStoredValue("unknown") != GameInfoSorter.SortOrder.MODS_FIRST
                || GameInfoSorter.SortOrder.fromPosition(99) != GameInfoSorter.SortOrder.MODS_FIRST
                || GameInfoSorter.SortOrder.fromPosition(1) != GameInfoSorter.SortOrder.NAME_ASC) {
            throw new AssertionError("Preferência desconhecida deve cair no padrão explícito mods-primeiro");
        }
    }

    private static GameInfo game(String packageName, String appName, int mods) {
        GameInfo game = new GameInfo(packageName, appName, "unknown");
        game.installedModsCount = mods;
        return game;
    }

    private static void expect(List<GameInfo> actual, GameInfo... expected) {
        if (!actual.equals(Arrays.asList(expected))) {
            throw new AssertionError("Ordem inesperada: " + describe(actual));
        }
    }

    private static String describe(List<GameInfo> games) {
        List<String> values = new ArrayList<>();
        for (GameInfo game : games) {
            values.add(game.appName + " [" + game.packageName + ", mods="
                    + game.installedModsCount + "]");
        }
        return values.toString();
    }
}
