#include "app.h"

#include "core/mii_parts.h"
#include "core/identity.h"
#include "core/log.h"
#include "core/util.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {
    constexpr float kToastLife = 6.0f;

    // control strip: 88px, --bg-1, controls right-aligned.
    constexpr float kHintBarHeight = 88.0f;

    struct TabInfo {
        ui::Icon icon;
        const char* label;
    };

    constexpr TabInfo kTabs[static_cast<int>(Tab::Count)] = {
        { ui::Icon::Inbox, "Plaza" },
        { ui::Icon::Radar, "Nearby" },
        { ui::Icon::Grid, "Collection" },
        { ui::Icon::Person, "Your pass" },
        { ui::Icon::Sliders, "Settings" },
    };
}

bool App::init()
{
    appletLockExit();
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);

    if (!m_gpu.init())
        return false;
    if (!m_font.init(m_gpu))
        return false;
    if (!m_renderer.init(m_gpu, m_font))
        return false;

    // The Mii artwork. A face draws nothing without it, so this is a hard
    // failure rather than something to discover on the passport screen.
    if (!miiParts().load("romfs:/mii/parts.bin"))
        return false;

    m_input.init(Renderer::DesignWidth, Renderer::DesignHeight);

    // A busy screen declares on the order of forty zones; reserving once keeps
    // the per-frame rebuild from reallocating.
    m_zonesBuilding.reserve(64);
    m_zonesLive.reserve(64);

    // Applied before the first frame, so nothing ever draws in the wrong palette.
    theme::setMode(static_cast<theme::Mode>(store().settings().themeMode));

    m_tabScenes[static_cast<int>(Tab::Plaza)] = makePlazaScene();
    m_tabScenes[static_cast<int>(Tab::Nearby)] = makeNearbyScene();
    m_tabScenes[static_cast<int>(Tab::Collection)] = makeCollectionScene();
    m_tabScenes[static_cast<int>(Tab::Passport)] = makePassportScene();
    m_tabScenes[static_cast<int>(Tab::Settings)] = makeSettingsScene();

    m_sync.start();

    if (!store().settings().firstRunDone)
        pushOverlay(makeFirstRunScene());
    else
        m_tabScenes[static_cast<int>(m_tab)]->onEnter(*this);

    m_ready = true;
    return true;
}

void App::exit()
{
    m_sync.stop();
    store().flush();

    m_overlays.clear();
    for (auto& scene : m_tabScenes)
        scene.reset();

    miiParts().exit();
    m_renderer.exit();
    m_font.exit();
    m_gpu.exit();

    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
    appletUnlockExit();
    m_ready = false;
}

Rect App::contentArea() const
{
    return Rect { theme::railWidth, 0.0f, Renderer::DesignWidth - theme::railWidth,
        Renderer::DesignHeight - kHintBarHeight };
}

Scene* App::activeScene()
{
    if (!m_overlays.empty())
        return m_overlays.back().get();
    return m_tabScenes[static_cast<int>(m_tab)].get();
}

void App::setTab(Tab tab)
{
    if (tab == m_tab || tab >= Tab::Count)
        return;
    m_tab = tab;
    if (Scene* scene = m_tabScenes[static_cast<int>(m_tab)].get())
        scene->onEnter(*this);
}

void App::pushOverlay(std::unique_ptr<Scene> scene)
{
    if (!scene)
        return;
    scene->onEnter(*this);
    m_overlays.push_back(std::move(scene));
}

void App::popOverlay()
{
    if (m_overlays.empty())
        return;
    m_overlays.pop_back();
    if (m_overlays.empty()) {
        if (Scene* scene = m_tabScenes[static_cast<int>(m_tab)].get())
            scene->onEnter(*this);
    }
}

void App::openEncounter(const std::string& crossingId, std::vector<std::string> siblings)
{
    pushOverlay(makeEncounterScene(crossingId, std::move(siblings)));
}

void App::toast(const std::string& title, const std::string& body)
{
    m_toast = ToastState {};
    m_toast.title = title;
    m_toast.body = body;
    m_toast.remaining = kToastLife;
}

void App::toast(const std::string& title, const std::string& body, const Pass& pass)
{
    toast(title, body);
    m_toast.face = pass.face();
    m_toast.hasFace = true;
    m_toast.theme = pass.theme;
}

