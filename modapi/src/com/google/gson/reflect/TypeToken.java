package com.google.gson.reflect;

import java.lang.reflect.Type;
import net.falcon.jdk.TypeImpl;

public class TypeToken<T> {
    private final Type type;

    protected TypeToken() {
        String sig = net.falcon.Reflect.classSignature(this);
        // "Lcom/google/gson/reflect/TypeToken<Ljava/util/List<Lcom/x/Foo;>;>;": the first argument is what is asked for
        TypeImpl self = TypeImpl.parse(sig.isEmpty() ? "Ljava/lang/Object;" : superOf(sig));
        this.type = self.args.length > 0 ? self.args[0] : TypeImpl.of("java/lang/Object");
    }

    private static String superOf(String sig) {
        int i = sig.indexOf("Lcom/google/gson/reflect/TypeToken<");
        return i < 0 ? "Ljava/lang/Object;" : sig.substring(i);
    }

    public final Type getType() { return type; }
}
