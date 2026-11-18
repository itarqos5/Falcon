package net.falcon.jdk;

import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;

// A type as a generic signature says it: a class name and its arguments.
public final class TypeImpl implements ParameterizedType {
    public final String raw;  // "java.util.List"
    public final TypeImpl[] args;

    public TypeImpl(String raw, TypeImpl[] args) { this.raw = raw; this.args = args; }

    public static TypeImpl of(String className) { return new TypeImpl(className.replace('/', '.'), new TypeImpl[0]); }
    @Override public Type[] getActualTypeArguments() { return args; }
    @Override public Type getRawType() { return this; }
    @Override public String toString() { return raw; }

    // From a descriptor ("I", "Ljava/lang/String;", "[I") or a generic signature ("Ljava/util/List<Ljava/lang/String;>;").
    public static TypeImpl parse(String sig) {
        if (sig == null || sig.isEmpty()) return of("java/lang/Object");
        int[] at = {0};
        return parse(sig, at);
    }

    private static TypeImpl parse(String s, int[] at) {
        char c = s.charAt(at[0]);
        switch (c) {
            case 'I': at[0]++; return of("int");
            case 'J': at[0]++; return of("long");
            case 'F': at[0]++; return of("float");
            case 'D': at[0]++; return of("double");
            case 'Z': at[0]++; return of("boolean");
            case 'B': at[0]++; return of("byte");
            case 'S': at[0]++; return of("short");
            case 'C': at[0]++; return of("char");
            case '[': { at[0]++; TypeImpl e = parse(s, at); return new TypeImpl("[" + e.raw, new TypeImpl[] {e}); }
            case 'T': { while (s.charAt(at[0]) != ';') at[0]++; at[0]++; return of("java/lang/Object"); }
            case '*': at[0]++; return of("java/lang/Object");
            case '+': case '-': at[0]++; return parse(s, at);
            default: break;
        }
        at[0]++;  // L
        int start = at[0];
        while (s.charAt(at[0]) != ';' && s.charAt(at[0]) != '<') at[0]++;
        String name = s.substring(start, at[0]);
        List<TypeImpl> args = new ArrayList<>();
        if (s.charAt(at[0]) == '<') {
            at[0]++;
            while (s.charAt(at[0]) != '>') args.add(parse(s, at));
            at[0]++;
        }
        at[0]++;  // ;
        return new TypeImpl(name.replace('/', '.'), args.toArray(new TypeImpl[0]));
    }
}
