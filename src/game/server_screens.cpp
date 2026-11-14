#include "falcon/game/server_screens.hpp"

#include "falcon/util/server_address.hpp"
#include "falcon/util/text_util.hpp"

namespace falcon {
namespace {

constexpr std::uint32_t kLabelGrey = 0xA0A0A0u;  // the captions above the text boxes
constexpr std::size_t kAddressMaxLength = 128;
constexpr int kKeyEnter = 257;
constexpr int kKeypadEnter = 335;

}  // namespace

// ---- Add / Edit Server -----------------------------------------------------------------------

ManageServerScreen::ManageServerScreen(ScreenNavigator& navigator, ScreenServices& services, std::string titleKey,
                                       ServerEntry server, Done done)
    : Screen(navigator),
      services_(services),
      titleKey_(std::move(titleKey)),
      server_(std::move(server)),
      done_(std::move(done)),
      nameText_(server_.name),
      ipText_(server_.ip) {}

void ManageServerScreen::init() {
    const int left = width_ / 2 - 100;
    auto& nameEdit = addEditBox({left, 66, 200, 20}, text("selectServer.defaultName"), 32);
    nameEdit.setText(nameText_);
    nameEdit.onChange = [this](const std::string& value) { nameText_ = value; };

    auto& ip = addEditBox({left, 106, 200, 20}, "", kAddressMaxLength);
    ip.setText(ipText_);
    ipEdit_ = &ip;
    ip.onChange = [this](const std::string& value) {
        ipText_ = value;
        if (doneButton_ != nullptr) doneButton_->active = isValidServerAddress(ipText_);
    };

    // "Server Resource Packs: Prompt": Enabled / Disabled / Prompt, applied to the copy being edited.
    const auto packLabel = [this](int i) {
        static constexpr const char* kKeys[] = {"manageServer.resourcePack.enabled", "manageServer.resourcePack.disabled",
                                                "manageServer.resourcePack.prompt"};
        return formatLanguageString(text("options.generic_value"), {text("manageServer.resourcePack"), text(kKeys[i])});
    };
    auto& packs = addWidget<CycleButton>(left, height_ / 4 + 72, 200, 20, 3, static_cast<int>(server_.packStatus), packLabel,
                                         [this] { return navigator_.isShiftDown(); });
    packs.onChange = [this](int i) { server_.packStatus = static_cast<ServerPackStatus>(i); };

    doneButton_ = &addButton(text("gui.done"), {left, height_ / 4 + 114, 200, 20}, [this] { finish(true); });
    doneButton_->active = isValidServerAddress(ipText_);
    addButton(text("gui.cancel"), {left, height_ / 4 + 138, 200, 20}, [this] { finish(false); });

    setFocus(&nameEdit);  // typing starts in the name box
}

void ManageServerScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    // The title and the captions go under the widgets, which are all added after them.
    const int left = width_ / 2 - 100;
    const std::string title = text(titleKey_);
    graphics.text(title, static_cast<float>(width_ / 2 - graphics.textWidth(title) / 2), 17.0f, kWhite);
    graphics.text(text("manageServer.enterName"), static_cast<float>(left + 1), 53.0f, 0xFF000000u | kLabelGrey);
    graphics.text(text("manageServer.enterIp"), static_cast<float>(left + 1), 94.0f, 0xFF000000u | kLabelGrey);
    Screen::render(graphics, mouseX, mouseY, time);
}

void ManageServerScreen::finish(bool saved) {
    if (saved) {
        if (!isValidServerAddress(ipText_)) return;
        server_.name = nameText_.empty() ? text("selectServer.defaultName") : nameText_;  // as typed: not trimmed
        server_.ip = ipText_;
    }
    navigator_.pop();
    if (done_) done_(saved, server_);
}

// ---- Direct Connection -----------------------------------------------------------------------

DirectJoinScreen::DirectJoinScreen(ScreenNavigator& navigator, ScreenServices& services, Joined joined)
    : Screen(navigator), services_(services), joined_(std::move(joined)) {}

void DirectJoinScreen::init() {
    const int left = width_ / 2 - 100;
    auto& ip = addEditBox({left, 116, 200, 20}, "", kAddressMaxLength);
    ip.setText(services_.options.lastServer);
    ipEdit_ = &ip;
    ip.onChange = [this](const std::string& value) {
        services_.options.lastServer = value;  // remembered for next time, valid or not
        if (joinButton_ != nullptr) joinButton_->active = isValidServerAddress(value);
    };

    joinButton_ = &addButton(text("selectServer.select"), {left, height_ / 4 + 108, 200, 20}, [this] { join(); });
    joinButton_->active = isValidServerAddress(ip.text());
    addButton(text("gui.cancel"), {left, height_ / 4 + 132, 200, 20}, [this] { onClose(); });
    setFocus(&ip);
}

void DirectJoinScreen::join() {
    if (ipEdit_ == nullptr || !isValidServerAddress(ipEdit_->text())) return;
    const std::string address = ipEdit_->text();
    navigator_.pop();
    if (joined_) joined_(address);
}

bool DirectJoinScreen::keyPressed(int key, int mods) {
    // Enter in the address box joins, when the Join Server button would be enabled.
    if ((key == kKeyEnter || key == kKeypadEnter) && joinButton_ != nullptr && joinButton_->active && ipEdit_ != nullptr &&
        ipEdit_->focused) {
        join();
        return true;
    }
    return Screen::keyPressed(key, mods);
}

void DirectJoinScreen::render(GuiGraphics& graphics, int mouseX, int mouseY, const FrameTime& time) {
    ui().drawBackdrop(graphics, width_, height_, time.deltaTicks);
    const int left = width_ / 2 - 100;
    const std::string title = text("selectServer.direct");
    graphics.text(title, static_cast<float>(width_ / 2 - graphics.textWidth(title) / 2), 20.0f, kWhite);
    graphics.text(text("manageServer.enterIp"), static_cast<float>(left + 1), 100.0f, 0xFF000000u | kLabelGrey);
    Screen::render(graphics, mouseX, mouseY, time);
}

}  // namespace falcon
