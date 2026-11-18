package com.google.gson;

public final class LazilyParsedNumber extends Number {
    private final String text;

    public LazilyParsedNumber(String text) { this.text = text; }

    @Override public int intValue() { try { return Integer.parseInt(text); } catch (NumberFormatException e) { try { return (int) Long.parseLong(text); } catch (NumberFormatException e2) { return (int) Double.parseDouble(text); } } }
    @Override public long longValue() { try { return Long.parseLong(text); } catch (NumberFormatException e) { return (long) Double.parseDouble(text); } }
    @Override public float floatValue() { return Float.parseFloat(text); }
    @Override public double doubleValue() { return Double.parseDouble(text); }
    @Override public String toString() { return text; }
}
