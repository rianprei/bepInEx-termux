package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.GameInfo;

import java.text.Normalizer;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

public final class GameInfoSorter {
    public enum SortOrder {
        MODS_FIRST("mods_first", 0),
        NAME_ASC("name_asc", 1);

        private final String storedValue;
        private final int position;

        SortOrder(String storedValue, int position) {
            this.storedValue = storedValue;
            this.position = position;
        }

        public String storedValue() {
            return storedValue;
        }

        public int position() {
            return position;
        }

        public static SortOrder fromStoredValue(String value) {
            for (SortOrder order : values()) {
                if (order.storedValue.equals(value)) return order;
            }
            return MODS_FIRST;
        }

        public static SortOrder fromPosition(int position) {
            for (SortOrder order : values()) {
                if (order.position == position) return order;
            }
            return MODS_FIRST;
        }
    }

    private GameInfoSorter() {}

    public static void sort(List<GameInfo> games, SortOrder order) {
        SortOrder selected = order == null ? SortOrder.MODS_FIRST : order;
        Comparator<GameInfo> comparator = Comparator
                .comparing((GameInfo game) -> normalizedName(game.appName))
                .thenComparing(game -> value(game.appName))
                .thenComparing(game -> value(game.packageName));

        if (selected == SortOrder.MODS_FIRST) {
            comparator = Comparator
                    .comparing((GameInfo game) -> game.installedModsCount > 0 ? 0 : 1)
                    .thenComparing(comparator);
        }

        Collections.sort(games, comparator);
    }

    private static String normalizedName(String name) {
        String decomposed = Normalizer.normalize(value(name), Normalizer.Form.NFD);
        StringBuilder withoutMarks = new StringBuilder(decomposed.length());
        for (int offset = 0; offset < decomposed.length();) {
            int codePoint = decomposed.codePointAt(offset);
            int type = Character.getType(codePoint);
            if (type != Character.NON_SPACING_MARK
                    && type != Character.COMBINING_SPACING_MARK
                    && type != Character.ENCLOSING_MARK) {
                withoutMarks.appendCodePoint(codePoint);
            }
            offset += Character.charCount(codePoint);
        }
        return withoutMarks.toString().toLowerCase(Locale.ROOT);
    }

    private static String value(String text) {
        return text == null ? "" : text;
    }
}
