package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.jupiter.api.Test;

class ReedSolomonTest {

    private static JSONArray vectors() throws IOException {
        final Path directory = Path.of(System.getProperty("gs.protocol.vectorDir", "../../protocol/vectors"));
        final String text = Files.readString(directory.resolve("fec").resolve("reed-solomon.json"), StandardCharsets.UTF_8);
        return new JSONObject(text).getJSONArray("vectors");
    }

    private static byte[][] copy(byte[][] shards) {
        final byte[][] result = new byte[shards.length][];
        for (int i = 0; i < shards.length; ++i) {
            result[i] = shards[i].clone();
        }
        return result;
    }

    private static boolean[] all(int count) {
        final boolean[] present = new boolean[count];
        Arrays.fill(present, true);
        return present;
    }

    @Test
    void goldenVectorsGiveTheParityByteForByteAndTheDataBackFromEveryListedLoss() throws IOException {
        final JSONArray list = vectors();
        assertTrue(list.length() >= 10);
        for (int v = 0; v < list.length(); ++v) {
            final JSONObject vector = list.getJSONObject(v);
            final String name = vector.getString("name");
            final int k = vector.getInt("k");
            final int m = vector.getInt("m");
            final JSONArray data = vector.getJSONArray("data");
            final int length = Json.fromHex(data.getString(0)).length;
            final byte[][] shards = new byte[k + m][];
            for (int d = 0; d < k; ++d) {
                shards[d] = Json.fromHex(data.getString(d));
            }
            for (int i = 0; i < m; ++i) {
                shards[k + i] = new byte[length];
                Arrays.fill(shards[k + i], (byte) 0xAA);
            }
            final ReedSolomon code = new ReedSolomon(k, m);
            code.encode(shards, length);
            for (int i = 0; i < m; ++i) {
                assertEquals(vector.getJSONArray("parity").getString(i), Json.toHex(shards[k + i]), name);
            }

            final JSONArray erasures = vector.getJSONArray("erasures");
            for (int e = 0; e < erasures.length(); ++e) {
                final JSONArray lost = erasures.getJSONArray(e);
                final byte[][] damaged = copy(shards);
                final boolean[] present = all(k + m);
                for (int l = 0; l < lost.length(); ++l) {
                    present[lost.getInt(l)] = false;
                    Arrays.fill(damaged[lost.getInt(l)], (byte) 0x5C);
                }
                assertTrue(code.reconstruct(damaged, present, length), name);
                for (int d = 0; d < k; ++d) {
                    assertArrayEquals(shards[d], damaged[d], name + " losing " + lost);
                }
            }
        }
    }

    @Test
    void anyLossUpToTheParityCountIsRecoveredAndAGreaterOneRefused() {
        final Random random = new Random(2026);
        for (int round = 0; round < 300; ++round) {
            final int k = 1 + random.nextInt(64);
            final int m = random.nextInt(17);
            final int length = 1 + random.nextInt(300);
            final byte[][] shards = new byte[k + m][length];
            for (int d = 0; d < k; ++d) {
                random.nextBytes(shards[d]);
            }
            final ReedSolomon code = new ReedSolomon(k, m);
            code.encode(shards, length);

            final List<Integer> order = new ArrayList<>();
            for (int i = 0; i < k + m; ++i) {
                order.add(i);
            }
            Collections.shuffle(order, random);
            final int lostCount = random.nextInt(m + 1);
            final byte[][] damaged = copy(shards);
            final boolean[] present = all(k + m);
            for (int i = 0; i < lostCount; ++i) {
                present[order.get(i)] = false;
                Arrays.fill(damaged[order.get(i)], (byte) 0);
            }
            assertTrue(code.reconstruct(damaged, present, length));
            for (int d = 0; d < k; ++d) {
                assertArrayEquals(shards[d], damaged[d]);
            }

            final boolean[] tooFew = all(k + m);
            for (int i = 0; i <= m; ++i) {
                tooFew[order.get(i)] = false;
            }
            assertFalse(code.reconstruct(copy(shards), tooFew, length));
        }
    }
}
