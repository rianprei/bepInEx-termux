package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.GameInfoSorter;
import io.github.rianprei.bepinex.manager.model.GameInfo;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class GameInfoSorterTest {
    public static void run() throws IOException {
        testModsFirstThenDeterministicName();
        testNameOnlyOrder();
        testPackageNameBreaksDisplayNameTie();
        testStableExactTies();
        testStoredOrderFallback();
        testNullNamesAndPackages();
        testResourceSortOrderMatchesEnum();
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

    private static void testNullNamesAndPackages() {
        GameInfo betaPackage = game("pkg.beta", "Beta", 0);
        GameInfo betaNoPackage = game(null, "Beta", 0);
        GameInfo unnamedPackage = game("pkg.unnamed", null, 0);
        GameInfo unnamedNoPackage = game(null, null, 0);
        List<GameInfo> games = new ArrayList<>(Arrays.asList(
                betaPackage, unnamedPackage, betaNoPackage, unnamedNoPackage));

        try {
            GameInfoSorter.sort(games, GameInfoSorter.SortOrder.NAME_ASC);
        } catch (NullPointerException e) {
            throw new AssertionError("Ordenar deve aceitar appName e packageName nulos", e);
        }

        expect(games, unnamedNoPackage, unnamedPackage, betaNoPackage, betaPackage);
    }

    private static void testResourceSortOrderMatchesEnum() throws IOException {
        Pattern arrayPattern = Pattern.compile(
                "<string-array\\s+name\\s*=\\s*[\"']game_sort_options[\"'][^>]*>(.*?)</string-array>",
                Pattern.DOTALL);
        Pattern optionPattern = Pattern.compile(
                "<item(?:\\s+[^>]*)?>\\s*@string/sort_option_([a-z0-9_]+)\\s*</item>");
        GameInfoSorter.SortOrder[] orders = GameInfoSorter.SortOrder.values();
        int arraysFound = 0;
        Path res = resourceDirectory();

        try (java.util.stream.Stream<Path> files = Files.walk(res)) {
            for (Path xml : files.filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().endsWith(".xml"))
                    .filter(path -> path.getParent().getFileName().toString().startsWith("values"))
                    .toList()) {
                String contents = Files.readString(xml);
                Matcher arrays = arrayPattern.matcher(contents);
                while (arrays.find()) {
                    arraysFound++;
                    List<String> keys = new ArrayList<>();
                    Matcher options = optionPattern.matcher(arrays.group(1));
                    while (options.find()) keys.add(options.group(1));
                    if (keys.size() != orders.length) {
                        throw new AssertionError(xml + ": game_sort_options tem " + keys.size()
                                + " entradas, mas SortOrder tem " + orders.length);
                    }
                    for (int position = 0; position < orders.length; position++) {
                        String key = keys.get(position);
                        GameInfoSorter.SortOrder resolved =
                                GameInfoSorter.SortOrder.fromStoredValue(key);
                        if (!resolved.storedValue().equals(key)
                                || resolved.position() != position
                                || orders[position] != resolved) {
                            throw new AssertionError(xml + ": opção " + position + " ("
                                    + key + ") diverge de SortOrder");
                        }
                    }
                }
            }
        }

        if (arraysFound == 0) {
            throw new AssertionError("Nenhum game_sort_options encontrado em res/values*");
        }
    }

    private static Path resourceDirectory() {
        Path current = Path.of(System.getProperty("user.dir")).toAbsolutePath().normalize();
        for (Path candidate = current; candidate != null; candidate = candidate.getParent()) {
            Path direct = candidate.resolve("res");
            if (Files.isDirectory(direct)) return direct;
            Path manager = candidate.resolve("manager/res");
            if (Files.isDirectory(manager)) return manager;
        }
        throw new AssertionError("Diretório manager/res não encontrado a partir de " + current);
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
