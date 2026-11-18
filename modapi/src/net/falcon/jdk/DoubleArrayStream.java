package net.falcon.jdk;

import java.util.OptionalDouble;
import java.util.stream.DoubleStream;

public final class DoubleArrayStream implements DoubleStream {
    private final double[] values;

    public DoubleArrayStream(double[] values) { this.values = values; }

    @Override public double sum() { double s = 0; for (double v : values) s += v; return s; }
    @Override public double max2() { double m = Double.NEGATIVE_INFINITY; for (double v : values) { if (v > m) m = v; } return m; }
    @Override public OptionalDouble average() { return values.length == 0 ? OptionalDouble.empty() : OptionalDouble.of(sum() / values.length); }
    @Override public long count() { return values.length; }
}
