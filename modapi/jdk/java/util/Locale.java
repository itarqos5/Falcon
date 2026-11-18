package java.util;

public final class Locale {
    public static final Locale ROOT = new Locale("");
    public static final Locale ENGLISH = new Locale("en");
    public static final Locale US = new Locale("en", "US");
    public static final Locale GERMAN = new Locale("de");
    public static final Locale FRENCH = new Locale("fr");
    private final String language, country;

    public Locale(String language) { this(language, ""); }
    public Locale(String language, String country) { this.language = language; this.country = country; }

    public static Locale getDefault() { return US; }
    public static Locale forLanguageTag(String tag) { return new Locale(tag); }
    public String getLanguage() { return language; }
    public String getCountry() { return country; }
    @Override public String toString() { return country.isEmpty() ? language : language + "_" + country; }
}
