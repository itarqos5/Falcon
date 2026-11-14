#include "apex/game/sound_options_screen.hpp"

#include "apex/util/text_util.hpp"

namespace apex {

SoundOptionsScreen::SoundOptionsScreen(ScreenNavigator& navigator, ScreenServices& services)
    : GameOptionsScreen(navigator, services, "options.sounds.title") {}

std::string SoundOptionsScreen::volumeLabel(SoundSource source, double value) const {
    const std::string caption = text(soundSourceLangKey(source));
    if (value == 0.0) {  // exactly zero reads "OFF"; tiny values still show a percentage
        return formatLanguageString(text("options.generic_value"), {caption, text("options.off")});
    }
    return formatLanguageString(text("options.percent_value"),
                                {caption, std::to_string(static_cast<int>(value * 100.0))});
}

std::vector<std::string> buildDeviceChoices(const std::vector<AudioDevice>& devices) {
    std::vector<std::string> choices = {""};
    if (devices.size() > 1) {
        for (const AudioDevice& device : devices) choices.push_back(device.name);
    }
    return choices;
}

std::string SoundOptionsScreen::deviceLabel(int choice) const {
    const auto index = static_cast<std::size_t>(choice);
    if (index == 0 || index >= deviceChoices_.size()) {
        return optionLabel("options.audioDevice", text("options.audioDevice.default"));
    }
    // Device names can be long: cut at a character boundary and add "..." so the label fits.
    std::string name = deviceChoices_[index];
    const int room = kBigWidth - 8 - services_.font.width(optionLabel("options.audioDevice", ""));
    if (services_.font.width(name) > room) {
        while (!name.empty() && services_.font.width(name + "...") > room) {
            std::size_t n = name.size();  // drop the last code point: its continuation bytes, then its lead
            do {
                --n;
            } while (n > 0 && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80);
            name.resize(n);
        }
        name += "...";
    }
    return optionLabel("options.audioDevice", name);
}

void SoundOptionsScreen::syncDeviceButton() {
    AudioEngine& engine = services_.sounds.engine();
    deviceListVersion_ = engine.deviceListVersion();
    deviceChoices_ = buildDeviceChoices(engine.devices());
    // Keep showing the player's choice if it is still plugged in, else "System Default".
    int selected = 0;
    for (std::size_t i = 1; i < deviceChoices_.size(); ++i) {
        if (deviceChoices_[i] == services_.options.audioDevice) selected = static_cast<int>(i);
    }
    deviceButton_->setValues(static_cast<int>(deviceChoices_.size()), selected);
}

void SoundOptionsScreen::addOptions() {
    GameOptions& options = services_.options;

    const auto volumeSlider = [&](SoundSource source, int w) {
        auto slider = std::make_unique<SliderWidget>(
            0, 0, w, kWidgetHeight, options.volume(source),
            [this, source](double v) { return volumeLabel(source, v); });
        slider->onValueChanged = [this, source](double v) {
            services_.options.setVolume(source, v);
            services_.sounds.onVolumeChanged(source, v);  // live gain + preview sound
        };
        return slider;
    };

    // Master volume, then the other categories two per row in the game's order.
    addBig(volumeSlider(SoundSource::Master, kBigWidth));
    constexpr SoundSource kPairs[][2] = {
        {SoundSource::Music, SoundSource::Records},   {SoundSource::Weather, SoundSource::Blocks},
        {SoundSource::Hostile, SoundSource::Neutral}, {SoundSource::Players, SoundSource::Ambient},
        {SoundSource::Voice, SoundSource::Ui},
    };
    for (const auto& pair : kPairs) addPair(volumeSlider(pair[0], kSmallWidth), volumeSlider(pair[1], kSmallWidth));

    // Device: System Default, then every output the OS reports. The list is gathered in the
    // background (kicked off here and at startup), so the button fills in a moment later.
    services_.sounds.engine().refreshDevices();
    auto device = std::make_unique<CycleButton>(0, 0, kBigWidth, kWidgetHeight, 1, 0,
                                                [this](int i) { return deviceLabel(i); }, shiftDown());
    device->onChange = [this](int i) {
        const std::string& choice = deviceChoices_[static_cast<std::size_t>(i)];
        services_.options.audioDevice = choice;
        services_.sounds.engine().selectDevice(choice);
    };
    deviceButton_ = device.get();
    addBig(std::move(device));
    syncDeviceButton();

    addPair(makeToggle("options.showSubtitles", options.showSubtitles, kSmallWidth,
                       [this](bool on) { services_.options.showSubtitles = on; }),
            makeToggle("options.directionalAudio", options.directionalAudio, kSmallWidth, [this](bool on) {
                services_.options.directionalAudio = on;
                navigator_.playButtonClick();  // the game reloads sounds and clicks again
            }));

    auto frequency = makeChoice("options.music_frequency",
                                {"options.music_frequency.default", "options.music_frequency.frequent",
                                 "options.music_frequency.constant"},
                                static_cast<int>(options.musicFrequency), kSmallWidth, [this](int i) {
                                    services_.options.musicFrequency = static_cast<MusicFrequency>(i);
                                    services_.music.frequencyChanged();
                                });
    auto toast = makeChoice("options.musicToast",
                            {"options.musicToast.never", "options.musicToast.pauseMenu",
                             "options.musicToast.pauseMenuAndToast"},
                            static_cast<int>(options.musicToast), kSmallWidth,
                            [this](int i) { services_.options.musicToast = static_cast<MusicToast>(i); });
    addPair(std::move(frequency), std::move(toast));
}

void SoundOptionsScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    // The device list is gathered on a background thread; pick it up when it arrives.
    if (deviceButton_ != nullptr && services_.sounds.engine().deviceListVersion() != deviceListVersion_) {
        syncDeviceButton();
    }
    GameOptionsScreen::render(graphics, mouseX, mouseY, time);
}

}  // namespace apex