void App::askConfirm(const std::string& title, const std::string& body,
    const std::string& confirmLabel, DialogAction onConfirm)
{
    m_dialog.active = true;
    m_dialog.title = title;
    m_dialog.body = body;
    m_dialog.confirmLabel = confirmLabel;
    m_dialog.onConfirm = std::move(onConfirm);
    m_dialog.index = 1;
}

bool App::textInput(const char* header, const std::string& initial, size_t maxChars,
    std::string& out, bool allowEmpty)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return false;

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config, header);
    swkbdConfigSetInitialText(&config, initial.c_str());
    swkbdConfigSetStringLenMax(&config, static_cast<u32>(maxChars));
    swkbdConfigSetStringLenMin(&config, allowEmpty ? 0 : 1);
    swkbdConfigSetBlurBackground(&config, true);

    char buffer[512] = {};
    Result rc = swkbdShow(&config, buffer, sizeof(buffer));
    swkbdClose(&config);

    // The finger that pressed the keyboard's own OK button is very likely still
    // on the panel; do not let it land on the screen we are returning to.
    m_input.forgetTouch();

    if (R_FAILED(rc))
        return false;

    out = clampUtf8(trim(buffer), maxChars);
    return true;
}

void App::hint(const char* button, const std::string& label)
{
    // Title-cased here rather than at every call site, so a scene can declare
    // its controls in plain lowercase and they all read the same in the bar.
    if (m_hints.size() < 6)
        m_hints.push_back(HintEntry { button, titleCase(label) });
}

// The strip along the bottom. 88px on --bg-1 with the controls
// right-aligned; the tab chips on its left are the rail's job here, so that
// space carries the name of the view instead - the rail is icons only.
void App::drawHintBar(Renderer& r)
{
    // Starts where the rail ends. Run full width and it covers the bottom of
    // the rail, where the artboard parks Settings 40px off the floor.
    Rect bar { theme::railWidth, Renderer::DesignHeight - kHintBarHeight,
        Renderer::DesignWidth - theme::railWidth, kHintBarHeight };
    r.rect(bar, theme::bg1);
    r.rect(Rect { bar.x, bar.y, bar.w, theme::stroke }, theme::stroke1);

    TextStyle label;
    label.size = theme::textSm;
    label.weight = FontWeight::Medium;
    label.color = theme::fg3;
    label.tracking = theme::trackingWide;
    r.text(Rect { bar.x + theme::edge, bar.y, 600.0f, bar.h },
        kTabs[static_cast<int>(m_tab)].label, label, Align::Left, VAlign::Middle);

    std::vector<std::pair<const char*, std::string>> hints;
    hints.reserve(m_hints.size() + 2);

    if (m_dialog.active) {
        // A dialog takes the buttons over entirely, so it speaks for them. Its
        // confirm label is a sentence the caller wrote ("Block and forget"), so
        // it is left exactly as given.
        hints.emplace_back("A", m_dialog.index == 0 ? m_dialog.confirmLabel
                                                    : titleCase("keep everything"));
        hints.emplace_back("B", titleCase("cancel"));
    } else {
        for (const HintEntry& entry : m_hints)
            hints.emplace_back(entry.button, entry.label);

        // The controls that belong to the app rather than to a view. Tabs only
        // move when nothing is layered over them.
        if (m_overlays.empty())
            hints.emplace_back("L/R", titleCase("switch tab"));
        hints.emplace_back("+", titleCase("quit"));
    }

    ui::buttonHints(r, Rect { bar.x, bar.y, bar.w - theme::edge, bar.h }, hints.data(),
        static_cast<int>(hints.size()));

}

void App::touchZone(const Rect& rect, int id, int index)
{
    Rect visible = rect.intersect(m_renderer.clipRect());
    if (visible.empty())
        return;
    m_zonesBuilding.push_back(TouchZone { visible, id, index });
}

TouchTarget App::hitTest(float x, float y) const
{
    // Backwards: the last zone declared was drawn on top.
    for (size_t i = m_zonesLive.size(); i > 0; i--) {
        const TouchZone& zone = m_zonesLive[i - 1];
        if (zone.rect.contains(x, y))
            return TouchTarget { zone.id, zone.index };
    }
    return TouchTarget {};
}

bool App::touchHeld(int id, int index) const
{
    if (!m_pressed)
        return false;
    return index < 0 ? m_pressed.is(id) : m_pressed.is(id, index);
}

bool App::takeTap(TouchTarget& out)
{
    if (!m_tap)
        return false;
    out = m_tap;
    m_tap = TouchTarget {};
    return true;
}

