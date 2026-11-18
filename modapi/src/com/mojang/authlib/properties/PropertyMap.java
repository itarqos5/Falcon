package com.mojang.authlib.properties;

import java.util.HashMap;

public class PropertyMap extends HashMap<String, Object> {
    public Iterable<Object> values2() { return values(); }
}
