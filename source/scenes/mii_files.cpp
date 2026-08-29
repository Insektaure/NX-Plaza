#include "app.h"
#include "core/mii_file.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// Faces on the SD card: the save row, then everything already saved.
//
// Saving and loading are one screen rather than two buttons because they are
// the same folder seen from two sides, and because the editor has no spare
// face buttons - A, B, X and the shoulders are all spoken for.
class MiiFilesScene final : public Scene {
public:
    enum Zone : int {
        Zone_Row = Touch_SceneBase,
        Zone_Delete,
    };

    MiiFilesScene(const Mii& current, std::string defaultName,
        std::function<void(const Mii&)> onPick)
        : m_current(current)
        , m_defaultName(std::move(defaultName))
        , m_onPick(std::move(onPick))
    {
    }

    void onEnter(App& app) override { refresh(); }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

        const Touch& touch = input.touch;
        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();
        if (touch.down && touch.dragged && m_listArea.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dy, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            m_dragging = false;
        }
        m_scroll.update(dt);

        if (input.back()) {
            app.popOverlay();
            return;
        }

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap)) {
            if (tap.is(Zone_Delete) && tap.index < rowCount()) {
                m_focus = tap.index;
                m_onDelete = true;
                deleteFlow(app);
                return;
            }
            if (tap.is(Zone_Row) && tap.index < rowCount()) {
                m_focus = tap.index;
                m_onDelete = false;
                activate(app);
                return;
            }
        }

        if (input.navDown)
            m_focus = std::min(m_focus + 1, rowCount() - 1);
        if (input.navUp)
            m_focus = std::max(m_focus - 1, 0);
        if (input.navDown || input.navUp)
            m_scroll.centerOn(rowTop(m_focus) + kRowHeight * 0.5f);

        // The bin is part of the row rather than a row of its own, so it is
        // reached across rather than down - the same move the passport uses to
        // get from its rows to the card beside them.
        if (input.navRight)
            m_onDelete = true;
        if (input.navLeft)
            m_onDelete = false;
        if (!canDelete())
            m_onDelete = false;

        if (input.accept())
            activate(app);
    }

    // The save row has nothing to delete, and neither does an empty list.
    bool canDelete() const { return m_focus > 0 && m_focus < rowCount(); }

    void draw(App& app, Renderer& r) override
    {
        app.hint("A", m_onDelete ? "delete" : (m_focus == 0 ? "save" : "load this face"));
        app.hint("B", "close");

        constexpr float kWidth = 1000.0f;
        constexpr float kHeight = 700.0f;
        Rect box { Renderer::DesignWidth * 0.5f - kWidth * 0.5f,
            Renderer::DesignHeight * 0.5f - kHeight * 0.5f - 20.0f, kWidth, kHeight };

        app.touchZone(box, Zone_Card);
        r.roundRect(box, theme::r5, theme::bg1);
        r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);

        Rect inner = box.inset(theme::s7, theme::s7);
        float y = inner.y;

        TextStyle eyebrow;
        eyebrow.size = theme::textXs;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::teal;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(inner.x, y, "faces on the sd card", eyebrow);
        y += eyebrow.size * theme::leadingNormal + theme::s3;

        TextStyle title;
        title.size = theme::textXl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        r.text(inner.x, y, "Save and load", title);
        y += title.size * theme::leadingTight + theme::s3;

        // The path, because a file the user cannot find is a file they did not
        // export. Written without the sdmc: prefix, which is a libnx detail.
        TextStyle path;
        path.size = theme::textSm;
        path.color = theme::fg3;
        std::string where = miiExportDir();
        if (where.rfind("sdmc:", 0) == 0)
            where = where.substr(5);
        r.text(inner.x, y, where, path);
        y += path.size * theme::leadingNormal + theme::s5;

        m_listArea = Rect { inner.x, y, inner.w, inner.bottom() - y };
        drawList(app, r, m_listArea);
    }

