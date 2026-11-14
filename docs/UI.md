# The UI library

Falcon has a small Minecraft-style UI library (`falcon_ui`, headers in `include/falcon/ui/`,
sources in `src/ui/`). It gives you the game's buttons, sliders, text boxes, lists, tooltips and
screens, drawn with the game's own sprites and font, and it is the only thing the game's screens use
to build their interface.

```
falcon_base   assets, text/fonts, OpenGL, the window        (src/{util,assets,text,gfx,platform})
falcon_ui     widgets, screens, layout, tooltips            (src/ui)         depends on falcon_base only
falcon_core   the game: sound, packs, worlds, every screen  (src/{game,audio,packs,world})
```

The build enforces the layering: `falcon_ui` cannot include anything from the game. It learns what it
needs through one interface, `UiContext` (translations, the font, the backdrop behind menus), which
the game implements once (`GameUiContext`).

## Adding a button to a screen

A screen is a class with an `init()` that adds its widgets. Everything is positioned in GUI units
(the game's units: a 854x480 window is 427x240). `text(key)` translates, `width_`/`height_` are the
screen size.

```cpp
class GreetingScreen : public Screen {
public:
    using Screen::Screen;                       // (ScreenNavigator&)
protected:
    void init() override {
        addCenteredLabel(text("greeting.title"), width_ / 2, 40);
        auto row = layout::centeredRow(width_, 100, {metrics::kButtonWidth, metrics::kButtonWidth}, 8);
        addButton(text("gui.yes"), row[0], [this] { std::puts("yes"); onClose(); });
        addButton(text("gui.no"),  row[1], [this] { onClose(); });
    }
};
```

Open it from another screen's button: `navigator_.push(std::make_unique<GreetingScreen>(navigator_));`
(the game's screens use `openScreen<T>(navigator_, services_)`).

The helpers on `Screen`:

| call | adds |
| --- | --- |
| `addButton(label, rect, onPress)` | a standard button |
| `addIconButton(sprite, iconSize, rect, onPress)` | a square button showing a sprite |
| `addLockedButton(label, rect)` | a padlocked, disabled button (Telemetry Data) |
| `addLabel(text, x, y, rgb)` / `addCenteredLabel(text, centerX, y, rgb)` | white text with a shadow |
| `addEditBox(rect, hint, maxLength)` | a text box with a grey hint |
| `addCheckbox(label, x, y, checked, onChange)` | a tick box with its label to the right (the multiplayer warning) |
| `addSlider(rect, initial, labelFn, onChange)` | a 0..1 slider |
| `addWidget<T>(...)` | any other widget (`ScrollList`, your own) |

Every widget has `tooltip` (a string shown in the game's tooltip box on hover), `active`, `visible`.

## Layout

`falcon/ui/layout.hpp` and `falcon/ui/metrics.hpp`. Positions truncate like the game's layout code.

```cpp
layout::centeredX(width_, 200);                         // x that centres a 200 wide widget
layout::centeredRow(width_, y, {150, 150}, 8);          // two buttons side by side, centred
layout::footerRow(width_, height_, 2, 150);             // buttons in the Done strip at the bottom
layout::gridCell(width_, top, column, row);             // the Options screen's two-column grid
auto col = layout::Column::centered(width_, 108, 200);  // buttons stacked top to bottom:
Rect a = col.next(); Rect b = col.next(); col.skip(12);
```

`metrics::` has the sizes the menus are built from (`kButtonWidth` 150, `kWideButtonWidth` 200,
`kButtonHeight` 20, `kHeaderHeight` 33, `kRowHeight` 25, ...). Use them instead of bare numbers.

## A settings screen in a few lines

`ListScreen` is the title + scrolling rows + Done screen that Music & Sounds, Chat Settings and the
other settings screens share. List the rows in `addOptions()`:

```cpp
class MyScreen : public ListScreen {
public:
    explicit MyScreen(ScreenNavigator& n) : ListScreen(n, "my.title") {}   // translation key of the title
protected:
    void addOptions() override {
        addHeader(text("my.section"));
        addPair(makeToggle("my.fast", fast_, kSmallWidth, [this](bool on) { fast_ = on; }),
                makeChoice("my.mode", {"my.mode.a", "my.mode.b"}, mode_, kSmallWidth, [this](int i) { mode_ = i; }));
        addBig(makeIntSlider(1, 10, level_, kBigWidth,
                             [this](int v) { return optionLabel("my.level", std::to_string(v)); },
                             [this](int v) { level_ = v; }));
        addPair(withTooltip(makeSlider(...), "my.opacity.tooltip"), makeLink("my.more", kSmallWidth,
                [this] { return std::make_unique<MoreScreen>(navigator_); }));
    }
    bool fast_ = false; int mode_ = 0; int level_ = 5;
};
```

`makeToggle`, `makeChoice`, `makeSlider`, `makeIntSlider` and `makeLink` build the widgets;
`percentLabel`, `percentOrOffLabel`, `pixelLabel`, `numberOrOffLabel` and `optionLabel` build their
text ("Caption: 50%"). Override `addFooter()` to replace the Done button.

In the game, derive from `GameOptionsScreen` instead: it is a `ListScreen` that also has the game's
settings (`services_.options`) and adds `optionToggle(caption, &GameOptions::field)` and
`percentSlider(caption, &GameOptions::field)`, which bind a widget straight to a setting.

## Other building blocks

- `ConfirmScreen(navigator, title, message, yes, no, callback)` - a modal question; closes itself
  before calling `callback(bool)`.
- `ScrollList` - the selection list with scrollbar, keyboard and click handling (world and language
  lists, pack lists). Rows derive from `ScrollList::Entry`.
- `GuiGraphics` - drawing: `text(...)` in the Minecraft font (with Unifont fallback), `blitSprite`,
  `fill`, `blit`, scissor clipping (`pushScissor`/`popScissor` nest inside a list row's clip), tooltips.
- `Screen::onClose()` leaves the screen; override it for a screen that saves something.

## Wiring it into an application

Implement `UiContext` (`text`, `font`, `drawBackdrop`; optionally `drawBackdropFrom`, whose darkening starts
lower down, for a screen with its own header strip), implement `ScreenNavigator` (push/pop,
`ui()`), and call `screen->resize(w, h)` / `render(...)` each frame. The game does this in
`src/client.cpp` (`ClientNavigator`) and `include/falcon/game/game_ui.hpp` (`GameUiContext`).