void App::routeTouch(const Touch& touch)
{
    // Last frame's zones are what the user was looking at, so they are what a
    // touch this frame has to be matched against.
    m_zonesLive = std::move(m_zonesBuilding);
    m_zonesBuilding.clear();

    if (touch.pressed)
        m_pressed = hitTest(touch.x, touch.y);

    // Turning into a drag cancels the press, the way a button does: the finger
    // has started doing something else.
    if (touch.dragged)
        m_pressed = TouchTarget {};

    if (touch.released) {
        // A tap only counts if the finger comes up on the same thing it went down on.
        if (!touch.dragged && m_pressed && hitTest(touch.x, touch.y).is(m_pressed.id, m_pressed.index))
            m_tap = m_pressed;
        m_pressed = TouchTarget {};
    }
}

void App::pumpArrivals()
{
    std::vector<std::string> arrivals = m_sync.takeArrivals();
    if (arrivals.empty())
        return;

    if (!store().settings().notify)
        return;

    Crossing first;
    std::string title;
    if (store().findCrossing(arrivals.front(), first)) {
        if (arrivals.size() == 1)
            title = format("%s passed you", first.pass.handle.c_str());
        else
            title = format("%s and %zu others passed you", first.pass.handle.c_str(),
                arrivals.size() - 1);
    } else {
        title = format("%zu new passes", arrivals.size());
    }

    std::string body;
    if (!first.place.empty())
        body = first.place;
    if (!first.pass.carrying.empty()) {
        if (!body.empty())
            body += " - ";
        body += format("one of them is carrying %s", first.pass.carrying.front().c_str());
    }
    if (body.empty())
        body = "Open the plaza to see who.";

    toast(title, body, first.pass);
}

bool App::handleChromeInput(const Input& input)
{
    if (m_dialog.active) {
        TouchTarget tap;
        if (takeTap(tap)) {
            if (tap.is(Touch_DialogConfirm)) {
                DialogAction action = m_dialog.onConfirm;
                m_dialog = DialogState {};
                if (action)
                    action();
                return true;
            }
            if (tap.is(Touch_DialogCancel) || tap.is(Touch_Dismiss)) {
                m_dialog = DialogState {};
                return true;
            }
        }

        if (input.navLeft || input.navRight)
            m_dialog.index = m_dialog.index == 0 ? 1 : 0;
        if (input.pressed(HidNpadButton_A)) {
            DialogAction action = m_dialog.onConfirm;
            bool confirmed = m_dialog.index == 0;
            m_dialog = DialogState {};
            if (confirmed && action)
                action();
        } else if (input.pressed(HidNpadButton_B)) {
            m_dialog = DialogState {};
        }
        return true;
    }

    if (input.pressed(HidNpadButton_Plus)) {
        requestExit();
        return true;
    }

    // Chrome gets first refusal on a tap; anything left over falls through to
    // the scene's own update.
    TouchTarget tap;
    if (takeTap(tap)) {
        if (tap.is(Touch_Tab)) {
            while (hasOverlay())
                popOverlay();
            setTab(static_cast<Tab>(tap.index));
            return true;
        }
        if (tap.is(Touch_Toast)) {
            // Dismisses it, and nothing else. Jumping to the plaza from here
            // threw away whatever the user was in the middle of, for a tap they
            // had no way to know would do that.
            m_toast.remaining = 0.0f;
            return true;
        }
        if (tap.is(Touch_Dismiss)) {
            popOverlay();
            return true;
        }
        // Not ours: hand it back for the scene to pick up.
        m_tap = tap;
    }

    // Tab switching is global, but overlays own the shoulder buttons so a
    // detail view can use them for next/previous.
    if (m_overlays.empty()) {
        int index = static_cast<int>(m_tab);
        int count = static_cast<int>(Tab::Count);
        if (input.pressed(HidNpadButton_R) || input.pressed(HidNpadButton_ZR))
            setTab(static_cast<Tab>((index + 1) % count));
        else if (input.pressed(HidNpadButton_L) || input.pressed(HidNpadButton_ZL))
            setTab(static_cast<Tab>((index + count - 1) % count));
    }

    return false;
}

