#include "app.h"
#include "core/i18n.h"
#include "core/quest_record.h"
#include "core/quest_rules.h"
#include "core/util.h"
#include "platform/audio.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nxp {

namespace {

    // Three pieces in, one of the rank above out.
    //
    // A screen of its own rather than a button on the bag, because what it
    // costs has to be in front of somebody while they decide: three sockets
    // with three real pieces in them, the thing they would become underneath,
    // and no way to press the button without having read both. The bag's
    // rank sweep is the opposite shape on purpose - that one is tidying, and
    // this one is spending.
    //
    // Why it exists at all is in the comment over QuestRecord::check(): the
    // tower gives out tiers by depth, depth is set by how many people you
    // have crossed, and a small collection would otherwise farm the same rank
    // for ever.
    class ForgeScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Socket = Touch_SceneBase,
            Zone_Row,
            Zone_Filter,
            Zone_Melt,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        void onEnter(App&) override
        {
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++)
                m_socket[i] = 0;
            m_filter = 0;
            m_pick = 0;
            m_socketPick = 0;
            m_focus = Focus_Bag;
            m_scroll.stop();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            gather();
            m_scroll.update(dt);

            const Touch& touch = input.touch;
            if (touch.pressed)
                m_braked = m_scroll.absorbPress();
            if (touch.down && touch.dragged
                && m_listArea.contains(touch.startX, touch.startY)) {
                m_scroll.drag(-touch.dy, dt);
                m_dragging = true;
            } else if (m_dragging && !touch.down) {
                m_scroll.release();
                m_dragging = false;
            }

            TouchTarget tap;
            if (app.takeTap(tap) && !m_braked) {
                if (tap.is(Zone_Back)) {
                    app.popOverlay();
                    return;
                }
                if (tap.is(Zone_Melt)) {
                    // The cursor goes with the finger, so a toast about why
                    // it will not melt is followed by the stick already being
                    // on the thing that said so.
                    m_focus = Focus_Sockets;
                    m_socketPick = int(QuestRecord::kForgeSlots);
                    meltThem(app);
                    return;
                }
                if (tap.is(Zone_Filter) && tap.index >= 0 && tap.index <= Slot_Count) {
                    setFilter(tap.index);
                    return;
                }
                if (tap.is(Zone_Socket) && tap.index >= 0
                    && tap.index < int(QuestRecord::kForgeSlots)) {
                    m_focus = Focus_Sockets;
                    m_socketPick = tap.index;
                    pullOut(tap.index);
                    return;
                }
                if (tap.is(Zone_Row) && tap.index >= 0 && tap.index < int(m_shown.size())) {
                    m_focus = Focus_Bag;
                    m_pick = tap.index;
                    putIn(app);
                    return;
                }
                return;
            }

            if (input.back()) {
                app.popOverlay();
                return;
            }

            if (input.navLeft)
                m_focus = Focus_Sockets;
            if (input.navRight && !m_shown.empty())
                m_focus = Focus_Bag;
            if (input.navUp)
                m_focus == Focus_Sockets ? stepSocket(-1) : stepPick(-1);
            if (input.navDown)
                m_focus == Focus_Sockets ? stepSocket(1) : stepPick(1);

            // One verb per column, which is the shape the gear screen already
            // has: A puts a piece in from the bag, and takes one back out of
            // a socket.
            if (input.accept()) {
                if (m_focus == Focus_Bag)
                    putIn(app);
                else if (onMelt())
                    meltThem(app);
                else
                    pullOut(m_socketPick);
            }
            if (input.pressed(HidNpadButton_Y))
                meltThem(app);
            if (input.pressed(HidNpadButton_X))
                emptyIt();
            if (input.pressed(HidNpadButton_ZL))
                setFilter((m_filter + Slot_Count) % (Slot_Count + 1));
            if (input.pressed(HidNpadButton_ZR))
                setFilter((m_filter + 1) % (Slot_Count + 1));
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            app.touchZone(r.viewport(), Touch_None);

