#pragma once

#include "core/store.h"
#include "gfx/font.h"
#include "gfx/gpu.h"
#include "gfx/renderer.h"
#include "net/sync.h"
#include "platform/input.h"
#include "scenes/scene.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nxp {

// Touch zone ids.
//
// Everything below Touch_SceneBase belongs to the app chrome and is consumed
// by App itself; a scene numbers its own zones upward from Touch_SceneBase.
enum TouchId : int {
    Touch_None = 0,
    Touch_Tab,           // index = the tab to switch to
    Touch_Toast,
    Touch_DialogConfirm,
    Touch_DialogCancel,
    Touch_Dismiss,       // anywhere outside an overlay
    Touch_SceneBase = 64,
};

// What a touch landed on.
struct TouchTarget {
    int id = TouchId::Touch_None;
    int index = -1;

    bool is(int wantId) const { return id == wantId; }
    bool is(int wantId, int wantIndex) const { return id == wantId && index == wantIndex; }
    explicit operator bool() const { return id != TouchId::Touch_None; }
};

enum class Tab : int {
    Plaza = 0,
    Nearby,
    Collection,
    Square,
    Puzzles,
    Shop,
    Passport,
    Settings,
    Count
};

// Owns the graphics stack, the scenes, and the applet lifecycle.
class App {
public:
    bool init();
    void exit();
    void run();

    // ------------------------------------------------------------ services

    Renderer& renderer() { return m_renderer; }
    Font& font() { return m_font; }
    Store& store() { return Store::get(); }
    Sync& sync() { return m_sync; }

    // Seconds since launch, for animation.
    float time() const { return m_time; }

    // The area a scene may draw in: everything right of the nav rail.
    //
    // There is deliberately no handheld variant of this, or of any layout in
    // the app. A Switch Lite can never dock, so a docked-only arrangement
    // would be a different product on different hardware; instead the docked
    // artboards are implemented once in the 1920x1080 design space and the
    // handheld panel renders that same layout at two thirds scale.
    Rect contentArea() const;

    // -------------------------------------------------------------- naviga

    Tab tab() const { return m_tab; }
    void setTab(Tab tab);

    void pushOverlay(std::unique_ptr<Scene> scene);
    void popOverlay();
    bool hasOverlay() const { return !m_overlays.empty(); }

    void openEncounter(const std::string& crossingId,
        std::vector<std::string> siblings = {});

    void setLastViewedCrossing(const std::string& id) { m_lastViewedCrossing = id; }

    // Reads it once and forgets it. The lists rebuild themselves every frame,
    // so a value that stayed put would pin the cursor there and the stick would
    // stop working.
    std::string takeLastViewedCrossing()
    {
        std::string id;
        id.swap(m_lastViewedCrossing);
        return id;
    }

    // ---------------------------------------------------------------- touch

    // Declares a touchable rectangle, from inside draw().
    //
    // Layout lives in draw(), so that is where the geometry is known; taps are
    // matched against the zones declared on the *previous* frame, which is
    // exactly the layout the user was looking at when they touched it. Zones
    // declared later win, so anything drawn on top is hit first, and each is
    // clipped to the renderer's live clip rect.
    void touchZone(const Rect& rect, int id, int index = -1);

    // True while the finger is pressing this zone. For press feedback.
    bool touchHeld(int id, int index = -1) const;

    // A completed tap, handed to the first caller that asks for it. Returns
    // false once it has been consumed.
    bool takeTap(TouchTarget& out);

    // ---------------------------------------------------------------- hints

    // Declares a control for the bottom bar, from inside draw().
    //
    // The bar itself is chrome, so every view gets the same strip in the same
    // place; a scene only says what its buttons do. Declared in draw order,
    // drawn left to right.
    void hint(const char* button, const std::string& label);

    // --------------------------------------------------------------- chrome

    void toast(const std::string& title, const std::string& body);
    // A toast that also carries the look of the pass it is about.
    void toast(const std::string& title, const std::string& body, const Pass& pass);

    using DialogAction = std::function<void()>;
    void askConfirm(const std::string& title, const std::string& body,
        const std::string& confirmLabel, DialogAction onConfirm);

    // System software keyboard. Returns false when the user cancelled.
    bool textInput(const char* header, const std::string& initial, size_t maxChars,
        std::string& out, bool allowEmpty = true);

    void requestExit() { m_shouldExit = true; }

private:
    void onOperationMode(AppletOperationMode mode);
    void update(float dt);
    void pollUpdate();
    void draw();

    void drawNavRail(Renderer& r);
    void drawHintBar(Renderer& r);
    void drawToast(Renderer& r);
    void drawDialog(Renderer& r);
    void drawStatusPip(Renderer& r);

    bool handleChromeInput(const Input& input);
    void routeTouch(const Touch& touch);
    TouchTarget hitTest(float x, float y) const;
    void pumpArrivals();

    Scene* activeScene();

    Gpu m_gpu;
    Font m_font;
    Renderer m_renderer;
    Sync m_sync;
    InputTracker m_input;

    std::unique_ptr<Scene> m_tabScenes[static_cast<int>(Tab::Count)];
    std::vector<std::unique_ptr<Scene>> m_overlays;

    // One control in the bottom bar.
    struct HintEntry {
        const char* button;
        std::string label;
    };
    std::vector<HintEntry> m_hints;

    struct TouchZone {
        Rect rect;
        int id = TouchId::Touch_None;
        int index = -1;
    };

    // Filled while drawing; becomes the live set at the top of the next update.
    std::vector<TouchZone> m_zonesBuilding;
    std::vector<TouchZone> m_zonesLive;
    TouchTarget m_pressed;
    TouchTarget m_tap;

    Tab m_tab = Tab::Plaza;
    std::string m_lastViewedCrossing;
    float m_time = 0.0f;
    float m_tabHighlight = 0.0f; // animated rail highlight position

    struct ToastState {
        std::string title;
        std::string body;
        Mii face;              // the pass that arrived, for the thumbnail
        bool hasFace = false;  // false for a notice that is not about a person
        uint32_t theme = 0;
        float remaining = 0.0f;
        float age = 0.0f;
    } m_toast;

    struct DialogState {
        bool active = false;
        std::string title;
        std::string body;
        std::string confirmLabel;
        DialogAction onConfirm;
        int index = 1; // 0 = confirm, 1 = cancel (cancel is the safe default)
    } m_dialog;

    // What the updater said last frame, so a result is announced once rather
    // than on every frame it stays in that state.
    // The launch check runs once, after the plaza has answered.
    bool m_updateChecked = false;
    int m_lastUpdateState = -1;
    bool m_shouldExit = false;
    bool m_ready = false;
};

} // namespace nxp