void App::update(float dt)
{
    m_time += dt;
    m_input.update(dt);
    const Input& input = m_input.state();

    routeTouch(input.touch);
    pumpArrivals();

    if (!handleChromeInput(input)) {
        if (Scene* scene = activeScene())
            scene->update(*this, input, dt);
    }

    // Nothing claimed it: drop it rather than let it fire on a later frame,
    // possibly on a different screen.
    m_tap = TouchTarget {};

    if (m_toast.remaining > 0.0f) {
        m_toast.remaining -= dt;
        m_toast.age += dt;
    }

    float target = static_cast<float>(static_cast<int>(m_tab));
    m_tabHighlight += (target - m_tabHighlight) * std::min(1.0f, dt * 16.0f);

    // Two writes a second at most, so a burst of arrivals is one SD card hit.
    static float saveTimer = 0.0f;
    saveTimer += dt;
    if (saveTimer > 2.0f) {
        saveTimer = 0.0f;
        store().flush();
    }
}

void App::draw()
{
    dk::CmdBuf cmd = m_gpu.beginFrame();
    m_renderer.beginFrame(cmd);
    m_renderer.clear(theme::bg0);

    m_hints.clear();

    Scene* scene = activeScene();
    bool covered = scene && scene->coversChrome();

    if (!covered) {
        // The tab underneath keeps drawing behind a detail overlay, which is
        // what makes the encounter view feel like it sits on the plaza.
        if (Scene* base = m_tabScenes[static_cast<int>(m_tab)].get())
            base->draw(*this, m_renderer);

        if (!m_overlays.empty()) {
            m_renderer.rect(m_renderer.viewport(), theme::scrim);
            // Declared before the overlay draws, so the overlay's own zones sit
            // on top of it and only a touch that misses them dismisses.
            touchZone(m_renderer.viewport(), Touch_Dismiss);
            m_overlays.back()->draw(*this, m_renderer);
        }

        drawNavRail(m_renderer);

        drawStatusPip(m_renderer);
    } else if (scene) {
        scene->draw(*this, m_renderer);
    }

    // Every view gets the same strip in the same place, overlays included.
    drawHintBar(m_renderer);

    drawToast(m_renderer);
    drawDialog(m_renderer);

    m_renderer.endFrame();
    m_gpu.endFrame();
}

void App::drawNavRail(Renderer& r)
{
    // 112px on --bg-1, padding 40px 0, gap --space-6.
    // The lantern is 44px with a 28px bloom, then 72px icon slots on a 104px pitch,
    // with Settings pinned to the floor by a flex spacer.
    Rect rail { 0.0f, 0.0f, theme::railWidth, Renderer::DesignHeight };
    r.rect(rail, theme::bg1);
    r.rect(Rect { rail.right() - theme::stroke, 0.0f, theme::stroke, rail.h }, theme::stroke1);

    float lanternY = 40.0f + 22.0f;
    r.glow(Rect { rail.centerX() - 28.0f, lanternY - 28.0f, 56.0f, 56.0f },
        theme::accentGlow.scaleAlpha(0.45f), 1.6f);
    r.circle(rail.centerX(), lanternY, 22.0f, theme::mark);
    // radial-gradient(circle at 34% 30%, #FFE7B0, #F5A524 55%, #D88E14)
    r.circle(rail.centerX() - 5.0f, lanternY - 6.0f, 9.0f, theme::accentSoft);

    Stats stats = store().stats();

    auto drawTab = [&](int index, float y) {
        Rect box { rail.centerX() - 36.0f, y, 72.0f, 72.0f };
        bool selected = index == static_cast<int>(m_tab);

        touchZone(Rect { 0.0f, y - 16.0f, rail.w, 104.0f }, Touch_Tab, index);

        if (selected)
            r.roundRect(box, theme::r2, theme::accentTint);
        else if (touchHeld(Touch_Tab, index))
            r.roundRect(box, theme::r2, theme::bg3);

        ui::icon(r, box, kTabs[index].icon, selected ? theme::accent : theme::fg3, 3.0f);

        // A plaza with unread passes has to say so.
        if (index == static_cast<int>(Tab::Plaza) && stats.unopened > 0) {
            std::string count = format("%u", stats.unopened);
            TextStyle badgeText;
            badgeText.size = theme::textXs;
            badgeText.weight = FontWeight::Bold;
            float width = std::max(r.measure(count, badgeText) + 20.0f, 34.0f);
            // Clear of the glyph, not across it.
            Rect badge { box.right() - 6.0f, box.y - 14.0f, width,
                theme::textXs * theme::leadingNormal + 8.0f };
            ui::pill(r, badge, count, theme::bg0, theme::teal, theme::textXs);
        }
    };

    float y = 140.0f;
    for (int i = 0; i < static_cast<int>(Tab::Settings); i++) {
        drawTab(i, y);
        y += 104.0f;
    }
    drawTab(static_cast<int>(Tab::Settings), Renderer::DesignHeight - 40.0f - 72.0f);
}

