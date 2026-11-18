// The game's screen as stand-ins: what a mod draws (GuiGraphics), the buttons it puts on a screen (Button, Screens), the keys it asks about (KeyMapping), where the player is and how big the
// window is. Nothing here draws or reads anything itself: a draw call becomes a DrawCmd in the capture, a button becomes a stand-in the client reads the label, place and handler of, and the
// questions about keys and position are answered from what the client lent the VM (HostState). The client then does the drawing, adds the buttons, and runs the handlers.
#include "falcon/jvm/vm.hpp"

#include "shim_util.hpp"

#include <algorithm>
#include <cmath>

namespace falcon::jvm {
namespace {

bool isGraphics(Object* o) { return shimClassHas(o, "GuiGraphics"); }

float px(Vm& v, double x) {
    const auto& p = v.host.pose.back();
    return static_cast<float>(x * p[2] + p[0]);
}
float py(Vm& v, double y) {
    const auto& p = v.host.pose.back();
    return static_cast<float>(y * p[3] + p[1]);
}

void emit(Vm& v, DrawCmd cmd) {
    if (v.capture.draw.size() < 20000) v.capture.draw.push_back(std::move(cmd));
}

int widthOf(Vm& v, const std::string& text) {
    if (v.host.textWidth) return v.host.textWidth(text);
    return static_cast<int>(text.size()) * 6;
}

std::uint32_t colorOf(const Args& g, std::size_t i) { return static_cast<std::uint32_t>(g.v[i].i); }

// drawString and drawCenteredString: the text is the second object (the font is the first), then x, y, the colour and maybe the shadow flag.
bool drawText(Vm& v, Value* a, const MethodRef& r, Value& result, bool centered) {
    if (!isGraphics(a[0].a)) return false;
    const Args g = unpack(a, r, false);
    const std::vector<std::size_t> obj = objectIndexes(g), num = numberIndexes(g);
    if (obj.size() < 2 || num.size() < 3) return true;
    DrawCmd c;
    c.kind = DrawCmd::Kind::Text;
    c.text = textOf(v, g.v[obj[1]].a);
    c.x0 = px(v, g.num(num[0]));
    c.y0 = py(v, g.num(num[1]));
    c.color = colorOf(g, num[2]);
    if ((c.color >> 24) == 0) c.color |= 0xFF000000u;  // (a colour without alpha is opaque for text)
    c.shadow = num.size() > 3 ? g.v[num[3]].i != 0 : true;
    c.centered = centered;
    c.scale = v.host.pose.back()[2];
    const int width = widthOf(v, c.text);
    emit(v, std::move(c));
    if (r.ret == 'I') result = valueOfInt(width);
    return true;
}

Object* buttonList(Vm& v, Object* screen) {
    Shim& shim = v.shimOf(screen);
    Object*& list = shim.fields["#buttons"];
    if (list == nullptr) list = v.newObject("java/util/ArrayList");
    return list;
}

}  // namespace

void installGameShims(Vm& vm) {
    // ---- the client, its window, the player ----
    vm.defineShimHandler("*.getInstance", [](Vm& v, Value*, const MethodRef& r, Value& result) {
        if (r.cls->name.find("Minecraft") == std::string::npos) return false;
        result = valueOfRef(v.client());
        return true;
    });
    for (const char* name : {"*.getGuiScaledWidth", "*.getScaledWidth"}) {
        vm.defineShimHandler(name, [](Vm& v, Value*, const MethodRef&, Value& result) {
            result = valueOfInt(v.host.guiWidth);
            return true;
        });
    }
    for (const char* name : {"*.getGuiScaledHeight", "*.getScaledHeight"}) {
        vm.defineShimHandler(name, [](Vm& v, Value*, const MethodRef&, Value& result) {
            result = valueOfInt(v.host.guiHeight);
            return true;
        });
    }
    const auto coordinate = [](int axis) {
        return [axis](Vm& v, Value* a, const MethodRef& r, Value& result) {
            if (!isPlayerLike(a[0].a)) return false;
            const double value = axis == 0 ? v.host.x : axis == 1 ? v.host.y : v.host.z;
            result = r.ret == 'D' ? valueOfDouble(value) : r.ret == 'F' ? valueOfFloat(static_cast<float>(value)) : r.ret == 'J' ? valueOfLong(static_cast<std::int64_t>(std::floor(value))) : valueOfInt(static_cast<int>(std::floor(value)));
            return true;
        };
    };
    for (const char* name : {"*.getX", "*.x"}) vm.defineShimHandler(name, coordinate(0));
    for (const char* name : {"*.getY", "*.y"}) vm.defineShimHandler(name, coordinate(1));
    for (const char* name : {"*.getZ", "*.z"}) vm.defineShimHandler(name, coordinate(2));
    for (const char* name : {"*.getYRot", "*.getYaw", "*.getXRot", "*.getPitch"}) {
        const bool yaw = std::string(name).find("Y") != std::string::npos;
        vm.defineShimHandler(name, [yaw](Vm& v, Value* a, const MethodRef&, Value& result) {
            if (!isPlayerLike(a[0].a)) return false;
            result = valueOfFloat(yaw ? v.host.yaw : v.host.pitch);
            return true;
        });
    }
    for (const char* name : {"*.blockPosition", "*.position", "*.getBlockPos", "*.getPosition", "*.getEyePosition"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef& r, Value& result) {
            if (!isPlayerLike(a[0].a) || r.ret != 'A') return false;
            const std::size_t close = r.desc.find(')');
            const std::string type = r.desc.substr(close + 2, r.desc.size() - close - 3);
            result = valueOfRef(v.makeShim(type, "playerPos"));
            return true;
        });
    }
    vm.defineShimHandler("*.getHealth", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        if (!isPlayerLike(a[0].a)) return false;
        result = valueOfFloat(static_cast<float>(v.host.health));
        return true;
    });

    // ---- a text's width ----
    vm.defineShimHandler("*.width", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (!shimClassHas(a[0].a, "Font") || r.params.size() != 1 || r.params[0] != 'A') return false;
        result = valueOfInt(widthOf(v, textOf(v, a[1].a)));
        return true;
    });
    vm.defineShimHandler("*.getString", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (!shimClassHas(a[0].a, "Component") || !r.params.empty()) return false;
        result = valueOfRef(v.newString(textOf(v, a[0].a)));
        return true;
    });
    vm.defineShimHandler("*.append", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (!shimClassHas(a[0].a, "Component") || r.params.size() != 1) return false;
        a[0].a->str += textOf(v, a[1].a);
        a[0].a->shim->origin.clear();  // (the text is made now)
        result = a[0];
        return true;
    });

    // ---- GuiGraphics: what the mod draws ----
    vm.defineShimHandler("*.fill", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> n = numberIndexes(g);
        if (n.size() < 5) return true;
        DrawCmd c;
        c.kind = DrawCmd::Kind::Fill;
        c.x0 = px(v, g.num(n[0]));
        c.y0 = py(v, g.num(n[1]));
        c.x1 = px(v, g.num(n[2]));
        c.y1 = py(v, g.num(n[3]));
        c.color = colorOf(g, n.back());
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.fillGradient", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> n = numberIndexes(g);
        if (n.size() < 6) return true;
        DrawCmd c;
        c.kind = DrawCmd::Kind::Gradient;
        c.x0 = px(v, g.num(n[0]));
        c.y0 = py(v, g.num(n[1]));
        c.x1 = px(v, g.num(n[2]));
        c.y1 = py(v, g.num(n[3]));
        c.color = colorOf(g, n[n.size() - 2]);
        c.color2 = colorOf(g, n.back());
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.hLine", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> n = numberIndexes(g);
        if (n.size() < 4) return true;
        DrawCmd c;
        c.x0 = px(v, g.num(n[0]));
        c.x1 = px(v, g.num(n[1]) + 1);
        c.y0 = py(v, g.num(n[2]));
        c.y1 = py(v, g.num(n[2]) + 1);
        c.color = colorOf(g, n.back());
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.vLine", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> n = numberIndexes(g);
        if (n.size() < 4) return true;
        DrawCmd c;
        c.x0 = px(v, g.num(n[0]));
        c.x1 = px(v, g.num(n[0]) + 1);
        c.y0 = py(v, g.num(n[1]));
        c.y1 = py(v, g.num(n[2]) + 1);
        c.color = colorOf(g, n.back());
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.renderOutline", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> n = numberIndexes(g);
        if (n.size() < 5) return true;
        const double x = g.num(n[0]), y = g.num(n[1]), w = g.num(n[2]), h = g.num(n[3]);
        const std::uint32_t color = colorOf(g, n.back());
        const double rects[4][4] = {{x, y, x + w, y + 1}, {x, y + h - 1, x + w, y + h}, {x, y + 1, x + 1, y + h - 1}, {x + w - 1, y + 1, x + w, y + h - 1}};
        for (const auto& q : rects) {
            DrawCmd c;
            c.x0 = px(v, q[0]);
            c.y0 = py(v, q[1]);
            c.x1 = px(v, q[2]);
            c.y1 = py(v, q[3]);
            c.color = color;
            emit(v, std::move(c));
        }
        return true;
    });
    vm.defineShimHandler("*.drawString", [](Vm& v, Value* a, const MethodRef& r, Value& result) { return drawText(v, a, r, result, false); });
    vm.defineShimHandler("*.drawCenteredString", [](Vm& v, Value* a, const MethodRef& r, Value& result) { return drawText(v, a, r, result, true); });
    vm.defineShimHandler("*.blit", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> obj = objectIndexes(g), n = numberIndexes(g);
        if (obj.empty() || n.size() < 8) return true;
        DrawCmd c;
        c.kind = DrawCmd::Kind::Blit;
        c.text = textOf(v, g.v[obj.back()].a);
        const double x = g.num(n[0]), y = g.num(n[1]);
        double u, vv, w, h, rw, rh, tw, th;
        if (n.size() >= 10 && g.kinds[n[2]] != 'F') {  // the old order: x y width height u v regionWidth regionHeight textureWidth textureHeight
            w = g.num(n[2]); h = g.num(n[3]); u = g.num(n[4]); vv = g.num(n[5]); rw = g.num(n[6]); rh = g.num(n[7]); tw = g.num(n[8]); th = g.num(n[9]);
        } else if (n.size() >= 10) {  // x y u v width height regionWidth regionHeight textureWidth textureHeight
            u = g.num(n[2]); vv = g.num(n[3]); w = g.num(n[4]); h = g.num(n[5]); rw = g.num(n[6]); rh = g.num(n[7]); tw = g.num(n[8]); th = g.num(n[9]);
        } else {  // x y u v width height textureWidth textureHeight
            u = g.num(n[2]); vv = g.num(n[3]); w = g.num(n[4]); h = g.num(n[5]); rw = w; rh = h; tw = g.num(n[6]); th = g.num(n[7]);
        }
        c.x0 = px(v, x);
        c.y0 = py(v, y);
        c.x1 = px(v, x + w);
        c.y1 = py(v, y + h);
        c.u = static_cast<float>(u);
        c.v = static_cast<float>(vv);
        c.regionW = static_cast<float>(rw);
        c.regionH = static_cast<float>(rh);
        c.texW = static_cast<int>(tw);
        c.texH = static_cast<int>(th);
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.blitSprite", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> obj = objectIndexes(g), n = numberIndexes(g);
        if (obj.empty() || n.size() < 4) return true;
        DrawCmd c;
        c.kind = DrawCmd::Kind::Blit;
        c.text = "sprite:" + textOf(v, g.v[obj.back()].a);
        c.x0 = px(v, g.num(n[0]));
        c.y0 = py(v, g.num(n[1]));
        c.x1 = px(v, g.num(n[0]) + g.num(n[2]));
        c.y1 = py(v, g.num(n[1]) + g.num(n[3]));
        emit(v, std::move(c));
        return true;
    });
    for (const char* name : {"*.renderItem", "*.renderFakeItem"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef& r, Value&) {
            if (!isGraphics(a[0].a)) return false;
            const Args g = unpack(a, r, false);
            const std::vector<std::size_t> obj = objectIndexes(g), n = numberIndexes(g);
            if (obj.empty() || n.size() < 2) return true;
            DrawCmd c;
            c.kind = DrawCmd::Kind::Item;
            c.text = itemIdOf(v, g.v[obj.back()].a);
            c.x0 = px(v, g.num(n[0]));
            c.y0 = py(v, g.num(n[1]));
            c.scale = v.host.pose.back()[2];
            if (!c.text.empty()) emit(v, std::move(c));
            return true;
        });
    }
    vm.defineShimHandler("*.enableScissor", [](Vm& v, Value* a, const MethodRef& r, Value&) {
        if (!isGraphics(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        const std::vector<std::size_t> n = numberIndexes(g);
        if (n.size() < 4) return true;
        DrawCmd c;
        c.kind = DrawCmd::Kind::ScissorOn;
        c.x0 = px(v, g.num(n[0]));
        c.y0 = py(v, g.num(n[1]));
        c.x1 = px(v, g.num(n[2]));
        c.y1 = py(v, g.num(n[3]));
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.disableScissor", [](Vm& v, Value* a, const MethodRef&, Value&) {
        if (!isGraphics(a[0].a)) return false;
        DrawCmd c;
        c.kind = DrawCmd::Kind::ScissorOff;
        emit(v, std::move(c));
        return true;
    });
    vm.defineShimHandler("*.guiWidth", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        if (!isGraphics(a[0].a)) return false;
        result = valueOfInt(v.host.guiWidth);
        return true;
    });
    vm.defineShimHandler("*.guiHeight", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        if (!isGraphics(a[0].a)) return false;
        result = valueOfInt(v.host.guiHeight);
        return true;
    });

    // ---- the pose (translate and scale only: all a HUD needs) ----
    for (const char* name : {"*.pushPose", "*.pushMatrix"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef&, Value& result) {
            if (!isPoseStack(a[0].a)) return false;
            v.host.pose.push_back(v.host.pose.back());
            result = a[0];
            return true;
        });
    }
    for (const char* name : {"*.popPose", "*.popMatrix"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef&, Value&) {
            if (!isPoseStack(a[0].a)) return false;
            if (v.host.pose.size() > 1) v.host.pose.pop_back();
            return true;
        });
    }
    vm.defineShimHandler("*.translate", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (!isPoseStack(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        if (g.size() >= 2) {
            auto& p = v.host.pose.back();
            p[0] += static_cast<float>(g.num(0)) * p[2];
            p[1] += static_cast<float>(g.num(1)) * p[3];
        }
        result = a[0];
        return true;
    });
    vm.defineShimHandler("*.scale", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (!isPoseStack(a[0].a)) return false;
        const Args g = unpack(a, r, false);
        if (g.size() >= 2) {
            auto& p = v.host.pose.back();
            p[2] *= static_cast<float>(g.num(0));
            p[3] *= static_cast<float>(g.num(1));
        }
        result = a[0];
        return true;
    });

    // ---- buttons ----
    // Button.builder(message, onPress).bounds(x, y, w, h).build(): the stand-in keeps the message and the handler, and the calls that follow (bounds, pos, size, width) are recorded as usual.
    vm.defineShimHandler("net/minecraft/client/gui/components/Button.builder", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        Object* b = v.makeShim("net/minecraft/client/gui/components/Button$Builder");
        Shim& s = v.shimOf(b);
        s.fields["message"] = a[0].a;
        s.fields["onPress"] = a[1].a;
        result = valueOfRef(b);
        return true;
    });
    vm.defineShimHandler("*.build", [](Vm& v, Value* a, const MethodRef&, Value& result) {
        if (shimClassHas(a[0].a, "FoodProperties$Builder")) {  // (what a food builder was given is read off it later)
            result = a[0];
            return true;
        }
        if (!shimClassHas(a[0].a, "Button$Builder")) return false;
        Object* button = v.makeShim("net/minecraft/client/gui/components/Button");
        v.shimOf(button).fields["builder"] = a[0].a;
        result = valueOfRef(button);
        return true;
    });
    vm.defineShimHandler("*.getButtons", [](Vm& v, Value* a, const MethodRef& r, Value& result) {
        if (r.cls->name.find("Screens") == std::string::npos || a[0].a == nullptr) return false;
        result = valueOfRef(buttonList(v, a[0].a));
        return true;
    });
    for (const char* name : {"*.addRenderableWidget", "*.addDrawableChild", "*.addRenderableOnly", "*.addWidget", "*.addDrawable", "*.addSelectableChild"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef& r, Value& result) {
            if (!shimClassHas(a[0].a, "Screen") || r.params.size() != 1 || a[1].a == nullptr) return false;
            buttonList(v, a[0].a)->elems.push_back(a[1]);
            result = a[1];
            return true;
        });
    }

    // ---- keys ----
    for (const char* name : {"*.consumeClick", "*.wasPressed"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef&, Value& result) {
            if (!shimClassHas(a[0].a, "KeyMapping") && !shimClassHas(a[0].a, "KeyBinding")) return false;
            result = valueOfInt(v.host.consumeClick && v.host.consumeClick(keyNameOf(a[0].a)) ? 1 : 0);
            return true;
        });
    }
    for (const char* name : {"*.isDown", "*.isPressed"}) {
        vm.defineShimHandler(name, [](Vm& v, Value* a, const MethodRef&, Value& result) {
            if (!shimClassHas(a[0].a, "KeyMapping") && !shimClassHas(a[0].a, "KeyBinding")) return false;
            result = valueOfInt(v.host.isDown && v.host.isDown(keyNameOf(a[0].a)) ? 1 : 0);
            return true;
        });
    }
}

}  // namespace falcon::jvm