            const QuestRecord& record = QuestRecord::get();
            bool ready = record.check(m_socket) == QuestRecord::Forge::Ready;

            if (m_focus == Focus_Bag && !m_shown.empty())
                app.hint("A", "into the forge");
            else if (m_focus == Focus_Sockets && onMelt())
                app.hint("A", "melt them down");
            else if (m_focus == Focus_Sockets && m_socket[m_socketPick] != 0)
                app.hint("A", "take it back");
            // Y from anywhere, for somebody who has already put three in and
            // does not want to walk the cursor down to say so. Not shown when
            // the cursor is on the button, where A says it already.
            if (ready && !(m_focus == Focus_Sockets && onMelt()))
                app.hint("Y", "melt them down");
            if (anyIn())
                app.hint("X", "empty it");
            app.hint("ZL/ZR", "which peg");
            app.hint("B", "back");

            drawHeader(app, r);
            drawSockets(app, r, record);
            drawVerdict(app, r, record);
            drawFilters(app, r);
            drawList(app, r);
        }

    private:
        enum Focus : uint8_t { Focus_Sockets, Focus_Bag };

        static constexpr float kTop = 236.0f;
        static constexpr float kRow = 92.0f;
        static constexpr float kScrollGutter = 44.0f;
        static constexpr float kFilterY = 150.0f;
        static constexpr float kFilterH = 52.0f;
        // The sockets and the answer on the left, the bag on the right. Wide
        // enough for a name, a rank and a line of stats without ellipsis.
        static constexpr float kLeft = 760.0f;
        static constexpr float kSocketH = 116.0f;
        static constexpr float kVerdictH = 260.0f;
        static constexpr float kButtonH = 52.0f;

        // The left column is the three sockets and then the answer card with
        // the button on it, which is a row like any other so that the stick
        // reaches it.
        static constexpr int kRows = int(QuestRecord::kForgeSlots) + 1;
        bool onMelt() const { return m_socketPick >= int(QuestRecord::kForgeSlots); }

        // Everything spare that passes the peg filter, best first - and never
        // anything already in a socket, because a list that still offers what
        // it has just taken is a list somebody will press twice.
        //
        // Spare only: a worn piece and a locked piece are both refused by the
        // record, so offering either here would be offering a button that
        // does nothing.
        void gather()
        {
            m_shown.clear();
            const QuestRecord& record = QuestRecord::get();
            for (const Item& item : record.items()) {
                if (m_filter != 0 && item.slot != m_filter - 1)
                    continue;
                if (!record.spare(item))
                    continue;
                if (inSocket(item.id))
                    continue;
                m_shown.push_back(item);
            }
            std::stable_sort(m_shown.begin(), m_shown.end(),
                [](const Item& a, const Item& b) {
                    if (a.quality != b.quality)
                        return a.quality > b.quality;
                    return itemRating(a) > itemRating(b);
                });
            if (m_pick >= int(m_shown.size()))
                m_pick = std::max(0, int(m_shown.size()) - 1);

            // A piece can leave the bag while this screen is open - the gear
            // screen is underneath it and the record is one object - so a
            // socket holding something that is no longer spare is emptied
            // rather than left pointing at a ghost. This is also what clears
            // the forge after it has been used, since the three that went in
            // are not in the bag any more.
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++) {
                if (m_socket[i] == 0)
                    continue;
                const Item* held = record.find(m_socket[i]);
                if (!held || !record.spare(*held))
                    m_socket[i] = 0;
            }

            // Which piece the forge has just made, without the confirmation
            // having to reach back into this screen to say so: the record
            // counts what it takes in, so a count that has moved since the
            // last frame is a new piece, and the record says which. Not the
            // highest id, which is only the newest until the ids run out and
            // start being reused.
            uint32_t added = record.added();
            if (!m_seenAny) {
                m_seenAny = true; // the first frame: nothing here is new
                m_added = added;
            } else if (added != m_added) {
                m_added = added;
                if (record.find(record.lastAdded()))
                    m_made = record.lastAdded();
            }
        }

        bool inSocket(uint16_t id) const
        {
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++) {
                if (m_socket[i] == id)
                    return true;
            }
            return false;
        }

        bool anyIn() const
        {
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++) {
                if (m_socket[i] != 0)
                    return true;
            }
            return false;
        }

        void setFilter(int which)
        {
            if (which == m_filter)
                return;
            m_filter = which;
            m_pick = 0;
            m_scroll.stop();
            m_scroll.centerOn(0.0f);
        }

        void stepPick(int by)
        {
            if (m_shown.empty())
                return;
            int count = int(m_shown.size());
            m_pick = (m_pick + by % count + count) % count;
            m_scroll.centerOn(float(m_pick) * kRow + kRow * 0.5f);
        }

        void stepSocket(int by)
        {
            m_socketPick = (m_socketPick + by % kRows + kRows) % kRows;
        }

        // Into the first empty socket. Not into the focused one: somebody
        // filling a forge is adding a third piece, not choosing which hole it
        // goes in, and the order they sit in changes nothing about what comes
        // out.
        void putIn(App& app)
        {
            if (m_shown.empty())
                return;
            const Item& item = m_shown[size_t(m_pick)];
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++) {
                if (m_socket[i] != 0)
                    continue;
                m_socket[i] = item.id;
                m_socketPick = int(i);
                playSfx(Sfx::Select);
                return;
            }
            app.toast(tr("The forge is full"),
                tr("Take one back out first, or melt what is in it."));
        }

        void pullOut(int which)
        {
            if (which < 0 || which >= int(QuestRecord::kForgeSlots))
                return;
            if (m_socket[which] == 0)
                return;
            m_socket[which] = 0;
            playSfx(Sfx::Back);
        }

        void emptyIt()
        {
            if (!anyIn())
                return;
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++)
                m_socket[i] = 0;
            playSfx(Sfx::Back);
        }

        // Asked for, because it destroys three pieces and the app asks before
        // anything else that does. The sentence names what goes in and what
        // comes out, since by the time the box is up the sockets are behind
        // a scrim.
        void meltThem(App& app)
        {
            QuestRecord& record = QuestRecord::get();
            uint8_t quality = 0;
            int slot = -1;
            if (!record.plan(m_socket, quality, slot)) {
                sayWhyNot(app, record.check(m_socket));
                return;
            }

            const Item* first = record.find(m_socket[0]);
            std::string from = tr(qualityName(first->quality));
            std::string into = tr(qualityName(quality));
            std::string title = slot < 0
                ? format(tr("Melt three %s into one %s?"), from.c_str(), into.c_str())
                : format(tr("Melt three %s into one %s %s?"), from.c_str(),
                    into.c_str(), tr(slotName(uint8_t(slot))));

            uint32_t at = std::max(1u, record.deepest());
            uint16_t sockets[QuestRecord::kForgeSlots];
            for (size_t i = 0; i < QuestRecord::kForgeSlots; i++)
                sockets[i] = m_socket[i];

            app.askConfirm(title,
                format(tr("The three go for good, and what comes out is rolled "
                          "fresh - it could be worse than any of them. It is "
                          "rolled at floor %u, the deepest you have reached."),
                    unsigned(at)),
                // Nothing of this scene is captured. The dialog is modal and
                // the scene cannot be popped underneath it, so a captured
                // `this` would work - but it would be a promise about
                // lifetimes that this file is in no position to make, and
                // there is nothing here it needs. The sockets empty
                // themselves, because gather() drops any socket whose piece
                // is no longer spare and all three have just left the bag;
                // and the new piece is found by the record's count of what it
                // has taken in, which has just moved.
                tr("Melt them down"), [sockets, at]() {
                    QuestRecord& record = QuestRecord::get();
                    if (record.forge(sockets, at) == 0)
                        return;
                    record.flush();
                    playSfx(Sfx::Trophy);
                });
        }

        void sayWhyNot(App& app, QuestRecord::Forge why)
        {
            switch (why) {
            case QuestRecord::Forge::Empty:
            case QuestRecord::Forge::Short:
                app.toast(tr("Not enough in the forge"),
                    tr("It takes three pieces of the same rank to make one of "
                       "the next."));
                break;
            case QuestRecord::Forge::Mixed:
                app.toast(tr("Not the same rank"),
                    tr("All three have to be the same rank. What comes out is "
                       "the one above it."));
                break;
            case QuestRecord::Forge::Top:
                app.toast(tr("Nothing above godlike"),
                    tr("There is no rank to melt them into. A whetstone is what "
                       "improves one of those."));
                break;
            default:
                break;
            }
        }

        // ---------------------------------------------------------- drawing

        void drawHeader(App& app, Renderer& r) const
        {
            constexpr float kMark = 72.0f;
            ui::icon(r, Rect { theme::edge, 50.0f, kMark, kMark }, ui::Icon::Anvil,
                theme::accent);
            float textX = theme::edge + kMark + theme::s5;

            TextStyle label;
            label.size = theme::textSm;
            label.weight = FontWeight::Bold;
            label.color = theme::accent;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            r.text(textX, 44.0f, tr("the forge"), label);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            r.text(textX, 74.0f, tr("Three of a rank make one of the next"), title);

            Rect back { Renderer::DesignWidth - theme::edge - 42.0f, 44.0f, 42.0f,
                42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        void drawSockets(App& app, Renderer& r, const QuestRecord& record)
        {
            for (int i = 0; i < int(QuestRecord::kForgeSlots); i++) {
                Rect box { theme::edge, kTop + float(i) * (kSocketH + theme::s3),
                    kLeft - theme::s6, kSocketH };
                app.touchZone(box, Zone_Socket, i);
                bool here = m_focus == Focus_Sockets && i == m_socketPick;
                const Item* held = record.find(m_socket[i]);

                ui::card(r, box, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                    held ? theme::bg2 : theme::bg1, theme::r3);
                if (!held) {
                    // A dashed outline would be the obvious thing and the app
                    // has no dashes; a second stroke inside the card reads as
                    // a hole well enough and uses what there is.
                    r.strokeRect(box.inset(theme::s4), theme::r2, theme::stroke,
                        theme::stroke2);
                }
                Rect inner = box.inset(theme::s5, theme::s4);

                TextStyle which;
                which.size = theme::textXs;
                which.color = theme::fg3;
                which.tracking = theme::trackingWide;
                which.uppercase = true;
                r.text(inner.x, inner.y, format(tr("socket %d"), i + 1), which);

                TextStyle noun;
                noun.size = theme::textBase;
                noun.weight = FontWeight::Bold;
                noun.color = held ? theme::fg1 : theme::fg4;
                r.text(inner.x, inner.y + 28.0f,
                    held ? tr(itemNoun(*held)) : tr("empty"), noun);

                if (!held)
                    continue;

                TextStyle tier;
                tier.size = theme::textXs;
                tier.weight = FontWeight::Bold;
                tier.color = ui::qualityColour(held->quality);
                tier.tracking = theme::trackingWide;
                tier.uppercase = true;
                r.text(Rect { inner.x, inner.y + 28.0f, inner.w, 28.0f },
                    tr(qualityName(held->quality)), tier, Align::Right, VAlign::Top);

                TextStyle gain;
                gain.size = theme::textXs;
                gain.color = theme::fg3;
                r.text(inner.x, inner.y + 62.0f, itemSummary(*held), gain);

                TextStyle peg;
                peg.size = theme::textXs;
                peg.color = theme::fg4;
                peg.tracking = theme::trackingWide;
                peg.uppercase = true;
                r.text(Rect { inner.x, inner.y + 62.0f, inner.w, 26.0f },
                    tr(slotName(held->slot)), peg, Align::Right, VAlign::Top);
            }
        }

        // What it would make, or why it would not - one card, always in the
        // same place, so the answer never moves about.
        void drawVerdict(App& app, Renderer& r, const QuestRecord& record)
        {
            float y = kTop + float(QuestRecord::kForgeSlots) * (kSocketH + theme::s3)
                + theme::s5;
            Rect box { theme::edge, y, kLeft - theme::s6, kVerdictH };
            bool here = m_focus == Focus_Sockets && onMelt();
            ui::card(r, box, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                here ? theme::bg2 : theme::bg1, theme::r3);
            Rect inner = box.inset(theme::s5, theme::s5);

            QuestRecord::Forge why = record.check(m_socket);
            uint8_t quality = 0;
            int slot = -1;

            TextStyle head;
            head.size = theme::textXs;
            head.color = theme::fg3;
            head.tracking = theme::trackingWide;
            head.uppercase = true;

            TextStyle body;
            body.size = theme::textBase;
            body.weight = FontWeight::Bold;

            if (record.plan(m_socket, quality, slot)) {
                r.text(inner.x, inner.y, tr("what comes out"), head);
                body.color = ui::qualityColour(quality);
                // Wrapped, not placed. "one epic, on a peg of its own
                // choosing" fits on one line in English and in none of the
                // languages that say it the long way round, and r.text()
                // would walk it straight off the side of the card.
                r.textWrapped(Rect { inner.x, inner.y + 32.0f, inner.w, 68.0f },
                    slot < 0 ? format(tr("one %s, on a peg of its own choosing"),
                        tr(qualityName(quality)))
                             : format(tr("one %s %s"), tr(qualityName(quality)),
                                 tr(slotName(uint8_t(slot)))),
                    body, 2);

                TextStyle note;
                note.size = theme::textXs;
                note.color = theme::fg3;
                r.textWrapped(Rect { inner.x, inner.y + 104.0f, inner.w, 44.0f },
                    slot < 0
                        ? tr("Three pieces on one peg would make a fourth on "
                             "that peg instead.")
                        : tr("All three sit on the same peg, so what comes out "
                             "does too."),
                    note, 2);

                Rect button { inner.x, inner.bottom() - kButtonH, 300.0f, kButtonH };
                app.touchZone(button, Zone_Melt);
                ui::pill(r, button, tr("Melt them down"), theme::bg0, theme::accent);
                return;
            }

            r.text(inner.x, inner.y, tr("not yet"), head);
            body.color = theme::fg2;
            const char* said = tr("Put three pieces of the same rank in.");
            switch (why) {
            case QuestRecord::Forge::Short:
                said = tr("Three of a rank. Two is not a recipe.");
                break;
            case QuestRecord::Forge::Mixed:
                said = tr("They are not all the same rank.");
                break;
            case QuestRecord::Forge::Top:
                said = tr("Nothing goes above godlike.");
                break;
            default:
                break;
            }
            r.textWrapped(Rect { inner.x, inner.y + 32.0f, inner.w, 120.0f }, said,
                body, 3);
        }

        void drawFilters(App& app, Renderer& r)
        {
            float x = kLeft + theme::edge;
            for (int i = 0; i <= Slot_Count; i++) {
                std::string label = i == 0 ? std::string(tr("everything"))
                                           : std::string(tr(slotName(uint8_t(i - 1))));
                float width = ui::segmentWidth(r, label.c_str());
                Rect chip { x, kFilterY, width, kFilterH };
                app.touchZone(chip, Zone_Filter, i);
                bool here = i == m_filter;
                ui::pill(r, chip, label, here ? theme::bg0 : theme::fg3,
                    here ? theme::accent : theme::bg2);
                x += width + theme::s3;
            }
        }

        void drawList(App& app, Renderer& r)
        {
            Rect list { kLeft + theme::edge, kTop,
                Renderer::DesignWidth - kLeft - theme::edge * 2.0f,
                Renderer::DesignHeight - kTop - 120.0f };
            m_listArea = list;

            if (m_shown.empty()) {
                TextStyle empty;
                empty.size = theme::textBase;
                empty.color = theme::fg4;
                r.textWrapped(Rect { list.x, list.y + 40.0f, list.w, 90.0f },
                    tr("Nothing spare on this peg. Worn and locked pieces stay "
                       "out of the forge."),
                    empty, 3);
                return;
            }

            float total = float(m_shown.size()) * kRow;
            m_scroll.setBounds(list.h, total);

            r.pushClipVertical(list.inset(0.0f, -theme::focusRoom));
            float y = list.y - m_scroll.offset();
            for (int i = 0; i < int(m_shown.size()); i++) {
                Rect row { list.x, y, list.w - kScrollGutter, kRow - theme::s2 };
                if (row.bottom() >= list.y - theme::focusRoom
                    && row.y <= list.bottom() + theme::focusRoom)
                    drawRow(app, r, row, i);
                y += kRow;
            }
            r.popClip();

            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { list.right() - 8.0f, list.y, 8.0f, list.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

        void drawRow(App& app, Renderer& r, const Rect& box, int index)
        {
            const Item& item = m_shown[size_t(index)];
            bool here = m_focus == Focus_Bag && index == m_pick;
            app.touchZone(box, Zone_Row, index);
            ui::card(r, box, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                here ? theme::bg2 : theme::bg1, theme::r2);
            Rect inner = box.inset(theme::s5, theme::s3);

            TextStyle noun;
            noun.size = theme::textBase;
            noun.weight = FontWeight::Bold;
            noun.color = theme::fg1;
            r.text(inner.x, inner.y, tr(itemNoun(item)), noun);

            // The piece the forge has just made, marked until something else
            // is picked up: a new item lands in a sorted list wherever its
            // numbers put it, which is nowhere in particular.
            if (item.id == m_made) {
                ui::icon(r, Rect { inner.x + 240.0f, inner.y, 28.0f, 28.0f },
                    ui::Icon::ArrowUp, theme::success, 2.5f);
            }

            TextStyle tier;
            tier.size = theme::textXs;
            tier.weight = FontWeight::Bold;
            tier.color = ui::qualityColour(item.quality);
            tier.tracking = theme::trackingWide;
            tier.uppercase = true;
            r.text(Rect { inner.x, inner.y, inner.w, 26.0f },
                tr(qualityName(item.quality)), tier, Align::Right, VAlign::Top);

            TextStyle gain;
            gain.size = theme::textXs;
            gain.color = theme::fg3;
            r.text(inner.x, inner.y + 34.0f, itemSummary(item), gain);

            TextStyle peg;
            peg.size = theme::textXs;
            peg.color = theme::fg4;
            peg.tracking = theme::trackingWide;
            peg.uppercase = true;
            r.text(Rect { inner.x, inner.y + 34.0f, inner.w, 26.0f },
                tr(slotName(item.slot)), peg, Align::Right, VAlign::Top);
        }

        uint16_t m_socket[QuestRecord::kForgeSlots] = {};
        int m_socketPick = 0;
        uint16_t m_made = 0;  // the last thing forged, marked in the list
        uint32_t m_added = 0;   // the record's count of arrivals, last frame
        bool m_seenAny = false; // and whether there has been a last frame

        uint8_t m_focus = Focus_Bag;
        std::vector<Item> m_shown;
        int m_filter = 0; // 0 is everything, otherwise the slot plus one
        int m_pick = 0;
        float m_pulse = 0.0f;

        ui::ScrollView m_scroll;
        Rect m_listArea {};
        bool m_dragging = false;
        bool m_braked = false;
    };
}

std::unique_ptr<Scene> makeQuestForgeScene() { return std::make_unique<ForgeScene>(); }

} // namespace nxp
