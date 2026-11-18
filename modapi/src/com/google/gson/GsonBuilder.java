package com.google.gson;

public final class GsonBuilder {
    boolean pretty, html = true, nulls;

    public GsonBuilder setPrettyPrinting() { pretty = true; return this; }
    public GsonBuilder disableHtmlEscaping() { html = false; return this; }
    public GsonBuilder serializeNulls() { nulls = true; return this; }
    public GsonBuilder setLenient() { return this; }
    public GsonBuilder registerTypeAdapter(Object type, Object adapter) { return this; }
    public Gson create() { return new Gson(pretty, html, nulls); }
}
