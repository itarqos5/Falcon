package org.joml;

public class Vector3f {
    public float x, y, z;

    public Vector3f() {}
    public Vector3f(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
    public float x() { return x; }
    public float y() { return y; }
    public float z() { return z; }
    public Vector3f set(float x, float y, float z) { this.x = x; this.y = y; this.z = z; return this; }
    public Vector3f add(float dx, float dy, float dz) { x += dx; y += dy; z += dz; return this; }
    public Vector3f mul(float s) { x *= s; y *= s; z *= s; return this; }
    public float length() { return (float) Math.sqrt(x * x + y * y + z * z); }
    public Vector3f normalize() { float l = length(); if (l > 0.0F) { x /= l; y /= l; z /= l; } return this; }
}