private:
    static constexpr float kRowHeight = 96.0f;
    static constexpr float kRowGap = 10.0f;
    enum { Zone_Card = Touch_SceneBase + 1 };

    int rowCount() const { return 1 + static_cast<int>(m_files.size()); }
    float rowTop(int index) const { return static_cast<float>(index) * (kRowHeight + kRowGap); }

    void refresh()
    {
        m_files = listSavedMiis();
        m_focus = std::min(m_focus, rowCount() - 1);
    }

    void activate(App& app)
    {
        if (m_onDelete && canDelete()) {
            deleteFlow(app);
            return;
        }
        if (m_focus == 0) {
            MiiFilesScene* self = this;
            promptSaveMii(app, m_current, m_defaultName, [self]() { self->refresh(); });
            return;
        }

        const MiiFile& file = m_files[static_cast<size_t>(m_focus - 1)];
        if (!file.readable) {
            std::string why;
            Mii ignored;
            loadMii(file.path, ignored, &why);
            app.toast("Cannot load that one", why);
            return;
        }
        if (m_onPick)
            m_onPick(file.face);
        app.popOverlay();
    }

    // Deleting is the only thing here that cannot be undone - the file is
    // gone, and nothing in the app kept a copy of it - so it asks first, and
    // the prompt names the file rather than asking about "this face".
    void deleteFlow(App& app)
    {
        const MiiFile& file = m_files[static_cast<size_t>(m_focus - 1)];
        std::string name = file.name;
        std::string path = file.path;

        MiiFilesScene* self = this;
        App* appPtr = &app;
        app.askConfirm("Delete " + name + "?",
            "It is removed from the SD card for good. Anyone you sent it to still has "
            "their copy.",
            "Delete", [self, appPtr, name, path]() {
                std::string err;
                if (deleteSavedMii(path, &err)) {
                    appPtr->toast("Deleted", name + " is gone from the export folder.");
                    self->refresh();
                } else {
                    appPtr->toast("Not deleted", err);
                }
            });
    }

    void drawList(App& app, Renderer& r, const Rect& box)
    {
        float content = rowTop(rowCount());
        m_scroll.setBounds(box.h, content);

        // Vertical only, with room for the ring: the focus ring is drawn
        // *outside* the row, so clipping the sides cuts it off down both edges
        // and a clip that stops at box.y shaves the top row's.
        r.pushClipVertical(box.inset(0.0f, -theme::focusRoom));
        float y = box.y - m_scroll.offset();
        for (int i = 0; i < rowCount(); i++) {
            Rect row { box.x, y, box.w - theme::s6, kRowHeight };
            if (row.bottom() > box.y - kRowHeight && row.y < box.bottom() + kRowHeight)
                drawRow(app, r, row, i);
            y += kRowHeight + kRowGap;
        }
        r.popClip();

        // An empty folder is one row and a lot of nothing, which reads as a
        // screen that failed to load rather than one with nothing in it yet.
        if (m_files.empty()) {
            TextStyle empty;
            empty.size = theme::textSm;
            empty.color = theme::fg3;
            empty.leading = theme::leadingNormal;
            r.textWrapped(Rect { box.x + theme::s5, box.y + kRowHeight + kRowGap + theme::s5,
                              box.w - theme::s5 * 2.0f, 100.0f },
                "Nothing saved yet. Faces you save show up here, and so does anything you "
                "drop into the folder yourself.",
                empty, 2);
        }

        if (m_scroll.scrollable())
            ui::scrollbar(r, Rect { box.right() - 8.0f, box.y, 8.0f, box.h },
                m_scroll.progress(), m_scroll.visibleFraction());
    }

    // Where the bin sits on a row. Square, and inset from the row's right edge
    // by the same gap the portrait gets on the left.
    static Rect deleteButton(const Rect& row)
    {
        float side = kRowHeight - theme::s3 * 2.0f;
        return Rect { row.right() - theme::s5 - side, row.centerY() - side * 0.5f, side, side };
    }

    void drawDeleteButton(App& app, Renderer& r, const Rect& box, int index, bool focused)
    {
        app.touchZone(box, Zone_Delete, index);

        float focus = app.touchHeld(Zone_Delete, index)
            ? 1.0f
            : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);

        // Red, because it is the one control here that destroys something, and
        // filled rather than outlined so it reads as a button on a row that is
        // otherwise all text. The ring is the app's amber even on red: it says
        // where the cursor is, not what the button does.
        r.roundRect(box, theme::r2, theme::danger);
        ui::focusRing(r, box, theme::r2, focus);

        float glyph = box.w * 0.52f;
        ui::icon(r, Rect { box.centerX() - glyph * 0.5f, box.centerY() - glyph * 0.5f, glyph,
                     glyph },
            ui::Icon::Trash, Color::hex(0xFFFFFF), 2.5f);
    }

    void drawRow(App& app, Renderer& r, const Rect& box, int index)
    {
        app.touchZone(box, Zone_Row, index);

        bool focused = index == m_focus;
        bool onRow = focused && !m_onDelete;
        float focus = app.touchHeld(Zone_Row, index)
            ? 1.0f
            : (onRow ? 0.7f + 0.3f * m_pulse : 0.0f);
        // The row still reads as the selected one while the cursor is across on
        // its bin - it is raised, but the ring is over there.
        ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

        Rect inner = box.inset(theme::s5, theme::s3);

        TextStyle label;
        label.size = theme::textBase;
        label.weight = FontWeight::Bold;
        label.color = theme::fg1;

        TextStyle sub;
        sub.size = theme::textSm;
        sub.color = theme::fg3;

        if (index == 0) {
            float glyph = 34.0f;
            ui::icon(r, Rect { inner.x, inner.centerY() - glyph * 0.5f, glyph, glyph },
                ui::Icon::Plus, focused ? theme::accent : theme::fg2, 2.5f);
            r.text(Rect { inner.x + glyph + theme::s4, inner.y, inner.w, inner.h },
                "Save this face...", label, Align::Left, VAlign::Middle);
            return;
        }

        const MiiFile& file = m_files[static_cast<size_t>(index - 1)];

        Rect bin = deleteButton(box);
        drawDeleteButton(app, r, bin, index, focused && m_onDelete);

        // A face beside its name, because a list of filenames is not a list of
        // faces. Unreadable ones get the gap rather than a stand-in, which would
        // be a face the file does not contain.
        float head = kRowHeight - theme::s3 * 2.0f;
        if (file.readable)
            ui::miiHead(r, Rect { inner.x, inner.centerY() - head * 0.5f, head, head },
                file.face);

        float textX = inner.x + head + theme::s5;
        float textW = bin.x - theme::s5 - textX;

        std::string detail;
        if (!file.readable)
            detail = "Not a face this version can read";
        else if (!file.handle.empty())
            detail = file.handle;

        if (detail.empty()) {
            r.text(Rect { textX, inner.y, textW, inner.h }, r.ellipsize(file.name, label, textW),
                label, Align::Left, VAlign::Middle);
            return;
        }

        if (!file.readable)
            sub.color = theme::fg4;
        float lineHeight = label.size * theme::leadingSnug + sub.size * theme::leadingNormal;
        float top = inner.centerY() - lineHeight * 0.5f;
        r.text(textX, top + label.size * 0.8f, r.ellipsize(file.name, label, textW), label);
        r.text(textX, top + label.size * theme::leadingSnug + sub.size * 0.8f,
            r.ellipsize(detail, sub, textW), sub);
    }

    Mii m_current;
    std::string m_defaultName;
    std::function<void(const Mii&)> m_onPick;

    std::vector<MiiFile> m_files;
    int m_focus = 0;
    bool m_onDelete = false; // the cursor is across on the focused row's bin

    float m_pulse = 0.0f;

    ui::ScrollView m_scroll;
    Rect m_listArea {};
    bool m_dragging = false;
    bool m_brakedTap = false;
};

} // namespace