void App::drawStatusPip(Renderer& r)
{
    Sync::Status status = m_sync.status();

    Color dot = theme::fg4;
    switch (status.state) {
    case Sync::State::Idle:
        dot = theme::teal;
        break;
    case Sync::State::Working:
        // Breathing while a request is in flight.
        dot = theme::accent.scaleAlpha(0.55f + 0.45f * std::sin(m_time * 6.0f));
        break;
    case Sync::State::Error:
        dot = theme::danger;
        break;
    case Sync::State::Offline:
        dot = theme::fg4;
        break;
    }

    Rect area = contentArea();
    float x = area.right() - theme::edge;
    float y = 44.0f;
    r.circle(x, y, 8.0f, dot);
}

void App::drawToast(Renderer& r)
{
    if (m_toast.remaining <= 0.0f)
        return;

    // right:64px top:48px, 720 wide, --bg-2 at 86% behind a
    // --stroke-2 hairline, radius-4, --space-6 padding.
    // A cropped portrait on the left and the copy beside it.
    float appear = std::min(1.0f, m_toast.age / 0.28f);
    float fade = std::min(1.0f, m_toast.remaining / 0.6f);
    float alpha = appear * fade;

    constexpr float kWidth = 720.0f;
    constexpr float kAvatar = 96.0f;

    TextStyle eyebrow;
    eyebrow.size = theme::textXs;
    eyebrow.weight = FontWeight::Bold;
    eyebrow.color = theme::accent.scaleAlpha(alpha);
    eyebrow.tracking = theme::trackingWider;
    eyebrow.uppercase = true;

    TextStyle title;
    title.size = theme::textLg;
    title.weight = FontWeight::Bold;
    title.color = theme::fg1.scaleAlpha(alpha);
    title.tracking = theme::trackingTight;

    TextStyle body;
    body.size = theme::textSm;
    body.color = theme::fg2.scaleAlpha(alpha);
    body.leading = theme::leadingNormal;

    // A notice that is not about a person has no portrait to show.
    float avatarWidth = m_toast.hasFace ? kAvatar + theme::s5 : 0.0f;
    float textWidth = kWidth - theme::s6 * 2.0f - avatarWidth;

    float titleHeight = r.measureWrapped(textWidth, m_toast.title, title, 2);
    float bodyHeight = r.measureWrapped(textWidth, m_toast.body, body, 2);
    float textHeight = theme::textXs * theme::leadingNormal + 8.0f + titleHeight + 8.0f
        + bodyHeight;

    float height = std::max(m_toast.hasFace ? kAvatar : 0.0f, textHeight) + theme::s6 * 2.0f;
    Rect box { Renderer::DesignWidth - theme::edge - kWidth,
        theme::edgeTop - (1.0f - appear) * 24.0f, kWidth, height };

    touchZone(box, Touch_Toast);

    r.roundRect(box, theme::r4, theme::bg2.withAlpha(0.86f * alpha));
    r.strokeRect(box, theme::r4, theme::stroke, theme::stroke2.scaleAlpha(alpha));

    Rect inner = box.inset(theme::s6);

    if (m_toast.hasFace) {
        Rect avatar { inner.x, inner.y, kAvatar, kAvatar };
        r.pushClip(avatar);
        ui::miiStage(r, avatar, m_toast.face, m_toast.theme,
            ui::StageFigure { 0.56f, 0.56f, 0.32f, 0.50f, 0.0f, 0.17f }, theme::r2, theme::r2);
        r.popClip();
    }

    Rect text { inner.x + avatarWidth, inner.y, textWidth, inner.h };

    float dotY = text.y + theme::textXs * theme::leadingNormal * 0.5f;
    r.circle(text.x + 11.0f, dotY, 11.0f, theme::mark.scaleAlpha(alpha));
    r.text(text.x + 22.0f + theme::s3, text.y, kAppName, eyebrow);

    float titleY = text.y + theme::textXs * theme::leadingNormal + 8.0f;
    r.textWrapped(Rect { text.x, titleY, text.w, titleHeight }, m_toast.title, title, 2);
    r.textWrapped(Rect { text.x, titleY + titleHeight + 8.0f, text.w, bodyHeight },
        m_toast.body, body, 2);

}

