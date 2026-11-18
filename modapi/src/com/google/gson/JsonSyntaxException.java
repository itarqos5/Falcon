package com.google.gson;

public class JsonSyntaxException extends JsonParseException {
    public JsonSyntaxException(String message) { super(message); }
    public JsonSyntaxException(Throwable cause) { super(cause); }
}