void promptSaveMii(App& app, const Mii& face, const std::string& defaultName,
    std::function<void()> onSaved)
{
    std::string typed;
    if (!app.textInput("Name this face", defaultName, 48, typed, false))
        return;

    std::string name = sanitizeMiiName(typed);
    if (name.empty()) {
        app.toast("Needs a name", "That name has nothing in it a file can be called.");
        return;
    }

    App* appPtr = &app;
    auto write = [appPtr, name, face, defaultName, onSaved]() {
        std::string err;
        if (!saveMii(name, face, defaultName, &err)) {
            appPtr->toast("Not saved", err);
            return;
        }
        appPtr->toast("Saved", name + " is in the export folder.");
        if (onSaved)
            onSaved();
    };

    // Overwriting a saved face is the one move here that loses something, so it
    // asks. A dialog holds every button while it is up, so the screen that
    // called this cannot be dismissed out from under the callback.
    if (!fileExists(miiExportPath(name))) {
        write();
        return;
    }
    app.askConfirm("Replace " + name + "?",
        "There is already a face saved under that name. Replacing it cannot be undone.",
        "Replace", write);
}

std::unique_ptr<Scene> makeMiiFilesScene(const Mii& current, const std::string& defaultName,
    std::function<void(const Mii&)> onPick)
{
    return std::unique_ptr<Scene>(
        new MiiFilesScene(current, defaultName, std::move(onPick)));
}

} // namespace nxp