void App::drawDialog(Renderer& r)
{
    if (!m_dialog.active)
        return;

    r.rect(r.viewport(), theme::scrim);
    touchZone(r.viewport(), Touch_Dismiss);

    Rect box { Renderer::DesignWidth * 0.5f - 520.0f, Renderer::DesignHeight * 0.5f - 215.0f,
        1040.0f, 430.0f };
    r.roundRect(box, theme::r5, theme::bg2);
    r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);

    Rect inner = box.inset(theme::s8, theme::s7);

    TextStyle title;
    title.size = theme::textLg;
    title.weight = FontWeight::Bold;
    title.color = theme::fg1;
    r.text(inner.x, inner.y, m_dialog.title, title);

    TextStyle body;
    body.size = theme::textBase;
    body.color = theme::fg2;
    r.textWrapped(Rect { inner.x, inner.y + 62.0f, inner.w, 150.0f }, m_dialog.body, body, 3);

    float buttonWidth = (inner.w - theme::s4) * 0.5f;
    Rect confirm { inner.x, inner.bottom() - 96.0f, buttonWidth, 96.0f };
    Rect cancel { confirm.right() + theme::s4, confirm.y, buttonWidth, 96.0f };

    // Declared after the scrim, so they win over it.
    touchZone(box, Touch_None); // the dialog body swallows taps, it is not a target
    touchZone(confirm, Touch_DialogConfirm);
    touchZone(cancel, Touch_DialogCancel);

    bool confirmFocused = m_dialog.index == 0;

    r.roundRect(confirm, theme::r2, confirmFocused ? theme::danger : theme::dangerTint);
    ui::focusRing(r, confirm, theme::r2, confirmFocused ? 1.0f : 0.0f);
    r.roundRect(cancel, theme::r2, confirmFocused ? theme::bg4 : theme::bg3);
    ui::focusRing(r, cancel, theme::r2, confirmFocused ? 0.0f : 1.0f);

    TextStyle label;
    label.size = theme::textBase;
    label.weight = FontWeight::Bold;
    label.color = confirmFocused ? theme::bg0 : theme::danger;
    r.text(confirm, m_dialog.confirmLabel, label, Align::Center, VAlign::Middle);

    label.color = theme::fg1;
    r.text(cancel, "Keep everything", label, Align::Center, VAlign::Middle);
}

void App::onOperationMode(AppletOperationMode mode)
{
    m_gpu.setOperationMode(mode);
}

void App::run()
{
    if (!m_ready)
        return;

    onOperationMode(appletGetOperationMode());

    uint64_t tickReference = armGetSystemTick();
    uint64_t tickSaved = tickReference;
    uint64_t lastNs = 0;
    bool focused = appletGetFocusState() == AppletFocusState_InFocus;

    while (!m_shouldExit) {
        u32 message = 0;
        if (R_SUCCEEDED(appletGetMessage(&message))) {
            if (!appletProcessMessage(message))
                break;

            switch (message) {
            case AppletMessage_FocusStateChanged: {
                bool wasFocused = focused;
                AppletFocusState state = appletGetFocusState();
                focused = state == AppletFocusState_InFocus;
                if (focused == wasFocused)
                    break;

                if (focused) {
                    // Do not let time spent in the HOME menu land as one huge
                    // delta on the first frame back.
                    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
                    tickReference += armGetSystemTick() - tickSaved;
                    // The console's own light/dark setting may have changed
                    // while we were in the background.
                    theme::refreshSystemMode();
                    // Same reasoning as after the keyboard: we have no idea
                    // what the panel was doing while we were not looking.
                    m_input.forgetTouch();
                } else {
                    tickSaved = armGetSystemTick();
                    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
                }
                break;
            }
            case AppletMessage_OperationModeChanged:
                onOperationMode(appletGetOperationMode());
                break;
            default:
                break;
            }
        }

        if (!focused) {
            // The sync thread keeps trading while we are in the background.
            svcSleepThread(16000000ULL);
            continue;
        }

        uint64_t nowNs = armTicksToNs(armGetSystemTick() - tickReference);
        float dt = lastNs == 0 ? 1.0f / 60.0f
                               : static_cast<float>(nowNs - lastNs) / 1000000000.0f;
        lastNs = nowNs;
        dt = std::min(std::max(dt, 0.0f), 0.1f);

        update(dt);
        draw();
    }
}

} // namespace nxp
