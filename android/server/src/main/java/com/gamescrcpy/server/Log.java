package com.gamescrcpy.server;

public final class Log {

    public enum Level { TRACE, DEBUG, INFO, WARNING, ERROR }

    private static volatile Level minimum = Level.INFO;

    private Log() {
    }

    public static void setMinimum(Level level) {
        minimum = level;
    }

    public static Level parseLevel(String name) {
        return switch (name) {
            case "trace" -> Level.TRACE;
            case "debug" -> Level.DEBUG;
            case "warning" -> Level.WARNING;
            case "error" -> Level.ERROR;
            default -> Level.INFO;
        };
    }

    public static boolean enabled(Level level) {
        return level.ordinal() >= minimum.ordinal();
    }

    public static void d(String message) {
        write(Level.DEBUG, message, null);
    }

    public static void i(String message) {
        write(Level.INFO, message, null);
    }

    public static void w(String message) {
        write(Level.WARNING, message, null);
    }

    public static void w(String message, Throwable cause) {
        write(Level.WARNING, message, cause);
    }

    public static void e(String message, Throwable cause) {
        write(Level.ERROR, message, cause);
    }

    private static void write(Level level, String message, Throwable cause) {
        if (!enabled(level)) {
            return;
        }
        final StringBuilder line = new StringBuilder("GameScrcpy server ").append(level.name()).append(": ")
                .append(message);
        if (cause != null) {
            line.append(" (").append(cause.getClass().getSimpleName()).append(": ").append(cause.getMessage())
                    .append(')');
        }
        System.err.println(line);
    }
}
