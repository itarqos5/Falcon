package com.example.test;

// What the VM is timed and tested on: plain arithmetic in a loop, a loop that never ends and a recursion that never returns (the VM stops both).
public final class Bench {
    public static long spin(int n) {
        long acc = 0;
        for (int i = 0; i < n; i++) acc += (i ^ (i >> 3)) * 31L + (acc & 7);
        return acc;
    }

    public static int forever() {
        int x = 0;
        while (true) x++;
    }

    public static int recurse(int n) { return recurse(n + 1) + 1; }
}
