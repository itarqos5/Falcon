package net.minecraft.util;

public final class Mth {
    private Mth() {}

    public static final float PI = 3.1415927F;
    public static final float TWO_PI = 6.2831855F;
    public static final float HALF_PI = 1.5707964F;
    public static final float DEG_TO_RAD = 0.017453292F;
    public static final float RAD_TO_DEG = 57.295776F;
    public static final float EPSILON = 1.0E-5F;

    public static int floor(double d) { int i = (int) d; return d < (double) i ? i - 1 : i; }
    public static int floor(float f) { int i = (int) f; return f < (float) i ? i - 1 : i; }
    public static long lfloor(double d) { long l = (long) d; return d < (double) l ? l - 1L : l; }
    public static int ceil(double d) { int i = (int) d; return d > (double) i ? i + 1 : i; }
    public static int ceil(float f) { int i = (int) f; return f > (float) i ? i + 1 : i; }
    public static int clamp(int v, int min, int max) { return v < min ? min : Math.min(v, max); }
    public static long clamp(long v, long min, long max) { return v < min ? min : Math.min(v, max); }
    public static float clamp(float v, float min, float max) { return v < min ? min : Math.min(v, max); }
    public static double clamp(double v, double min, double max) { return v < min ? min : Math.min(v, max); }
    public static double clampedLerp(double a, double b, double t) { return t < 0.0 ? a : (t > 1.0 ? b : lerp(t, a, b)); }
    public static float lerp(float t, float a, float b) { return a + t * (b - a); }
    public static double lerp(double t, double a, double b) { return a + t * (b - a); }
    public static float lerp(double t, float a, float b) { return (float) (a + t * (b - a)); }
    public static double inverseLerp(double v, double a, double b) { return (v - a) / (b - a); }
    public static float inverseLerp(float v, float a, float b) { return (v - a) / (b - a); }
    public static float invSqrt(float f) { return (float) (1.0 / Math.sqrt(f)); }
    public static double invSqrt(double d) { return 1.0 / Math.sqrt(d); }
    public static int positiveModulo(int a, int b) { return Math.floorMod(a, b); }
    public static float positiveModulo(float a, float b) { return (a % b + b) % b; }
    public static double positiveModulo(double a, double b) { return (a % b + b) % b; }
    public static float sqrt(float f) { return (float) Math.sqrt(f); }
    public static float sin(float f) { return (float) Math.sin(f); }
    public static float cos(float f) { return (float) Math.cos(f); }
    public static float wrapDegrees(float d) { float v = d % 360.0F; if (v >= 180.0F) v -= 360.0F; if (v < -180.0F) v += 360.0F; return v; }
    public static double wrapDegrees(double d) { double v = d % 360.0; if (v >= 180.0) v -= 360.0; if (v < -180.0) v += 360.0; return v; }
    public static float degreesDifference(float a, float b) { return wrapDegrees(b - a); }
    public static int square(int i) { return i * i; }
    public static float square(float f) { return f * f; }
    public static double square(double d) { return d * d; }
    public static float abs(float f) { return Math.abs(f); }
    public static int abs(int i) { return Math.abs(i); }
    public static boolean equal(float a, float b) { return Math.abs(b - a) < 1.0E-5F; }
    public static boolean equal(double a, double b) { return Math.abs(b - a) < 1.0E-5F; }
    public static int smallestEncompassingPowerOfTwo(int v) { int i = v - 1; i |= i >> 1; i |= i >> 2; i |= i >> 4; i |= i >> 8; i |= i >> 16; return i + 1; }
    public static float frac(float f) { return f - floor(f); }
    public static double frac(double d) { return d - lfloor(d); }
    public static int hsvToRgb(float h, float s, float v) {
        int i = (int) (h * 6.0F) % 6;
        float f = h * 6.0F - (float) i, p = v * (1.0F - s), q = v * (1.0F - f * s), t = v * (1.0F - (1.0F - f) * s);
        float r, g, b;
        switch (i) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
        return (clamp((int) (r * 255.0F), 0, 255) << 16) | (clamp((int) (g * 255.0F), 0, 255) << 8) | clamp((int) (b * 255.0F), 0, 255);
    }
}
