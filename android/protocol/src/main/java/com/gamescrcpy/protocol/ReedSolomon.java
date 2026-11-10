package com.gamescrcpy.protocol;

import java.util.Arrays;

public final class ReedSolomon {

    public static final int MAX_SHARDS = 256;

    private static final byte[] EXP = new byte[512];
    private static final int[] LOG = new int[256];

    static {
        int value = 1;
        for (int power = 0; power < 255; ++power) {
            EXP[power] = (byte) value;
            LOG[value] = power;
            value <<= 1;
            if ((value & 0x100) != 0) {
                value ^= 0x11D;
            }
        }
        for (int power = 255; power < 512; ++power) {
            EXP[power] = EXP[power - 255];
        }
    }

    private final int dataShards;
    private final int parityShards;
    private final byte[] cauchy;

    public ReedSolomon(int dataShards, int parityShards) {
        if (dataShards < 1 || parityShards < 0 || dataShards + parityShards > MAX_SHARDS) {
            throw new IllegalArgumentException(dataShards + " + " + parityShards + " shards (SPEC 10.4)");
        }
        this.dataShards = dataShards;
        this.parityShards = parityShards;
        this.cauchy = new byte[dataShards * parityShards];
        for (int i = 0; i < parityShards; ++i) {
            for (int j = 0; j < dataShards; ++j) {
                cauchy[i * dataShards + j] = (byte) invert((dataShards + i) ^ j);
            }
        }
    }

    public int dataShards() {
        return dataShards;
    }

    public int parityShards() {
        return parityShards;
    }

    public void encode(byte[][] shards, int length) {
        for (int i = 0; i < parityShards; ++i) {
            final byte[] out = shards[dataShards + i];
            Arrays.fill(out, 0, length, (byte) 0);
            for (int j = 0; j < dataShards; ++j) {
                addScaled(out, shards[j], cauchy[i * dataShards + j] & 0xFF, length);
            }
        }
    }

    public boolean reconstruct(byte[][] shards, boolean[] present, int length) {
        boolean dataMissing = false;
        for (int d = 0; d < dataShards; ++d) {
            dataMissing |= !present[d];
        }
        if (!dataMissing) {
            return true;
        }
        final int k = dataShards;
        final int[] used = new int[k];
        int count = 0;
        for (int i = 0; i < shards.length && count < k; ++i) {
            if (present[i]) {
                used[count++] = i;
            }
        }
        if (count < k) {
            return false;
        }
        final int[] matrix = new int[k * k];
        final int[] inverse = new int[k * k];
        for (int t = 0; t < k; ++t) {
            if (used[t] < k) {
                matrix[t * k + used[t]] = 1;
            } else {
                for (int c = 0; c < k; ++c) {
                    matrix[t * k + c] = cauchy[(used[t] - k) * k + c] & 0xFF;
                }
            }
            inverse[t * k + t] = 1;
        }
        for (int column = 0; column < k; ++column) {
            int pivot = column;
            while (matrix[pivot * k + column] == 0) {
                ++pivot;
            }
            if (pivot != column) {
                swapRows(matrix, k, pivot, column);
                swapRows(inverse, k, pivot, column);
            }
            final int scale = invert(matrix[column * k + column]);
            for (int c = 0; c < k; ++c) {
                matrix[column * k + c] = multiply(scale, matrix[column * k + c]);
                inverse[column * k + c] = multiply(scale, inverse[column * k + c]);
            }
            for (int r = 0; r < k; ++r) {
                final int factor = matrix[r * k + column];
                if (r == column || factor == 0) {
                    continue;
                }
                for (int c = 0; c < k; ++c) {
                    matrix[r * k + c] ^= multiply(factor, matrix[column * k + c]);
                    inverse[r * k + c] ^= multiply(factor, inverse[column * k + c]);
                }
            }
        }
        for (int d = 0; d < k; ++d) {
            if (present[d]) {
                continue;
            }
            final byte[] out = shards[d];
            Arrays.fill(out, 0, length, (byte) 0);
            for (int t = 0; t < k; ++t) {
                addScaled(out, shards[used[t]], inverse[d * k + t], length);
            }
        }
        return true;
    }

    private static int multiply(int a, int b) {
        return a == 0 || b == 0 ? 0 : EXP[LOG[a] + LOG[b]] & 0xFF;
    }

    private static int invert(int a) {
        return EXP[255 - LOG[a]] & 0xFF;
    }

    private static void addScaled(byte[] out, byte[] in, int coefficient, int length) {
        if (coefficient == 0) {
            return;
        }
        final int scale = LOG[coefficient];
        for (int b = 0; b < length; ++b) {
            final int value = in[b] & 0xFF;
            if (value != 0) {
                out[b] ^= EXP[scale + LOG[value]];
            }
        }
    }

    private static void swapRows(int[] matrix, int width, int a, int b) {
        for (int c = 0; c < width; ++c) {
            final int held = matrix[a * width + c];
            matrix[a * width + c] = matrix[b * width + c];
            matrix[b * width + c] = held;
        }
    }
}
