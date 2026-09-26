package io.github.rianprei.bepinex.manager.core;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

// Parser e gerador JSON minimo em Java puro (sem dependencias externas).
public final class MiniJson {
    private MiniJson() {}

    public static Object parse(String json) {
        if (json == null) return null;
        return new Parser(json.trim()).parseValue();
    }

    @SuppressWarnings("unchecked")
    public static Map<String, Object> parseObject(String json) {
        Object obj = parse(json);
        if (obj instanceof Map) return (Map<String, Object>) obj;
        throw new IllegalArgumentException("JSON nao e um objeto");
    }

    public static String toJson(Object obj) {
        StringBuilder sb = new StringBuilder();
        writeValue(obj, sb);
        return sb.toString();
    }

    @SuppressWarnings("unchecked")
    private static void writeValue(Object obj, StringBuilder sb) {
        if (obj == null) {
            sb.append("null");
        } else if (obj instanceof String) {
            sb.append('"').append(escape((String) obj)).append('"');
        } else if (obj instanceof Number || obj instanceof Boolean) {
            sb.append(obj);
        } else if (obj instanceof Map) {
            Map<?, ?> map = (Map<?, ?>) obj;
            sb.append('{');
            boolean first = true;
            for (Map.Entry<?, ?> entry : map.entrySet()) {
                if (!first) sb.append(',');
                first = false;
                sb.append('"').append(escape(String.valueOf(entry.getKey()))).append("\":");
                writeValue(entry.getValue(), sb);
            }
            sb.append('}');
        } else if (obj instanceof Iterable) {
            Iterable<?> iter = (Iterable<?>) obj;
            sb.append('[');
            boolean first = true;
            for (Object item : iter) {
                if (!first) sb.append(',');
                first = false;
                writeValue(item, sb);
            }
            sb.append(']');
        } else {
            sb.append('"').append(escape(obj.toString())).append('"');
        }
    }

    private static String escape(String s) {
        return s.replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\b", "\\b")
                .replace("\f", "\\f")
                .replace("\n", "\\n")
                .replace("\r", "\\r")
                .replace("\t", "\\t");
    }

    private static final class Parser {
        private final String src;
        private int pos = 0;

        Parser(String src) {
            this.src = src;
        }

        Object parseValue() {
            skipWhitespace();
            if (pos >= src.length()) throw new IllegalArgumentException("Fim inesperado do JSON");
            char c = src.charAt(pos);
            if (c == '{') return parseObject();
            if (c == '[') return parseArray();
            if (c == '"') return parseString();
            if (c == 't' || c == 'f') return parseBoolean();
            if (c == 'n') return parseNull();
            if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
            throw new IllegalArgumentException("Caractere invalido '" + c + "' no indice " + pos);
        }

        private Map<String, Object> parseObject() {
            Map<String, Object> map = new LinkedHashMap<>();
            pos++; // pular '{'
            skipWhitespace();
            if (peek('}')) {
                pos++;
                return map;
            }
            while (true) {
                skipWhitespace();
                if (pos >= src.length() || src.charAt(pos) != '"') {
                    throw new IllegalArgumentException("Esperado chave como string no indice " + pos);
                }
                String key = parseString();
                skipWhitespace();
                if (pos >= src.length() || src.charAt(pos) != ':') {
                    throw new IllegalArgumentException("Esperado ':' apos chave no indice " + pos);
                }
                pos++; // pular ':'
                Object val = parseValue();
                map.put(key, val);
                skipWhitespace();
                if (pos >= src.length()) throw new IllegalArgumentException("Fim inesperado ao ler objeto");
                char c = src.charAt(pos);
                if (c == '}') {
                    pos++;
                    return map;
                }
                if (c == ',') {
                    pos++;
                } else {
                    throw new IllegalArgumentException("Esperado ',' ou '}' no indice " + pos);
                }
            }
        }

        private List<Object> parseArray() {
            List<Object> list = new ArrayList<>();
            pos++; // pular '['
            skipWhitespace();
            if (peek(']')) {
                pos++;
                return list;
            }
            while (true) {
                list.add(parseValue());
                skipWhitespace();
                if (pos >= src.length()) throw new IllegalArgumentException("Fim inesperado ao ler array");
                char c = src.charAt(pos);
                if (c == ']') {
                    pos++;
                    return list;
                }
                if (c == ',') {
                    pos++;
                } else {
                    throw new IllegalArgumentException("Esperado ',' ou ']' no indice " + pos);
                }
            }
        }

        private String parseString() {
            pos++; // pular '"'
            StringBuilder sb = new StringBuilder();
            while (pos < src.length()) {
                char c = src.charAt(pos++);
                if (c == '"') return sb.toString();
                if (c == '\\') {
                    if (pos >= src.length()) throw new IllegalArgumentException("Escape invalido no final");
                    char esc = src.charAt(pos++);
                    switch (esc) {
                        case '"': sb.append('"'); break;
                        case '\\': sb.append('\\'); break;
                        case '/': sb.append('/'); break;
                        case 'b': sb.append('\b'); break;
                        case 'f': sb.append('\f'); break;
                        case 'n': sb.append('\n'); break;
                        case 'r': sb.append('\r'); break;
                        case 't': sb.append('\t'); break;
                        case 'u':
                            if (pos + 4 > src.length()) throw new IllegalArgumentException("Escape unicode invalido");
                            String hex = src.substring(pos, pos + 4);
                            sb.append((char) Integer.parseInt(hex, 16));
                            pos += 4;
                            break;
                        default: sb.append(esc); break;
                    }
                } else {
                    sb.append(c);
                }
            }
            throw new IllegalArgumentException("String nao terminada");
        }

        private Boolean parseBoolean() {
            if (src.startsWith("true", pos)) {
                pos += 4;
                return Boolean.TRUE;
            }
            if (src.startsWith("false", pos)) {
                pos += 5;
                return Boolean.FALSE;
            }
            throw new IllegalArgumentException("Esperado boolean no indice " + pos);
        }

        private Object parseNull() {
            if (src.startsWith("null", pos)) {
                pos += 4;
                return null;
            }
            throw new IllegalArgumentException("Esperado null no indice " + pos);
        }

        private Number parseNumber() {
            int start = pos;
            if (src.charAt(pos) == '-') pos++;
            while (pos < src.length() && Character.isDigit(src.charAt(pos))) pos++;
            boolean isFloat = false;
            if (pos < src.length() && src.charAt(pos) == '.') {
                isFloat = true;
                pos++;
                while (pos < src.length() && Character.isDigit(src.charAt(pos))) pos++;
            }
            if (pos < src.length() && (src.charAt(pos) == 'e' || src.charAt(pos) == 'E')) {
                isFloat = true;
                pos++;
                if (pos < src.length() && (src.charAt(pos) == '+' || src.charAt(pos) == '-')) pos++;
                while (pos < src.length() && Character.isDigit(src.charAt(pos))) pos++;
            }
            String numStr = src.substring(start, pos);
            if (isFloat) return Double.parseDouble(numStr);
            try {
                return Long.parseLong(numStr);
            } catch (NumberFormatException e) {
                return Double.parseDouble(numStr);
            }
        }

        private void skipWhitespace() {
            while (pos < src.length()) {
                char c = src.charAt(pos);
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    pos++;
                } else {
                    break;
                }
            }
        }

        private boolean peek(char c) {
            return pos < src.length() && src.charAt(pos) == c;
        }
    }
}
