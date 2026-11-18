package com.example.test;

final class Counter {
    private static int count = 100;
    static int next() { return ++count - 100; }
}
