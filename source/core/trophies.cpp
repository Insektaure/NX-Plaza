#include "core/trophies.h"

#include "core/store.h"
#include "core/wallet.h"

namespace nxp {

namespace {

    // Append only.
    //
    // The ids are what profile.json remembers a date against, so renaming one
    // loses the date and reusing one puts the wrong date on the wrong trophy.
    // Order is free to change - nothing is keyed by position - but there is no
    // reason to, and a table that reads bronze to platinum is a ladder.
    const std::vector<Trophy> kTrophies = {
        // ------------------------------------------------------------ bronze
        { "first_crossing", "Somebody's out there",
            "Cross one other console.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.uniquePeople >= 1; } },
        { "pass_ready", "Properly introduced",
            "Give your pass a name, a greeting and something to carry.",
            Tier::Bronze, [](const TrophyFacts& f) { return f.passReady; } },
        { "fully_laden", "Fully laden",
            "Carry four things on your pass at once.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.carrying >= 4; } },
        { "first_piece", "A corner of something",
            "Take your first puzzle piece off a crossing.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.piecesHeld >= 1; } },
        { "first_star", "Worth keeping",
            "Star a card.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.starred >= 1; } },
        { "ten_people", "Ten faces",
            "Cross ten different consoles.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.uniquePeople >= 10; } },
        { "week_in", "A week of it",
            "Open the app on seven different days.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.daysCheckedIn >= 7; } },
        { "two_pictures", "Two down",
            "Finish two puzzles.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.puzzlesDone >= 2; } },
        { "first_purchase", "First purchase",
            "Buy something in the shop.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.coinsSpent >= 50; } },
        { "first_winnings", "Beginner's luck",
            "Win a bet in the games tab.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.coinsWon >= 3; } },
        { "dice_first_win", "First blood",
            "Win a duel at the dice.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.diceWins >= 1; } },
        // One duel in thirty-six, and the least consequential thing that can
        // happen in one: two sixes, a draw, and your stake handed back. Worth
        // marking for the coincidence rather than the outcome.
        { "dice_six_all", "Six all",
            "Roll a six against a six.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.diceSixAll; } },
        { "tower_ten", "Ten storeys",
            "Stack ten floors in the Mii tower.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.towerBest >= 10; } },
        { "tower_centred", "Dead centre",
            "Land a floor within six pixels of the one below.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.towerCentred; } },
        { "dash_hundred", "Off the mark",
            "Run a hundred metres in the plaza dash.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.dashBest >= 100; } },
        { "own_face", "Your own face",
            "Make a face of your own in the Mii maker.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.ownFace; } },
        { "redecorated", "Redecorated",
            "Give your card a look of your own.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.ownTheme; } },
        { "out_in_the_world", "Out in the world",
            "Have your pass reach somebody.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.passSent; } },
        { "matching_outfits", "Matching outfits",
            "Cross somebody whose card wears the same look as yours.",
            Tier::Bronze,
            [](const TrophyFacts& f) { return f.matchingTheme; } },
        { "same_taste", "Same taste",
            "Cross somebody carrying something you carry too.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.sharedCarry; } },
        { "strong_silent_type", "The strong silent type",
            "Cross a pass with nothing written on it.", Tier::Bronze,
            [](const TrophyFacts& f) { return f.silentPass; } },

        // ------------------------------------------------------------ silver
        { "busy_day", "A busy afternoon",
            "Cross ten consoles in one day.", Tier::Silver,
            [](const TrophyFacts& f) { return f.today >= 10; } },
        { "fifty_people", "Fifty faces",
            "Cross fifty different consoles.", Tier::Silver,
            [](const TrophyFacts& f) { return f.uniquePeople >= 50; } },
        { "hundred_people", "A hundred faces",
            "Cross a hundred different consoles.", Tier::Silver,
            [](const TrophyFacts& f) { return f.uniquePeople >= 100; } },
        { "hundred_crossings", "A hundred hellos",
            "Cross people a hundred times, the same ones included.",
            Tier::Silver,
            [](const TrophyFacts& f) { return f.totalCrossings >= 100; } },
        { "regular", "A regular",
            "Cross the same console ten times.", Tier::Silver,
            [](const TrophyFacts& f) { return f.mostSeen >= 10; } },
        { "around_town", "Around town",
            "Cross people in five different places.", Tier::Silver,
            [](const TrophyFacts& f) { return f.places >= 5; } },
        { "ten_places", "Ten places",
            "Cross people in ten different places.", Tier::Silver,
            [](const TrophyFacts& f) { return f.places >= 10; } },
        { "late_train", "The late train",
            "Cross somebody between midnight and four.", Tier::Silver,
            [](const TrophyFacts& f) { return f.lateCrossing; } },
        { "every_weekday", "Every day of the week",
            "Cross somebody on each of the seven weekdays.", Tier::Silver,
            [](const TrophyFacts& f) { return f.weekdays == 0x7F; } },
        { "fortnight_in", "A fortnight",
            "Open the app on fourteen different days.", Tier::Silver,
            [](const TrophyFacts& f) { return f.daysCheckedIn >= 14; } },
        { "old_friend", "An old friend",
            "Keep a card for six months.", Tier::Silver,
            [](const TrophyFacts& f) { return f.oldestFriendDays >= 180; } },
        { "one_picture", "A picture at last",
            "Finish a puzzle.", Tier::Silver,
            [](const TrophyFacts& f) { return f.puzzlesDone >= 1; } },
        { "halfway_pieces", "Halfway through",
            "Hold half of every piece there is.", Tier::Silver,
            [](const TrophyFacts& f) {
                return f.piecesTotal > 0 && f.piecesHeld * 2 >= f.piecesTotal;
            } },
        { "committee", "A picture by committee",
            "Take pieces from ten different people.", Tier::Silver,
            [](const TrophyFacts& f) { return f.pieceDonors >= 10; } },
        { "twentyfive_stars", "A shortlist",
            "Star twenty-five cards.", Tier::Silver,
            [](const TrophyFacts& f) { return f.starred >= 25; } },
        { "all_read", "Nothing left unread",
            "Open every card you hold, with fifty of them.", Tier::Silver,
            [](const TrophyFacts& f) {
                return f.uniquePeople >= 50 && f.unopened == 0;
            } },
        { "reciprocated", "Reciprocated",
            "Send something back to twenty-five people.", Tier::Silver,
            [](const TrophyFacts& f) { return f.tradedBack >= 25; } },
        { "namesake", "Namesake",
            "Cross somebody who chose the same name as you.", Tier::Silver,
            [](const TrophyFacts& f) { return f.namesake; } },
        { "big_hours", "Somebody with a life",
            "Cross a pass carrying a thousand hours in one title.", Tier::Silver,
            [](const TrophyFacts& f) { return f.maxHours >= 1000; } },
        { "best_customer", "The shop's best customer",
            "Spend five hundred coins.", Tier::Silver,
            [](const TrophyFacts& f) { return f.coinsSpent >= 500; } },
        { "saving_up", "Saving up",
            "Have five hundred coins at once.", Tier::Silver,
            [](const TrophyFacts& f) { return f.balance >= 500; } },
        // Five wins on the trot is a little over one per cent, so this is
        // eighty duels or so of asking.
        { "dice_run_five", "On a run",
            "Win five duels at the dice in a row.", Tier::Silver,
            [](const TrophyFacts& f) { return f.diceStreakBest >= 5; } },
        { "good_day_racing", "A good day at the tables",
            "Win fifty coins betting in the games tab.", Tier::Silver,
            [](const TrophyFacts& f) { return f.coinsWon >= 50; } },
        { "tower_twenty", "Twenty up",
            "Stack twenty floors in the Mii tower.", Tier::Silver,
            [](const TrophyFacts& f) { return f.towerBest >= 20; } },
        // The signed lean is what makes the tower a game rather than a timing
        // test, and this is the only thing that rewards noticing it.
        { "tower_recovered", "Talked it down",
            "Lean a tower to the edge of going over, and bring it back.",
            Tier::Silver,
            [](const TrophyFacts& f) { return f.towerRecovered; } },
        // Half a kilometre is a little over half a minute of running and about
        // twenty-three things jumped, at a pace that never stops climbing.
        { "dash_five_hundred", "A good run",
            "Run five hundred metres in the plaza dash.", Tier::Silver,
            [](const TrophyFacts& f) { return f.dashBest >= 500; } },

        // -------------------------------------------------------------- gold
        { "five_hundred_people", "Five hundred faces",
            "Cross five hundred different consoles.", Tier::Gold,
            [](const TrophyFacts& f) { return f.uniquePeople >= 500; } },
        { "thousand_people", "A thousand faces",
            "Cross a thousand different consoles.", Tier::Gold,
            [](const TrophyFacts& f) { return f.uniquePeople >= 1000; } },
        { "thousand_crossings", "A thousand hellos",
            "Cross people a thousand times, the same ones included.", Tier::Gold,
            [](const TrophyFacts& f) { return f.totalCrossings >= 1000; } },
        { "neighbours", "Practically neighbours",
            "Cross the same console fifty times.", Tier::Gold,
            [](const TrophyFacts& f) { return f.mostSeen >= 50; } },
        { "far_travelled", "Far travelled",
            "Cross people in twenty-five different places.", Tier::Gold,
            [](const TrophyFacts& f) { return f.places >= 25; } },
        { "fifty_places", "Fifty places",
            "Cross people in fifty different places.", Tier::Gold,
            [](const TrophyFacts& f) { return f.places >= 50; } },
        { "month_in", "A month of it",
            "Open the app on thirty different days.", Tier::Gold,
            [](const TrophyFacts& f) { return f.daysCheckedIn >= 30; } },
        { "hundred_days", "A hundred days",
            "Open the app on a hundred different days.", Tier::Gold,
            [](const TrophyFacts& f) { return f.daysCheckedIn >= 100; } },
        { "year_in", "A year of it",
            "Open the app on three hundred and sixty-five different days.",
            Tier::Gold,
            [](const TrophyFacts& f) { return f.daysCheckedIn >= 365; } },
        { "year_friend", "Known you for a year",
            "Keep a card for a year.", Tier::Gold,
            [](const TrophyFacts& f) { return f.oldestFriendDays >= 365; } },
        { "all_pictures", "Every picture",
            "Finish every puzzle.", Tier::Gold,
            [](const TrophyFacts& f) {
                return f.puzzleCount > 0 && f.puzzlesDone >= f.puzzleCount;
            } },
        { "by_hand", "By hand alone",
            "Finish a puzzle without buying a single piece.", Tier::Gold,
            [](const TrophyFacts& f) { return f.puzzleByHand; } },
        // Thirty floors is a catch window of about four frames, with the swing
        // near its cap. Forty would be frame-perfect.
        { "tower_thirty", "Thirty storeys",
            "Stack thirty floors in the Mii tower.", Tier::Gold,
            [](const TrophyFacts& f) { return f.towerBest >= 30; } },
        // A kilometre takes about fifty-five seconds, which is past the point
        // where the plaza stops getting quicker - so it is roughly forty
        // hazards, the last dozen of them at full speed.
        { "dash_kilometre", "A kilometre of plaza",
            "Run a thousand metres in the plaza dash.", Tier::Gold,
            [](const TrophyFacts& f) { return f.dashBest >= 1000; } },
        // Seven is three in a thousand: hundreds of duels, and the streak is
        // kept across visits so it can be built over weeks.
        { "dice_run_seven", "Seven in a row",
            "Win seven duels at the dice in a row.", Tier::Gold,
            [](const TrophyFacts& f) { return f.diceStreakBest >= 7; } },
        // Two hundred won means about two hundred and seventy raced, since the
        // odds are against the player - which is twenty-seven days of coins
        // spent at the track.
        { "bookmakers_problem", "The bookmaker's problem",
            "Win two hundred coins betting in the games tab.", Tier::Gold,
            [](const TrophyFacts& f) { return f.coinsWon >= 200; } },
        { "fifty_donors", "Fifty contributors",
            "Take pieces from fifty different people.", Tier::Gold,
            [](const TrophyFacts& f) { return f.pieceDonors >= 50; } },

        // ---------------------------------------------------------- platinum
        // Decided from the others, so it has no test of its own.
        { "the_whole_plaza", "The whole plaza",
            "Earn everything else.", Tier::Platinum, nullptr },
    };
}

const std::vector<Trophy>& trophies() { return kTrophies; }

TrophyFacts trophyFacts(const Store& store)
{
    TrophyFacts facts = store.trophyFacts();

    // Days rather than coins: ten a day is the only way the wallet grows, so
    // the total divided by the daily is the number of days this console has
    // been opened while the plaza could be reached. It is the one fact here
    // that does not come out of the store.
    facts.daysCheckedIn = Wallet::get().granted() / Wallet::kDaily;
    facts.coinsSpent = Wallet::get().spent();
    facts.balance = Wallet::get().balance();
    facts.coinsWon = Wallet::get().won();
    return facts;
}

std::vector<uint8_t> evaluateTrophies(const TrophyFacts& facts)
{
    std::vector<uint8_t> earned(kTrophies.size(), 0);
    bool allElse = true;
    for (size_t i = 0; i < kTrophies.size(); i++) {
        if (!kTrophies[i].test)
            continue;
        earned[i] = kTrophies[i].test(facts) ? 1 : 0;
        if (!earned[i])
            allElse = false;
    }
    for (size_t i = 0; i < kTrophies.size(); i++) {
        if (!kTrophies[i].test)
            earned[i] = allElse ? 1 : 0;
    }
    return earned;
}

std::vector<uint8_t> trophyState(const Store& store, const TrophyFacts& facts)
{
    std::vector<uint8_t> state = evaluateTrophies(facts);
    for (size_t i = 0; i < kTrophies.size() && i < state.size(); i++) {
        if (!state[i] && store.trophyDate(kTrophies[i].id) != 0)
            state[i] = 1;
    }
    return state;
}

const char* tierName(Tier tier)
{
    switch (tier) {
    case Tier::Bronze:
        return "bronze";
    case Tier::Silver:
        return "silver";
    case Tier::Gold:
        return "gold";
    case Tier::Platinum:
    default:
        return "platinum";
    }
}

} // namespace nxp
