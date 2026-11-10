package com.gamescrcpy.server.uhid;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

final class HidDescriptor {

    private static final int LONG_ITEM = 0xFE;
    private static final int USAGE_PAGE = 0x04;
    private static final int USAGE = 0x08;
    private static final int LOGICAL_MAXIMUM = 0x24;
    private static final int REPORT_SIZE = 0x74;
    private static final int REPORT_ID = 0x84;
    private static final int REPORT_COUNT = 0x94;
    private static final int INPUT = 0x80;
    private static final int OUTPUT = 0x90;
    private static final int FEATURE = 0xB0;
    private static final int COLLECTION = 0xA0;
    private static final int END_COLLECTION = 0xC0;
    private static final int CONSTANT = 0x01;
    private static final int APPLICATION = 0x01;
    private static final int CONTACT_COUNT_MAXIMUM = 0x000D0055;
    private static final int JOYSTICK = 0x00010004;
    private static final int GAME_PAD = 0x00010005;
    private static final long MOST_REPORT_BITS = 8L * UhidEvents.DATA_MAX;

    private final boolean numbered;
    private final boolean gamepad;
    private final Map<Integer, byte[]> features = new HashMap<>();

    HidDescriptor(byte[] descriptor) {
        boolean ids = false;
        boolean pad = false;
        int depth = 0;
        int usagePage = 0;
        int logicalMaximum = 0;
        long reportSize = 0;
        long reportCount = 0;
        int reportId = 0;
        final List<Integer> usages = new ArrayList<>();
        final Map<Integer, Long> featureBits = new HashMap<>();
        final Map<Integer, List<long[]>> maxima = new HashMap<>();
        int at = 0;
        while (at < descriptor.length) {
            final int prefix = descriptor[at] & 0xFF;
            if (prefix == LONG_ITEM) {
                break;
            }
            final int size = (prefix & 3) == 3 ? 4 : prefix & 3;
            if (at + 1 + size > descriptor.length) {
                break;
            }
            final int value = littleEndian(descriptor, at + 1, size);
            switch (prefix & 0xFC) {
                case USAGE_PAGE -> usagePage = value;
                case USAGE -> usages.add(size == 4 ? value : usagePage << 16 | value);
                case LOGICAL_MAXIMUM -> logicalMaximum = signed(value, size);
                case REPORT_SIZE -> reportSize = value & 0xFFFFFFFFL;
                case REPORT_ID -> {
                    reportId = value & 0xFF;
                    ids = true;
                }
                case REPORT_COUNT -> reportCount = value & 0xFFFFFFFFL;
                case FEATURE -> {
                    if (reportSize == 0 || reportSize > 32 || reportCount > MOST_REPORT_BITS) {
                        break;
                    }
                    final long start = featureBits.getOrDefault(reportId, 0L);
                    final long end = start + reportSize * reportCount;
                    if (end > MOST_REPORT_BITS) {
                        break;
                    }
                    if ((value & CONSTANT) == 0) {
                        for (int i = 0; i < reportCount; ++i) {
                            final int usage = usages.isEmpty() ? 0 : usages.get(Math.min(i, usages.size() - 1));
                            if (usage == CONTACT_COUNT_MAXIMUM) {
                                maxima.computeIfAbsent(reportId, id -> new ArrayList<>())
                                        .add(new long[] {start + i * reportSize, reportSize, logicalMaximum});
                            }
                        }
                    }
                    featureBits.put(reportId, end);
                    usages.clear();
                }
                case COLLECTION -> {
                    if (depth == 0 && value == APPLICATION && !usages.isEmpty()) {
                        final int application = usages.get(usages.size() - 1);
                        pad = pad || application == GAME_PAD || application == JOYSTICK;
                    }
                    ++depth;
                    usages.clear();
                }
                case END_COLLECTION -> {
                    depth = Math.max(0, depth - 1);
                    usages.clear();
                }
                case INPUT, OUTPUT -> usages.clear();
                default -> {
                }
            }
            at += 1 + size;
        }
        this.numbered = ids;
        this.gamepad = pad;
        for (Map.Entry<Integer, List<long[]>> entry : maxima.entrySet()) {
            final int id = entry.getKey();
            final int offset = ids ? 8 : 0;
            final byte[] report = new byte[(int) ((featureBits.get(id) + 7) / 8) + (ids ? 1 : 0)];
            if (ids) {
                report[0] = (byte) id;
            }
            for (long[] field : entry.getValue()) {
                writeBits(report, offset + field[0], (int) field[1], field[2]);
            }
            features.put(id, report);
        }
    }

    boolean numbered() {
        return numbered;
    }

    boolean gamepad() {
        return gamepad;
    }

    byte[] featureReport(int id) {
        final byte[] report = features.get(id);
        return report == null ? null : report.clone();
    }

    private static int littleEndian(byte[] bytes, int from, int size) {
        int value = 0;
        for (int i = 0; i < size; ++i) {
            value |= (bytes[from + i] & 0xFF) << (8 * i);
        }
        return value;
    }

    private static int signed(int value, int size) {
        return switch (size) {
            case 1 -> (byte) value;
            case 2 -> (short) value;
            default -> value;
        };
    }

    private static void writeBits(byte[] report, long bit, int size, long value) {
        for (int i = 0; i < size; ++i) {
            final long at = bit + i;
            final int index = (int) (at / 8);
            if (index < report.length && ((value >> i) & 1) != 0) {
                report[index] = (byte) (report[index] | (1 << (int) (at % 8)));
            }
        }
    }
}
