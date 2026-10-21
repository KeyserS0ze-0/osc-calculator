#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char *kModuleId = "osc_toolkit.calc";

std::string trim(std::string_view v) {
    size_t a = 0, b = v.size();
    while (a < b && std::isspace((unsigned char)v[a])) ++a;
    while (b > a && std::isspace((unsigned char)v[b - 1])) --b;
    return std::string(v.substr(a, b - a));
}

std::string fmt(double n) {
    char buf[64];
    if (std::fabs(n) < 1e15 && n == std::floor(n)) {
        std::snprintf(buf, sizeof buf, "%.0f", n);
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%.6f", n);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool parseNum(const std::string &t, double &out) {
    if (t.empty()) return false;
    char *end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

bool parseWhole(const std::string &t, long long &out, long long minV) {
    double d;
    if (!parseNum(t, d)) return false;
    if (d != std::floor(d) || std::fabs(d) > 9007199254740991.0) return false;
    out = (long long)d;
    return out >= minV;
}

std::string stackText(long long full, double extra) {
    std::string r = std::to_string(full) + (full == 1 ? " STACK" : " STACKS");
    if (extra > 0) r += " + " + fmt(extra) + " BLOCKS";
    return r;
}

struct Result {
    bool ok = true;
    std::vector<std::string> lines;
};

Result calcOsc(bool zAxis, const std::string &cannonT,
               const std::string &targetT, const std::string &stackT) {
    Result r;
    double cannon, target;
    long long stack;
    if (!parseNum(cannonT, cannon) || !parseNum(targetT, target) ||
        !parseWhole(stackT, stack, 1)) {
        r.ok = false;
        r.lines = {"ERROR",
                   "Enter valid coordinates and a positive",
                   "whole-number stack size."};
        return r;
    }
    double distance = std::fabs(target - cannon);
    long long full = (long long)std::floor(distance / (double)stack);
    double extra = distance - (double)full * (double)stack;
    extra = std::round(extra * 1e8) / 1e8;
    std::string main = distance == 0 ? "0 BLOCKS" : stackText(full, extra);
    r.lines = {std::string("OSC - ") + (zAxis ? "Z" : "X") + " AXIS",
               "Distance: " + fmt(distance) + " blocks",
               "Supply: " + main,
               "Full stacks: " + std::to_string(full),
               "Extra blocks: " + fmt(extra),
               "Per stack: " + std::to_string(stack)};
    return r;
}

Result calcBlocks(const std::string &stacksT, const std::string &extraT,
                  const std::string &sizeT) {
    Result r;
    long long stacks, extras, size;
    if (!parseWhole(stacksT, stacks, 0) || !parseWhole(extraT, extras, 0) ||
        !parseWhole(sizeT, size, 1)) {
        r.ok = false;
        r.lines = {"ERROR", "Use non-negative whole numbers and a",
                   "positive stack size."};
        return r;
    }
    long long prod, total;
    if (__builtin_mul_overflow(stacks, size, &prod) ||
        __builtin_add_overflow(prod, extras, &total) ||
        total > 9007199254740991LL) {
        r.ok = false;
        r.lines = {"ERROR", "That block amount is too large."};
        return r;
    }
    r.lines = {"BLOCK CALCULATOR",
               "Total: " + std::to_string(total) +
                   (total == 1 ? " block" : " blocks"),
               "Equivalent stacks: " + std::to_string(total / size),
               "Remainder: " + std::to_string(total % size) + " blocks"};
    return r;
}

} // namespace
class OscToolkitMod {
public:
    static OscToolkitMod &instance() {
        static OscToolkitMod inst;
        return inst;
    }

    OscToolkitMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load() {
        getSelf().getLogger().info("Loaded {}", getSelf().getName());
        return true;
    }

    bool enable() {
        using pl::modmenu::ConfigType;
        return pl::modmenu::ModuleBuilder(kModuleId, "OSC Toolkit")
            .modId(getSelf().getId())
            .description("OSC and block calculators")
            .defaultEnabled(true)
            .config("use_z", "Use Z axis (off = X)", ConfigType::Toggle, "false")
            .config("cannon", "Cannon location", ConfigType::Text, "")
            .config("target", "Target location", ConfigType::Text, "")
            .config("stack_size", "Blocks per stack", ConfigType::Text, "64")
            .config("calc_osc", "Calculate OSC", ConfigType::Button)
            .config("num_stacks", "Number of stacks", ConfigType::Text, "1")
            .config("extra_blocks", "Extra blocks", ConfigType::Text, "0")
            .config("block_stack_size", "Block calc: per stack", ConfigType::Text, "64")
            .config("calc_blocks", "Calculate blocks", ConfigType::Button)
            .config("hide_result", "Hide result", ConfigType::Button)
            .onToggle([this](std::string_view, bool enabled) {
                std::lock_guard<std::mutex> lock(mMutex);
                mVisible = enabled;
                if (!enabled) clearHud();
                else showHud();
            })
            .onConfigChanged([this](std::string_view, std::string_view key,
                                    std::string_view value) {
                onConfig(std::string(key), std::string(value));
            })
            .registerModule();
    }

    bool disable() {
        pl::modmenu::unregisterModule(kModuleId);
        return true;
    }

    bool unload() { return true; }

private:
    void onConfig(const std::string &key, const std::string &value) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (key == "use_z") mZ = (value == "true" || value == "1");
        else if (key == "cannon") mCannon = trim(value);
        else if (key == "target") mTarget = trim(value);
        else if (key == "stack_size") mStack = trim(value);
        else if (key == "num_stacks") mNumStacks = trim(value);
        else if (key == "extra_blocks") mExtra = trim(value);
        else if (key == "block_stack_size") mBStack = trim(value);
        else if (key == "calc_osc") {
            mResult = calcOsc(mZ, mCannon, mTarget, mStack);
            showHud();
        } else if (key == "calc_blocks") {
            mResult = calcBlocks(mNumStacks, mExtra, mBStack);
            showHud();
        } else if (key == "hide_result") {
            mResult.lines.clear();
            clearHud();
        }
    }

    void clearHud() {
        pl::modmenu::submitDrawCommands(kModuleId, {});
    }

    void showHud() {
        if (!mVisible || mResult.lines.empty()) return;
        using pl::modmenu::DrawCommand;
        using pl::modmenu::DrawCommandType;

        const float w = 360.0f, pad = 16.0f, titleH = 36.0f, rowH = 28.0f;
        const uint32_t accent = mResult.ok ? 0xFFA7E8B7 : 0xFFFF7777;
        const uint32_t labelCol = 0xFF9A9A9A, valueCol = 0xFFEEEEEE;

        // Split the result into title, main answer and detail rows
        std::string title = mResult.lines[0];
        std::string mainText;
        std::vector<std::pair<std::string, std::string>> rows;
        for (size_t i = 1; i < mResult.lines.size(); ++i) {
            const std::string &ln = mResult.lines[i];
            size_t p = ln.find(": ");
            std::string label = p == std::string::npos ? "" : ln.substr(0, p);
            std::string value = p == std::string::npos ? ln : ln.substr(p + 2);
            if (mResult.ok && mainText.empty() &&
                (label == "Supply" || label == "Total")) {
                mainText = value;
                continue;
            }
            rows.push_back({label, value});
        }

        const float mainH = mainText.empty() ? 0.0f : 64.0f;
        const float h = titleH + mainH + rowH * (float)rows.size() + pad;
        auto surf = pl::modmenu::getHudSurfaceSize();
        const float x = surf.width > w ? (surf.width - w) / 2.0f : 24.0f;
        const float y = 24.0f;

        std::vector<DrawCommand> cmds;
        cmds.push_back({.type = DrawCommandType::RectFilled,
                        .x = x, .y = y, .w = w, .h = h, .color = 0xE6121212});
        cmds.push_back({.type = DrawCommandType::Rect,
                        .x = x, .y = y, .w = w, .h = h,
                        .color = 0xFF2E3B33, .size = 1.0f});
        cmds.push_back({.type = DrawCommandType::RectFilled,
                        .x = x, .y = y, .w = w, .h = 4.0f, .color = accent});
        cmds.push_back({.type = DrawCommandType::Text,
                        .x = x + pad, .y = y + 12.0f, .color = labelCol,
                        .size = 16.0f, .text = title});

        if (!mainText.empty()) {
            float ms = 28.0f;
            const float maxW = w - 2.0f * pad;
            const float len = (float)mainText.size();
            if (len * ms * 0.55f > maxW) ms = maxW / (len * 0.55f);
            float tx = x + (w - len * ms * 0.55f) / 2.0f;
            if (tx < x + pad) tx = x + pad;
            cmds.push_back({.type = DrawCommandType::Text,
                            .x = tx, .y = y + titleH + 10.0f, .color = accent,
                            .size = ms, .text = mainText});
            cmds.push_back({.type = DrawCommandType::RectFilled,
                            .x = x + pad, .y = y + titleH + mainH,
                            .w = w - 2.0f * pad, .h = 1.0f, .color = 0xFF303030});
        }

        float ry = y + titleH + mainH + 8.0f;
        for (const auto &row : rows) {
            if (row.first.empty()) {
                cmds.push_back({.type = DrawCommandType::Text,
                                .x = x + pad, .y = ry, .color = valueCol,
                                .size = 16.0f, .text = row.second});
            } else {
                cmds.push_back({.type = DrawCommandType::Text,
                                .x = x + pad, .y = ry, .color = labelCol,
                                .size = 16.0f, .text = row.first});
                float vx = x + w - pad - (float)row.second.size() * 16.0f * 0.55f;
                cmds.push_back({.type = DrawCommandType::Text,
                                .x = vx, .y = ry, .color = valueCol,
                                .size = 16.0f, .text = row.second});
            }
            ry += rowH;
        }
        pl::modmenu::submitDrawCommands(kModuleId, cmds);
    }

    ll::mod::NativeMod &mSelf;
    std::mutex mMutex;
    bool mVisible = true;
    bool mZ = false;
    std::string mCannon, mTarget, mStack = "64";
    std::string mNumStacks = "1", mExtra = "0", mBStack = "64";
    Result mResult;
};

PL_REGISTER_MOD(OscToolkitMod, OscToolkitMod::instance())
