package net.minecraft.world.food;

public record FoodProperties(int nutrition, float saturation, boolean canAlwaysEat) {
    public static class Builder {
        private int nutrition;
        private float saturation;
        private boolean always;

        public Builder nutrition(int n) { nutrition = n; return this; }
        public Builder saturationModifier(float modifier) { saturation = nutrition * modifier * 2.0F; return this; }
        public Builder alwaysEdible() { always = true; return this; }
        public FoodProperties build() { return new FoodProperties(nutrition, saturation, always); }
    }
}
