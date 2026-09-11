#include "app.h"
#include "core/i18n.h"
#include "core/quest_record.h"
#include "core/quest_rules.h"
#include "core/wallet.h"
#include "core/store.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace nxp {

namespace {

    // Everything the tower has given up, in one list.
    //
    // The gear screen answers "what else could go on this peg", which means
    // it only ever shows one slot for one person and hides the rest. This
    // answers the other question - what have I actually got - and it is the
    // one you ask when you are deciding whether a climb was worth it, or
    // what to throw away, or simply looking at your things.
    //
    // Read-only but for throwing away, on purpose: equipping needs a person
    // to equip, and asking which one from here would be building the gear
    // screen again in the wrong order.
    class BagScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Row = Touch_SceneBase,
            Zone_Filter,
            Zone_Back,
            Zone_Forge,
        };

        bool coversChrome() const override { return true; }

        void onEnter(App& app) override
        {
            m_filter = 0;
            m_pick = 0;
            m_scroll.stop();

            // Who is who. QuestRecord keys a wearer by the crossing's
            // public id, because that is the only name for somebody that
            // survives them changing their handle - but an id is not a
            // thing to show anybody, so the handles are read once here.
            // The collection cannot change while this screen is open.
            m_names.clear();
            for (const Crossing& c : app.store().crossings()) {
                m_names.push_back({ c.id,
                    c.pass.handle.empty() ? std::string(tr("A stranger"))
                                          : c.pass.handle });
            }
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
            bool tapped = app.takeTap(tap);
            if (!m_forge && tapped && !m_braked) {
                if (tap.is(Zone_Back))
                    app.popOverlay();
                else if (tap.is(Zone_Filter) && tap.index >= 0
                    && tap.index <= Slot_Count)
                    setFilter(tap.index);
                else if (tap.is(Zone_Row) && tap.index >= 0
                    && tap.index < int(m_shown.size()))
                    m_pick = tap.index;
                return;
            }

            if (m_forge) {
                if (input.back())
                    m_forge = false;
                else if (input.accept() || (tapped && tap.is(Zone_Forge)))
                    rollOnce();
                return;
            }

            if (input.back()) {
                app.popOverlay();
                return;
            }
            if (input.navLeft)
                setFilter((m_filter + Slot_Count) % (Slot_Count + 1));
            if (input.navRight)
                setFilter((m_filter + 1) % (Slot_Count + 1));
            if (input.navUp)
                step(-1);
            if (input.navDown)
                step(1);
            if (input.pressed(HidNpadButton_ZL))
                flipLock();
            if (input.pressed(HidNpadButton_X))
                throwAway(app);
            if (input.pressed(HidNpadButton_Y))
                openForge(app);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            app.touchZone(r.viewport(), Touch_None);

            if (m_forge) {
                drawHeader(app, r);
                drawFilters(app, r);
                drawList(app, r);
                drawForge(app, r);
                return;
            }

            if (!m_shown.empty()) {
                app.hint("ZL", m_shown[size_t(m_pick)].locked ? "unlock" : "lock");
                if (!m_shown[size_t(m_pick)].locked)
                    app.hint("X", "throw away");
                app.hint("Y", "roll it again");
            }
            app.hint("B", "back");

            drawHeader(app, r);
            drawFilters(app, r);
            drawList(app, r);
        }

    private:
        static constexpr float kTop = 236.0f;
        static constexpr float kRow = 92.0f;
        static constexpr float kFilterY = 150.0f;
        static constexpr float kFilterH = 52.0f;

        // Everything that passes the filter, the best of it first. Rebuilt
        // every frame because throwing something away changes it and the
        // list is at most a hundred and twenty rows of eight bytes.
        void gather()
        {
            m_shown.clear();
            for (const Item& item : QuestRecord::get().items()) {
                if (m_filter != 0 && item.slot != m_filter - 1)
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

        void step(int by)
        {
            if (m_shown.empty())
                return;
            int count = int(m_shown.size());
            m_pick = (m_pick + by % count + count) % count;
            m_scroll.centerOn(float(m_pick) * kRow + kRow * 0.5f);
        }

        // A whetstone, spent here rather than in the shop, because this is
        // the only screen that has the piece in front of it.
        //
        // Y opens the comparison with the piece as it stands and nothing
        // beside it; the roll is a button inside, the way a bet is a button
        // on the race. That way the before is on screen when you commit to
        // losing it, and a second stone can be spent without closing
        // anything - the right card becomes the left one and the new roll
        // takes its place.
        void openForge(App& app)
        {
            if (m_shown.empty())
                return;
            const Item& item = m_shown[size_t(m_pick)];
            if (QuestRecord::get().stones() == 0) {
                app.toast(tr("No whetstones"),
                    format(tr("They are %u coins in the shop."),
                        unsigned(Wallet::kWhetstonePrice)));
                return;
            }
            m_was = item;
            m_now = Item {};
            m_forge = true;
        }

        void rollOnce()
        {
            QuestRecord& record = QuestRecord::get();
            if (!m_forge || record.stones() == 0)
                return;
            // The right-hand card slides left, so the comparison is always
            // this roll against the one before it rather than against
            // wherever the piece started.
            uint16_t id = m_now.valid() ? m_now.id : m_was.id;
            if (m_now.valid())
                m_was = m_now;
            if (!record.reforge(id))
                return;
            record.flush();
            if (const Item* now = record.find(id))
                m_now = *now;
        }

        // Kept, or not. A locked piece survives everything that removes
        // gear without being asked - the sweep, and the eviction a full bag
        // makes on its own - which is what the lock is for.
        void flipLock()
        {
            if (m_shown.empty())
                return;
            const Item& item = m_shown[size_t(m_pick)];
            QuestRecord& record = QuestRecord::get();
            record.setLocked(item.id, !item.locked);
            record.flush();
        }

        void throwAway(App& app)
        {
            if (m_shown.empty())
                return;
            const Item gone = m_shown[size_t(m_pick)];
            if (gone.locked) {
                app.toast(tr("That one is locked"),
                    tr("Unlock it first, then throw it away."));
                return;
            }
            std::string owner;
            if (QuestRecord::get().wearer(gone.id, owner)) {
                // Refused rather than quietly stripped: taking a piece off
                // somebody is a decision, and this screen does not know
                // enough about the party to make it for you.
                app.toast(tr("Somebody is wearing that"),
                    tr("Take it off in the gear screen first."));
                return;
            }
            std::string noun = tr(itemNoun(gone));
            std::string quality = tr(qualityName(gone.quality));
            app.askConfirm(format(tr("Throw away the %s?"), noun.c_str()),
                format(tr("A %s piece from floor %u. It does not come back."),
                    quality.c_str(), unsigned(gone.floor)),
                tr("Throw it away"), [id = gone.id]() {
                    QuestRecord& record = QuestRecord::get();
                    record.discard(id);
                    record.flush();
                });
        }

        // The empty string is your own Mii. An id with nobody behind it
        // is somebody who has dropped off the end of the collection and
        // whose gear has not been released yet - they are still wearing
        // it, and saying so is more use than saying nothing.
        std::string nameOf(const std::string& id) const
        {
            if (id.empty())
                return tr("You");
            for (const auto& row : m_names) {
                if (row.first == id)
                    return row.second;
            }
            return tr("Somebody who has gone");
        }

        // ---------------------------------------------------------- drawing

        // The piece, and what a whetstone made of it, over a veil - the
        // same shape the quest uses when it offers a blessing. The roll is
        // a button inside rather than a dialog outside, so what you are
        // about to lose is on screen while you decide to lose it.
        void drawForge(App& app, Renderer& r)
        {
            app.hint("B", "close");
            r.rect(r.viewport(), theme::scrim);

            constexpr float kCard = 400.0f;
            constexpr float kCardH = 190.0f;
            constexpr float kButton = 72.0f;
            float boxW = kCard * 2.0f + theme::s5 + theme::s7 * 2.0f;
            float boxH = 44.0f + theme::s5 + kCardH + theme::s5 + 34.0f + theme::s5
                + kButton + theme::s7 * 2.0f;
            Rect box { Renderer::DesignWidth * 0.5f - boxW * 0.5f,
                Renderer::DesignHeight * 0.5f - boxH * 0.5f, boxW, boxH };
            r.roundRect(box, theme::r5, theme::bg1);
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s7);

            TextStyle head;
            head.size = theme::textLg;
            head.weight = FontWeight::Bold;
            head.color = theme::fg1;
            head.tracking = theme::trackingTight;
            r.text(Rect { inner.x, inner.y, inner.w, 44.0f }, tr(itemNoun(m_was)),
                head, Align::Center, VAlign::Top);

            float y = inner.y + 44.0f + theme::s5;
            bool rolled = m_now.valid();
            drawSide(r, Rect { inner.x, y, kCard, kCardH }, m_was,
                rolled ? tr("before") : tr("as it stands"), !rolled);
            if (rolled) {
                drawSide(r, Rect { inner.x + kCard + theme::s5, y, kCard, kCardH },
                    m_now, tr("after"), true);
            } else {
                drawEmptySide(r,
                    Rect { inner.x + kCard + theme::s5, y, kCard, kCardH });
            }

            // The verdict, because the numbers alone do not give one: a
            // roll can gain attack and lose more health than it gained, and
            // itemRating already weighs a point of health at a quarter of
            // everything else.
            float verdictY = y + kCardH + theme::s5;
            if (rolled) {
                uint32_t was = itemRating(m_was);
                uint32_t now = itemRating(m_now);
                TextStyle verdict;
                verdict.size = theme::textBase;
                verdict.weight = FontWeight::Bold;
                verdict.color = now > was ? theme::success
                                          : (now < was ? theme::danger : theme::fg3);
                r.text(Rect { inner.x, verdictY, inner.w, 34.0f },
                    now > was ? tr("It came out better.")
                              : (now < was ? tr("It came out worse.")
                                           : tr("It came out much the same.")),
                    verdict, Align::Center, VAlign::Top);
            }

            uint16_t left = QuestRecord::get().stones();
            TextStyle note;
            note.size = theme::textSm;
            note.color = theme::fg4;
            if (left == 0) {
                r.text(Rect { inner.x, verdictY + 40.0f, inner.w, 30.0f },
                    format(tr("They are %u coins in the shop."),
                        unsigned(Wallet::kWhetstonePrice)),
                    note, Align::Center, VAlign::Top);
                return;
            }

            std::string label = tr("Use a whetstone");
            Rect roll { inner.centerX() - ui::actionButtonWidth(r, label) * 0.5f,
                inner.bottom() - kButton, ui::actionButtonWidth(r, label), kButton };
            app.touchZone(roll, Zone_Forge);
            ui::actionButton(r, roll, label, true,
                app.touchHeld(Zone_Forge) ? 1.0f : 0.7f + 0.3f * m_pulse);
            r.text(Rect { inner.x, roll.y - 30.0f, inner.w, 26.0f },
                format(tr("%u left"), unsigned(left)), note, Align::Center,
                VAlign::Top);
        }

        // The right-hand card before anything has been rolled into it.
        static void drawEmptySide(Renderer& r, const Rect& box)
        {
            r.roundRect(box, theme::r3, theme::bg1.scaleAlpha(0.5f));
            r.strokeRect(box, theme::r3, theme::stroke, theme::stroke1);
            TextStyle waiting;
            waiting.size = theme::textSm;
            waiting.color = theme::fg4;
            r.text(box.inset(theme::s5), tr("whatever it becomes"), waiting,
                Align::Center, VAlign::Middle);
        }

        static void drawSide(Renderer& r, const Rect& box, const Item& item,
            const std::string& when, bool lit)
        {
            r.roundRect(box, theme::r3, lit ? theme::bg3 : theme::bg2);
            Rect inner = box.inset(theme::s5, theme::s4);

            TextStyle label;
            label.size = theme::textXs;
            label.color = theme::fg4;
            label.tracking = theme::trackingWide;
            label.uppercase = true;
            r.text(inner.x, inner.y, when, label);

            TextStyle tier;
            tier.size = theme::textXs;
            tier.weight = FontWeight::Bold;
            tier.color = ui::qualityColour(item.quality);
            tier.tracking = theme::trackingWide;
            tier.uppercase = true;
            r.text(Rect { inner.x, inner.y, inner.w, 24.0f },
                tr(qualityName(item.quality)), tier, Align::Right, VAlign::Top);

            TextStyle body;
            body.size = theme::textBase;
            body.color = lit ? theme::fg1 : theme::fg3;
            body.leading = theme::leadingNormal;
            r.textWrapped(Rect { inner.x, inner.y + 38.0f, inner.w, 110.0f },
                itemSummary(item), body, 3);
        }


        void drawHeader(App& app, Renderer& r) const
        {
            TextStyle label;
            label.size = theme::textSm;
            label.weight = FontWeight::Bold;
            label.color = theme::accent;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            r.text(theme::edge, 44.0f, tr("in the bag"), label);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            r.text(theme::edge, 74.0f,
                format(tr("%zu of %zu in the bag"), QuestRecord::get().items().size(),
                    QuestRecord::kBagLimit),
                title);

            Rect back { Renderer::DesignWidth - theme::edge - 42.0f, 44.0f, 42.0f,
                42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        // All of it, or one peg's worth. Chips rather than a menu, because
        // there are five and they all fit.
        void drawFilters(App& app, Renderer& r)
        {
            float x = theme::edge;
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
            Rect list { theme::edge, kTop, Renderer::DesignWidth - theme::edge * 2.0f,
                Renderer::DesignHeight - kTop - 120.0f };
            m_listArea = list;

            if (m_shown.empty()) {
                TextStyle empty;
                empty.size = theme::textBase;
                empty.color = theme::fg4;
                r.text(Rect { list.x, list.y + 40.0f, list.w, 40.0f },
                    tr("Nothing here yet. The tower is where it comes from."), empty);
                return;
            }

            float total = float(m_shown.size()) * kRow;
            m_scroll.setBounds(list.h, total);

            r.pushClipVertical(list.inset(0.0f, -theme::focusRoom));
            float y = list.y - m_scroll.offset();
            const QuestRecord& record = QuestRecord::get();
            for (int i = 0; i < int(m_shown.size()); i++) {
                Rect row { list.x, y, list.w - 24.0f, kRow - theme::s2 };
                if (row.bottom() >= list.y - theme::focusRoom
                    && row.y <= list.bottom() + theme::focusRoom)
                    drawRow(app, r, row, i, record);
                y += kRow;
            }
            r.popClip();

            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { list.right() - 8.0f, list.y, 8.0f, list.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

        void drawRow(App& app, Renderer& r, const Rect& box, int index,
            const QuestRecord& record)
        {
            const Item& item = m_shown[size_t(index)];
            bool here = index == m_pick;
            app.touchZone(box, Zone_Row, index);
            ui::card(r, box, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                here ? theme::bg2 : theme::bg1, theme::r2);
            Rect inner = box.inset(theme::s5, theme::s3);

            TextStyle noun;
            noun.size = theme::textBase;
            noun.weight = FontWeight::Bold;
            noun.color = theme::fg1;
            float nounX = inner.x;
            if (item.locked) {
                ui::icon(r, Rect { inner.x, inner.y + 2.0f, 24.0f, 24.0f },
                    ui::Icon::Lock, theme::accent, 2.0f);
                nounX += 32.0f;
            }
            r.text(nounX, inner.y, tr(itemNoun(item)), noun);

            TextStyle peg;
            peg.size = theme::textXs;
            peg.color = theme::fg4;
            peg.tracking = theme::trackingWide;
            peg.uppercase = true;
            r.text(inner.x + 220.0f, inner.y + 6.0f, tr(slotName(item.slot)), peg);

            TextStyle gain;
            gain.size = theme::textXs;
            gain.color = theme::fg3;
            r.text(inner.x, inner.y + 34.0f, itemSummary(item), gain);

            TextStyle tier;
            tier.size = theme::textXs;
            tier.weight = FontWeight::Bold;
            tier.color = ui::qualityColour(item.quality);
            tier.tracking = theme::trackingWide;
            tier.uppercase = true;
            r.text(Rect { inner.x, inner.y, inner.w, 26.0f },
                tr(qualityName(item.quality)), tier, Align::Right, VAlign::Top);

            // Who has it, or where it fell. Both answer "can I get rid of
            // this", which is the only decision this screen offers.
            std::string owner;
            TextStyle note;
            note.size = theme::textXs;
            note.color = theme::fg4;
            std::string tail = record.wearer(item.id, owner)
                ? format(tr("worn by %s"), nameOf(owner).c_str())
                : format(tr("from floor %u"), unsigned(item.floor));
            r.text(Rect { inner.x, inner.y + 34.0f, inner.w, 26.0f }, tail, note,
                Align::Right, VAlign::Top);
        }

        Item m_was {};        // the piece as it stands, or the last roll
        Item m_now {};        // what the newest roll made of it; id 0 until then
        bool m_forge = false; // whether the whetstone overlay is up

        std::vector<Item> m_shown;
        std::vector<std::pair<std::string, std::string>> m_names; // id -> handle
        int m_filter = 0; // 0 is everything, otherwise the slot plus one
        int m_pick = 0;
        float m_pulse = 0.0f;

        ui::ScrollView m_scroll;
        Rect m_listArea {};
        bool m_dragging = false;
        bool m_braked = false;
    };
}

std::unique_ptr<Scene> makeQuestBagScene() { return std::make_unique<BagScene>(); }

} // namespace nxp
