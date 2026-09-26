package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.DumpEntry;

import java.io.BufferedReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

// Analisador eficiente de dump.tsv (Contrato C5) com busca paginada.
public final class DumpParser {
    public static final class Header {
        public String pkg;
        public long il2cppSize;
        public String unityVersion;
    }

    public static final class SearchResult {
        public final List<DumpEntry> entries;
        public final int offset;
        public final boolean hasMore;
        public final int totalScanned;

        public SearchResult(List<DumpEntry> entries, int offset, boolean hasMore, int totalScanned) {
            this.entries = entries;
            this.offset = offset;
            this.hasMore = hasMore;
            this.totalScanned = totalScanned;
        }
    }

    private DumpParser() {}

    public static Header parseHeader(String line) {
        if (line == null || !line.startsWith("#")) return null;
        Header h = new Header();
        String[] tokens = line.substring(1).trim().split("\\s+");
        for (String token : tokens) {
            int eq = token.indexOf('=');
            if (eq > 0) {
                String key = token.substring(0, eq).trim();
                String val = token.substring(eq + 1).trim();
                if ("pkg".equals(key)) {
                    h.pkg = val;
                } else if ("il2cpp_size".equals(key)) {
                    try { h.il2cppSize = Long.parseLong(val); } catch (NumberFormatException ignored) {}
                } else if ("unity".equals(key)) {
                    h.unityVersion = val;
                }
            }
        }
        return h;
    }

    public static DumpEntry parseLine(String line) {
        if (line == null || line.isEmpty() || line.startsWith("#")) return null;
        String[] parts = line.split("\t");
        if (parts.length < 3) return null;

        String kind = parts[0];
        if (DumpEntry.KIND_CLASS.equals(kind)) {
            DumpEntry entry = new DumpEntry();
            entry.kind = DumpEntry.KIND_CLASS;
            entry.assembly = parts[1];
            entry.className = parts[2];
            return entry;
        } else if (DumpEntry.KIND_METHOD.equals(kind)) {
            if (parts.length < 6) return null;
            DumpEntry entry = new DumpEntry();
            entry.kind = DumpEntry.KIND_METHOD;
            entry.className = parts[1];
            entry.name = parts[2];
            try { entry.nargs = Integer.parseInt(parts[3]); } catch (NumberFormatException e) { entry.nargs = 0; }
            entry.type = parts[4];
            entry.isStatic = "1".equals(parts[5]);
            return entry;
        } else if (DumpEntry.KIND_FIELD.equals(kind)) {
            if (parts.length < 6) return null;
            DumpEntry entry = new DumpEntry();
            entry.kind = DumpEntry.KIND_FIELD;
            entry.className = parts[1];
            entry.name = parts[2];
            entry.type = parts[3];
            entry.isStatic = "1".equals(parts[4]);
            entry.offset = parts[5];
            return entry;
        }
        return null;
    }

    // Busca paginada por stream sem carregar todo o arquivo na memoria (100k+ linhas).
    public static SearchResult search(BufferedReader reader, String query, String kindFilter, int offset, int limit) throws IOException {
        List<DumpEntry> list = new ArrayList<>(limit);
        String q = (query != null) ? query.trim().toLowerCase() : "";
        int matchIndex = 0;
        int linesScanned = 0;
        boolean hasMore = false;

        String line;
        while ((line = reader.readLine()) != null) {
            linesScanned++;
            if (line.isEmpty() || line.startsWith("#")) continue;

            // Filtro rapido de texto antes de instanciar objeto
            if (!q.isEmpty() && !line.toLowerCase().contains(q)) {
                continue;
            }

            DumpEntry entry = parseLine(line);
            if (entry == null) continue;

            if (kindFilter != null && !kindFilter.isEmpty() && !kindFilter.equals(entry.kind)) {
                continue;
            }

            if (matchIndex >= offset) {
                if (list.size() < limit) {
                    list.add(entry);
                } else {
                    hasMore = true;
                    break;
                }
            }
            matchIndex++;
        }

        return new SearchResult(list, offset, hasMore, linesScanned);
    }
}
