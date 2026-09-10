#include "app.h"
#include "core/i18n.h"
#include "core/quest_record.h"
#include "core/quest_rules.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nxp {

namespace {

    // Moving gear around, drawn over the quest's party screen.
    //
    // Three columns: who is being dressed, what they are wearing, and what is
    // in the bag that would fit the peg the cursor is on. The bag is filtered
    // to that peg rather than listed whole, because the only question this
    // screen ever answers is "what else could go here", and a list of
    // everything makes the reader do the filtering.
    //
    // Nothing here decides anything about a fight. It writes into
    // QuestRecord, the quest scene re-reads its party every frame, and the
    // two never have to agree about anything more than that.
    class GearScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Peg = Touch_SceneBase,
            Zone_Bag,
            Zone_Who,
            Zone_Back,
        };

        explicit GearScene(std::vector<GearPerson> party)
            : m_party(std::move(party))
        {
        }

        bool coversChrome() const override { return true; }

        void onEnter(App&) override
        {
            m_who = 0;
            m_peg = 0;
            m_pick = 0;
        }

        void update(App& app, const Input& input, float dt) override
        {
            (void)dt;
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            refill();

            TouchTarget tap;
            if (app.takeTap(tap)) {
                if (tap.is(Zone_Back))
                    app.popOverlay();
                else if (tap.is(Zone_Who) && tap.index >= 0
                    && tap.index < int(m_party.size())) {
                    m_who = tap.index;
                    m_pick = 0;
                } else if (tap.is(Zone_Peg) && tap.index >= 0
                    && tap.index < Slot_Count) {
                    m_peg = tap.index;
                    m_pick = 0;
                } else if (tap.is(Zone_Bag) && tap.index >= 0
                    && tap.index < int(m_fits.size())) {
                    m_pick = tap.index;
                    wear();
                }
                return;
            }

            if (input.back()) {
                app.popOverlay();
                return;
            }
            if (m_party.empty())
                return;

            if (input.pressed(HidNpadButton_L))
                stepWho(-1);
            if (input.pressed(HidNpadButton_R))
                stepWho(1);
            if (input.navLeft)
                stepPeg(-1);
            if (input.navRight)
                stepPeg(1);
            if (input.navUp)
                stepPick(-1);
            if (input.navDown)
                stepPick(1);
            if (input.accept())
                wear();
            if (input.pressed(HidNpadButton_Y))
                bare();
            if (input.pressed(HidNpadButton_X))
                throwAway(app);
            if (input.pressed(HidNpadButton_ZR))
                sweep(app);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            app.touchZone(r.viewport(), Touch_None);

            app.hint("A", "wear it");
            app.hint("X", "throw away");
            app.hint("ZR", "clear out");
            app.hint("Y", "take it off");
            if (m_party.size() > 1)
                app.hint("L/R", "somebody else");
            app.hint("B", "back");

            drawHeader(app, r);
            drawWearer(r);
            drawPegs(app, r);
            drawBag(app, r);
        }

    private:
        static constexpr float kColumn = 520.0f;
        static constexpr float kTop = 176.0f;
        static constexpr float kPegH = 92.0f;
        static constexpr float kBagRow = 78.0f;

        const GearPerson& who() const { return m_party[size_t(m_who)]; }

        // What is in the bag that would go on the peg the cursor is on, best
        // first, and never anything already worn by somebody else: a piece
        // in use is not a piece you can pick up.
        void refill()
        {
            m_fits.clear();
            const QuestRecord& record = QuestRecord::get();
            if (m_party.empty())
                return;
            const std::string& owner = who().id;
            for (const Item& item : record.items()) {
                if (item.slot != m_peg)
                    continue;
                std::string wearer;
                if (record.wearer(item.id, wearer) && wearer != owner)
                    continue;
                m_fits.push_back(item);
            }
            std::stable_sort(m_fits.begin(), m_fits.end(),
                [](const Item& a, const Item& b) {
                    if (a.quality != b.quality)
                        return a.quality > b.quality;
                    return itemRating(a) > itemRating(b);
                });
            if (m_pick >= int(m_fits.size()))
                m_pick = std::max(0, int(m_fits.size()) - 1);
        }

        void stepWho(int by)
        {
            int count = int(m_party.size());
            m_who = (m_who + by % count + count) % count;
            m_pick = 0;
        }

        void stepPeg(int by)
        {
            m_peg = (m_peg + by % Slot_Count + Slot_Count) % Slot_Count;
            m_pick = 0;
        }

        void stepPick(int by)
        {
            if (m_fits.empty())
                return;
            int count = int(m_fits.size());
            m_pick = (m_pick + by % count + count) % count;
        }

        uint16_t wornHere() const
        {
            return QuestRecord::get().loadout(who().id).worn[m_peg];
        }

        void wear()
        {
            if (m_fits.empty() || m_party.empty())
                return;
            const Item& chosen = m_fits[size_t(m_pick)];
            QuestRecord& record = QuestRecord::get();
            // A second press on what is already on takes it off, so A alone
            // is the whole of the verb.
            uint16_t already = wornHere();
            record.equip(who().id, uint8_t(m_peg),
                already == chosen.id ? uint16_t(0) : chosen.id);
            record.flush();
        }

        void bare()
        {
            if (m_party.empty() || wornHere() == 0)
                return;
            QuestRecord& record = QuestRecord::get();
            record.equip(who().id, uint8_t(m_peg), 0);
            record.flush();
        }

        // Everything in the bag that could never be an upgrade for the
        // person on screen: worse than what they already have on the same
        // peg, and worn by nobody. A peg with nothing on it sets no
        // threshold, so an empty slot keeps its spares.
        //
        // Rating rather than quality decides it, because the tiers overlap
        // on purpose - a well-rolled rare can beat a poor epic, and sweeping
        // by tier would throw away the better piece.
        void sweep(App& app)
        {
            if (m_party.empty())
                return;
            QuestRecord& record = QuestRecord::get();
            QuestRecord::Loadout gear = record.loadout(who().id);
            size_t doomed = record.clearOut(gear, false);
            if (doomed == 0) {
                app.toast(tr("Nothing to clear out"),
                    tr("Everything spare is better than something being worn, or "
                       "sits on a peg with nothing on it."));
                return;
            }
            std::string name = who().name;
            app.askConfirm(format(tr("Throw away %zu pieces?"), doomed),
                format(tr("Everything in the bag that is worse than what %s has on. "
                          "Nothing being worn is touched."),
                    name.c_str()),
                tr("Throw them away"), [gear]() {
                    QuestRecord& record = QuestRecord::get();
                    record.clearOut(gear, true);
                    record.flush();
                });
        }

        void throwAway(App& app)
        {
            if (m_fits.empty())
                return;
            const Item gone = m_fits[size_t(m_pick)];
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

        // ---------------------------------------------------------- drawing

        void drawHeader(App& app, Renderer& r) const
        {
            TextStyle label;
            label.size = theme::textSm;
            label.weight = FontWeight::Bold;
            label.color = theme::accent;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            r.text(theme::edge, 44.0f, tr("what they carry"), label);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            r.text(theme::edge, 74.0f,
                m_party.empty() ? std::string(tr("Nobody to dress")) : who().name,
                title);

            TextStyle count;
            count.size = theme::textSm;
            count.color = theme::fg3;
            count.tracking = theme::trackingWide;
            r.text(Rect { 0.0f, 84.0f, Renderer::DesignWidth - theme::edge, 30.0f },
                format(tr("%zu of %zu in the bag"), QuestRecord::get().items().size(),
                    QuestRecord::kBagLimit),
                count, Align::Right, VAlign::Top);

            Rect back { Renderer::DesignWidth - theme::edge - 42.0f, 40.0f, 42.0f,
                42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        // Who, and what they come to once they are dressed.
        void drawWearer(Renderer& r) const
        {
            if (m_party.empty())
                return;
            Rect box { theme::edge, kTop, kColumn - theme::s6, 470.0f };
            ui::card(r, box, 0.0f, theme::bg1, theme::r3);
            Rect inner = box.inset(theme::s6, theme::s5);

            ui::miiHead(r, Rect { inner.centerX() - 76.0f, inner.y, 152.0f, 152.0f },
                who().face);

            TextStyle role;
            role.size = theme::textSm;
            role.color = theme::accent;
            role.tracking = theme::trackingWide;
            role.uppercase = true;
            r.text(Rect { inner.x, inner.y + 164.0f, inner.w, 28.0f },
                tr(className(classOf(who().face))), role, Align::Center, VAlign::Top);

            Sheet base = who().base;
            Sheet full = dressed();
            const char* names[5] = { "HP", "MP", "ATK", "DEF", "SPD" };
            uint16_t was[5] = { base.hp, base.mp, base.atk, base.def, base.spd };
            uint16_t now[5] = { full.hp, full.mp, full.atk, full.def, full.spd };

            float y = inner.y + 208.0f;
            for (int i = 0; i < 5; i++) {
                TextStyle key;
                key.size = theme::textSm;
                key.color = theme::fg3;
                key.tracking = theme::trackingWide;
                r.text(inner.x, y, names[i], key);

                TextStyle value;
                value.size = theme::textBase;
                value.weight = FontWeight::Bold;
                value.color = now[i] > was[i] ? theme::success : theme::fg1;
                std::string text = now[i] > was[i]
                    ? format("%u  (+%u)", unsigned(now[i]), unsigned(now[i] - was[i]))
                    : format("%u", unsigned(now[i]));
                r.text(Rect { inner.x, y - 2.0f, inner.w, 30.0f }, text, value,
                    Align::Right, VAlign::Top);
                y += 44.0f;
            }
        }

        Sheet dressed() const
        {
            Sheet out = who().base;
            const QuestRecord& record = QuestRecord::get();
            QuestRecord::Loadout gear = record.loadout(who().id);
            for (int slot = 0; slot < Slot_Count; slot++) {
                if (const Item* item = record.find(gear.worn[slot]))
                    out += itemBonus(*item);
            }
            return out;
        }

        // The four pegs.
        void drawPegs(App& app, Renderer& r)
        {
            if (m_party.empty())
                return;
            const QuestRecord& record = QuestRecord::get();
            QuestRecord::Loadout gear = record.loadout(who().id);

            float x = theme::edge + kColumn;
            for (int slot = 0; slot < Slot_Count; slot++) {
                Rect box { x, kTop + float(slot) * (kPegH + theme::s3),
                    kColumn - theme::s6, kPegH };
                app.touchZone(box, Zone_Peg, slot);
                bool here = slot == m_peg;
                ui::card(r, box, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                    here ? theme::bg2 : theme::bg1, theme::r3);
                Rect inner = box.inset(theme::s5, theme::s4);

                TextStyle peg;
                peg.size = theme::textXs;
                peg.color = theme::fg3;
                peg.tracking = theme::trackingWide;
                peg.uppercase = true;
                r.text(inner.x, inner.y, tr(slotName(uint8_t(slot))), peg);

                const Item* item = record.find(gear.worn[slot]);
                TextStyle name;
                name.size = theme::textBase;
                name.weight = FontWeight::Bold;
                name.color = item ? theme::fg1 : theme::fg4;
                r.text(inner.x, inner.y + 28.0f,
                    item ? tr(itemNoun(*item)) : tr("nothing on this peg"), name);

                if (item)
                    drawQuality(r, Rect { inner.right() - 150.0f, inner.y + 28.0f,
                                    150.0f, 30.0f },
                        item->quality);
            }
        }

        // The bag, filtered to the peg.
        void drawBag(App& app, Renderer& r)
        {
            float x = theme::edge + kColumn * 2.0f;
            float width = Renderer::DesignWidth - theme::edge - x;
            Rect head { x, kTop - 42.0f, width, 30.0f };

            TextStyle label;
            label.size = theme::textXs;
            label.color = theme::fg3;
            label.tracking = theme::trackingWide;
            label.uppercase = true;
            r.text(head, tr("in the bag"), label, Align::Left, VAlign::Top);

            if (m_fits.empty()) {
                TextStyle empty;
                empty.size = theme::textSm;
                empty.color = theme::fg4;
                r.text(Rect { x, kTop + 40.0f, width, 40.0f },
                    tr("Nothing here that would fit."), empty);
                return;
            }

            uint16_t worn = wornHere();
            int fits = 8;
            int first = std::max(0,
                std::min(m_pick - fits / 2, int(m_fits.size()) - fits));
            float y = kTop;
            for (int i = first; i < int(m_fits.size()) && i < first + fits; i++) {
                const Item& item = m_fits[size_t(i)];
                Rect row { x, y, width, kBagRow };
                app.touchZone(row, Zone_Bag, i);
                bool here = i == m_pick;
                ui::card(r, row, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                    here ? theme::bg2 : theme::bg1, theme::r2);
                Rect inner = row.inset(theme::s5, theme::s3);

                TextStyle name;
                name.size = theme::textBase;
                name.weight = FontWeight::Bold;
                name.color = theme::fg1;
                r.text(inner.x, inner.y, tr(itemNoun(item)), name);

                TextStyle note;
                note.size = theme::textXs;
                note.color = theme::fg3;
                r.text(inner.x, inner.y + 30.0f, summarise(item), note);

                drawQuality(r,
                    Rect { inner.right() - 160.0f, inner.y, 160.0f, 30.0f },
                    item.quality);

                if (item.id == worn) {
                    TextStyle on;
                    on.size = theme::textXs;
                    on.color = theme::accent;
                    on.tracking = theme::trackingWide;
                    on.uppercase = true;
                    r.text(Rect { inner.x, inner.y + 30.0f, inner.w, 26.0f },
                        tr("worn"), on, Align::Right, VAlign::Top);
                }
                y += kBagRow + theme::s2;
            }
        }

        // What it does, in the order the stat block shows them.
        static std::string summarise(const Item& item)
        {
            Sheet b = itemBonus(item);
            std::string out;
            const char* names[5] = { "HP", "MP", "ATK", "DEF", "SPD" };
            uint16_t values[5] = { b.hp, b.mp, b.atk, b.def, b.spd };
            for (int i = 0; i < 5; i++) {
                if (values[i] == 0)
                    continue;
                if (!out.empty())
                    out += "   ";
                out += format("+%u %s", unsigned(values[i]), names[i]);
            }
            return out;
        }

        // Six colours that mean the same thing in both palettes, the way the
        // dice are ivory in both: a rare that went grey in the light theme
        // would be a different rank, not a different shade.
        static void drawQuality(Renderer& r, const Rect& box, uint8_t quality)
        {
            static const Color kTiers[Quality_Count] = {
                Color::hex(0x9A938A), // common, the colour of nothing special
                Color::hex(0x6FAE5B), // uncommon
                Color::hex(0x4E8FD6), // rare
                Color::hex(0x9B6BC7), // epic
                Color::hex(0xD8A33A), // legendary
                Color::hex(0xD1574B), // godlike
            };
            uint8_t tier = quality < Quality_Count ? quality : Quality_Common;
            TextStyle text;
            text.size = theme::textXs;
            text.weight = FontWeight::Bold;
            text.color = kTiers[tier];
            text.tracking = theme::trackingWide;
            text.uppercase = true;
            r.text(box, tr(qualityName(tier)), text, Align::Right, VAlign::Top);
        }

        std::vector<GearPerson> m_party;
        std::vector<Item> m_fits;
        int m_who = 0;
        int m_peg = 0;
        int m_pick = 0;
        float m_pulse = 0.0f;
    };
}

std::unique_ptr<Scene> makeQuestGearScene(std::vector<GearPerson> party)
{
    return std::make_unique<GearScene>(std::move(party));
}

} // namespace nxp
