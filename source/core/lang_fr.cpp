#include "core/i18n.h"

// French.
//
// Left column: the English exactly as it is written in the code, which is the
// key - a reworded line in a scene silently stops matching, so
// `tools/i18n_scan.py` reports the rows whose English has gone as well as the
// strings no language has yet.
//
// A few decisions this file makes, so the next language can make them the same
// way or on purpose differently:
//
//   - "the plaza" is the service, "the Square" is the screen full of Miis, and
//     French has one word for both. So the plaza stays *le plaza*, which is
//     the app's own name anyway, and la place is the Square.
//   - A pass is *une carte*: it is the thing that is collected, starred and
//     sent, and carte is what a French player would call it.
//   - Coins are *des pièces*, and a puzzle piece is always *une pièce de
//     puzzle* - never shortened, because on the lantern wheel both are on the
//     screen at once.
//   - Game names are translated but keep their shape: la course de Mii, la
//     tour de Mii, le duel de dés, la roue aux lanternes, le bandit. Plaza
//     Dash keeps its name.
//   - The hint verbs at the foot of the screen are infinitives - ouvrir,
//     lancer, tourner - which is how a French interface gives an instruction.
//
// A word that is the same in both is still listed, at the bottom, with itself
// as its translation: tr() drops those rows - falling back to the English is
// the same answer - but listing them is how the scanner tells "the same in
// French" from "nobody has looked at this yet".

namespace nxp {
namespace {

    const Translation kEntries[] = {
        // ------------------------------------------------------------- tabs
        { "Nearby", "À proximité" },
        { "The Square", "La place" },
        { "Games", "Jeux" },
        { "The shop", "La boutique" },
        { "Trophies", "Trophées" },
        { "Your pass", "Votre carte" },
        { "Settings", "Paramètres" },

        // ------------------------------------------------- the hint strip
        { "again", "encore" },
        { "back", "retour" },
        { "block", "bloquer" },
        { "cancel", "annuler" },
        { "change", "changer" },
        { "choose", "choisir" },
        { "close", "fermer" },
        { "done", "terminé" },
        { "fill the screen", "plein écran" },
        { "give up", "abandonner" },
        { "jump - hold to go higher", "sauter - maintenir pour plus haut" },
        { "jump ten", "avancer de dix" },
        { "left and right to change", "gauche et droite pour changer" },
        { "look now", "chercher maintenant" },
        { "mark read", "marquer comme lu" },
        { "next pass", "carte suivante" },
        { "open", "ouvrir" },
        { "open section", "ouvrir la section" },
        { "play", "jouer" },
        { "roll", "lancer" },
        { "roll again", "relancer" },
        { "run", "courir" },
        { "run again", "courir encore" },
        { "save / load", "sauver / charger" },
        { "save face", "enregistrer le visage" },
        { "shuffle", "au hasard" },
        { "shuffle face", "visage au hasard" },
        { "spin", "tourner" },
        { "start", "commencer" },

        // --------------------------------------------------------- buttons
        { "Make my pass", "Créer ma carte" },
        { "Not now", "Plus tard" },
        { "Run", "Courir" },
        { "Run again", "Courir encore" },
        { "Start stacking", "Commencer à empiler" },
        { "Stack again", "Empiler encore" },

        // ------------------------------------------- eyebrows and captions
        { "games", "jeux" },
        { "trophies", "trophées" },
        { "the shop", "la boutique" },
        { "the square", "la place" },
        { "the mii race", "la course de mii" },
        { "the mii tower", "la tour de mii" },
        { "the dice duel", "le duel de dés" },
        { "what it pays", "ce que ça paie" },
        { "your prediction", "votre pronostic" },
        { "your pairing code", "votre code d'appairage" },
        { "people met", "personnes croisées" },
        { "times your pass was sent", "fois que votre carte a été envoyée" },
        { "floors", "étages" },
        { "best %u %s", "record %u %s" },

        // ------------------------------------------------------ the games
        { "Something to do", "De quoi s'occuper" },
        { "Played with the faces and names in your collection, so the more people "
          "you have crossed, the more there is here.",
            "Jouer avec les visages et les noms de votre collection : plus vous "
            "croisez de monde, plus il y a à faire ici." },

        { "The Mii race", "La course de Mii" },
        { "Three of the people you have crossed, against your own Mii, and nobody "
          "is faster than anybody.\n"
          "Watch for nothing, bet a coin for three back, or call first and second "
          "for eleven.",
            "Trois personnes que vous avez croisées contre votre propre Mii, et "
            "personne ne court plus vite que personne.\n"
            "Regardez pour rien, pariez une pièce pour trois en retour, ou "
            "annoncez les deux premiers pour onze." },

        { "The dice duel", "Le duel de dés" },
        { "One roll each against somebody you have crossed, and the highest takes "
          "it.\n"
          "Roll for nothing, or bet two coins for three back - and a draw hands "
          "your two back.",
            "Un lancer chacun contre quelqu'un que vous avez croisé, et le plus "
            "haut l'emporte.\n"
            "Lancez pour rien, ou pariez deux pièces pour trois en retour - une "
            "égalité vous rend les vôtres." },

        { "The lantern wheel", "La roue aux lanternes" },
        { "Twelve lanterns and a needle, and two of them are a puzzle piece.\n"
          "Watch it for nothing, or ten coins a spin - every lantern pays "
          "something, and one in six is a piece.",
            "Douze lanternes et une aiguille, et deux d'entre elles sont une pièce "
            "de puzzle.\n"
            "Regardez pour rien, ou dix pièces le tour - chaque lanterne paie "
            "quelque chose, et une sur six est une pièce de puzzle." },

        { "The bandit", "Le bandit" },
        { "Three reels, and two machines: three symbols that pay small and often, "
          "or five that pay rarely and big.\n"
          "A coin a spin, or spin for nothing - two bells hand the coin back, and "
          "the board on the wall lists every line it pays.",
            "Trois rouleaux et deux machines : trois symboles qui paient peu et "
            "souvent, ou cinq qui paient rarement et gros.\n"
            "Une pièce le tour, ou tournez pour rien - deux cloches rendent la "
            "pièce, et le tableau au mur liste chaque combinaison." },

        { "The Mii tower", "La tour de Mii" },
        { "Stack the people you have crossed, one drop at a time.\n"
          "Miss the shoulders below and they fall; drift too far from the base and "
          "the lot goes over.",
            "Empilez les gens que vous avez croisés, un lâcher à la fois.\n"
            "Ratez les épaules du dessous et ils tombent ; éloignez-vous trop de "
            "la base et tout s'écroule." },

        { "Plaza dash", "Plaza Dash" },
        { "Your own Mii running through the plaza, jumping what the market leaves "
          "in the way.\n"
          "It gets quicker. Nothing staked, nothing won - only how far you got.",
            "Votre Mii traverse le plaza en sautant ce que le marché laisse en "
            "travers.\n"
            "Ça accélère. Rien à parier, rien à gagner - seulement la distance." },

        // ------------------------------------------------ settings: sections
        { "%s never sends your name or an exact position. You choose how much of a "
          "pass leaves the console.",
            "%s n'envoie jamais votre nom ni votre position exacte. Vous "
            "choisissez ce qui, de votre carte, quitte la console." },
        { "How passes find their way to you.",
            "Comment les cartes trouvent leur chemin jusqu'à vous." },
        { "Nothing interrupts a game. The plaza waits.",
            "Rien n'interrompt une partie. Le plaza attend." },
        { "The app opens in daylight. Dark is for a dim room, or match whatever "
          "the console is set to.",
            "L'app s'ouvre en plein jour. Le thème sombre est pour une pièce peu "
            "éclairée, ou suivez le réglage de la console." },
        { "The app is written in English and says what it has been taught to say "
          "in anything else. Whatever a language is missing stays in English.",
            "L'app est écrite en anglais et dit, dans les autres langues, ce qu'on "
            "lui a appris à dire. Ce qui manque à une langue reste en anglais." },
        { "Where your passes go, and how another console recognises this one.",
            "Où vont vos cartes, et comment une autre console reconnaît celle-ci." },
        { "Everything the app keeps lives on the SD card, and all of it can go.",
            "Tout ce que l'app garde est sur la carte SD, et tout peut disparaître." },
        { "What this build of %s is, who made it, and where it came from.",
            "Ce qu'est cette version de %s, qui l'a faite et d'où elle vient." },

        // ---------------------------------------------------- settings: rows
        { "English - what the app is written in",
            "Anglais - la langue dans laquelle l'app est écrite" },
        { "Share where we crossed", "Partager où l'on s'est croisés" },
        { "District only - never a street or a venue you did not name",
            "Le quartier seulement - jamais une rue ni un lieu que vous n'avez pas "
            "nommé" },
        { "District label", "Nom du quartier" },
        { "e.g. Namba Station", "ex. Namba Station" },
        { "City label", "Nom de la ville" },
        { "used when sharing is set to City",
            "utilisé quand le partage est réglé sur Ville" },
        { "Show what I am playing", "Montrer ce que je joue" },
        { "The title, your hours in it, and how many games you have. Hidden titles "
          "stay hidden, always",
            "Le jeu, vos heures dessus et combien de jeux vous avez. Les jeux "
            "masqués restent masqués, toujours" },
        { "Exchange passes automatically", "Échanger les cartes automatiquement" },
        { "Trades happen while the app is open",
            "Les échanges se font tant que l'app est ouverte" },
        { "How far a crossing reaches", "Portée d'un croisement" },
        { "Same network is the closest thing to walking past someone",
            "Le même réseau est ce qui ressemble le plus à croiser quelqu'un dans "
            "la rue" },
        { "Crossings per day", "Croisements par jour" },
        { "Tell me when passes arrive", "Me prévenir quand des cartes arrivent" },
        { "A card in the corner, never an interruption",
            "Une carte dans un coin, jamais une interruption" },
        { "Theme", "Thème" },
        { "Hold the scenery still", "Garder le décor immobile" },
        { "On, for anybody the Plaza Dash minigame makes queasy: the background "
          "stops moving and only what you have to dodge does. Turn it off for the "
          "full parallax",
            "Activé, pour qui a la nausée avec le mini-jeu Plaza Dash : le décor "
            "s'arrête et seul ce qu'il faut éviter bouge. Désactivez-le pour la "
            "parallaxe complète" },
        { "Plaza server", "Serveur du plaza" },
        { "Wi-Fi match token", "Jeton de correspondance Wi-Fi" },
        { "Check in now", "Se connecter maintenant" },
        { "Blocked consoles", "Consoles bloquées" },
        { "Clear the whole list", "Vider toute la liste" },
        { "unblock", "débloquer" },
        { "none", "aucun" },
        { "Write a log file", "Écrire un fichier journal" },
        { "Copy everything to a backup folder",
            "Copier tout dans un dossier de sauvegarde" },
        { "Your identity, your pass and your collection, into backup/ on this card",
            "Votre identité, votre carte et votre collection, dans backup/ sur "
            "cette carte" },
        { "Download puzzle art", "Télécharger les images des puzzles" },
        { "Delete every pass you have collected",
            "Supprimer toutes les cartes que vous avez collectées" },
        { "This cannot be undone", "C'est sans retour" },
        { "Start over as someone new", "Repartir de zéro sous une autre identité" },
        { "New id, new pass; everything you handed out goes unlinkable",
            "Nouvel identifiant, nouvelle carte ; tout ce que vous avez distribué "
            "devient impossible à relier" },
        { "Check for updates", "Chercher une mise à jour" },
        { "Check on every launch", "Vérifier à chaque lancement" },
        { "One request when the app opens; it never installs on its own",
            "Une requête à l'ouverture de l'app ; rien ne s'installe tout seul" },
        { "Developed by Insektaure", "Développé par Insektaure" },
        { "Find us on GitHub !", "Retrouvez-nous sur GitHub !" },
        { "This console's id", "Identifiant de cette console" },

        // -------------------------------------------------- settings: toasts
        { "Not connected to the plaza", "Pas connecté au plaza" },
        { "This one needs the server. The dot by the tabs turns green when it can "
          "be reached.",
            "Celle-ci a besoin du serveur. Le point près des onglets passe au vert "
            "quand il répond." },
        { "Busy with another download", "Un autre téléchargement est en cours" },
        { "Wait for the update in About to finish, then try again.",
            "Attendez la fin de la mise à jour dans À propos, puis réessayez." },
        { "Downloading puzzle art", "Téléchargement des images" },
        { "This row shows how it is going.", "Cette ligne montre où ça en est." },
        { "Could not back up", "Sauvegarde impossible" },
        { "Backed up to %s", "Sauvegardé dans %s" },
        { "It is on the same card. Copy the backup folder to a computer to be safe "
          "from losing the card itself.",
            "C'est sur la même carte. Copiez le dossier de sauvegarde sur un "
            "ordinateur pour être à l'abri de perdre la carte elle-même." },
        { "Checking in", "Connexion en cours" },
        { "Unblocked %s", "%s débloqué" },
        { "They can cross you again, unless they blocked you as well.",
            "Ils peuvent vous croiser à nouveau, sauf s'ils vous ont bloqué "
            "aussi." },

        // ---------------------------------------------------- the Mii race
        { "Four runners, one line", "Quatre coureurs, une ligne" },
        { "Nobody is faster than anybody. Nothing you do changes that.",
            "Personne ne court plus vite que personne, et rien de ce que vous "
            "faites n'y change quoi que ce soit." },
        { "Race for free", "Courir pour rien" },
        { "Bet 1 coin - your Mii to win", "Parier 1 pièce - votre Mii gagnant" },
        { "Predict 1st and 2nd", "Annoncer les deux premiers" },
        { "Just to watch.", "Juste pour regarder." },
        { "%u back if it comes in.", "%u en retour si ça passe." },
        { "For free, or bet 1 coin for %u back.",
            "Pour rien, ou 1 pièce pour %u en retour." },
        { "Nothing to bet with.", "Rien à parier." },
        { "Nothing to bet", "Rien à parier" },
        { "Ten coins arrive on each new day you open the app.",
            "Dix pièces arrivent chaque nouveau jour où vous ouvrez l'app." },
        { "Who comes first?", "Qui arrive premier ?" },
        { "And who comes second?", "Et qui arrive deuxième ?" },
        { "That is your call", "C'est votre pronostic" },
        { "First and second, in order. Eleven for a coin if you call it, or watch "
          "for nothing.",
            "Les deux premiers, dans l'ordre. Onze pour une pièce si vous trouvez, "
            "ou regardez pour rien." },
        { "%s to win. Now who is behind them?",
            "%s gagnant. Et qui juste derrière ?" },
        { "%s first, %s second.", "%s premier, %s deuxième." },
        { "race", "courir" },
        { "Go", "Partez !" },
        { "You are in front", "Vous êtes en tête" },
        { "%s is in front", "%s est en tête" },
        { "predict again", "pronostiquer encore" },
        { "race again", "courir encore" },
        { "Called it", "Bien vu" },
        { "You won", "Vous gagnez" },
        { "%s won", "%s gagne" },
        { "Nobody", "Personne" },
        { "You said %s then %s - they came %s and %s.",
            "Vous avez dit %s puis %s - ils sont arrivés %s et %s." },
        { "The bet paid %u - %u coins to spend.",
            "Le pari rapporte %u - %u pièces à dépenser." },
        { "The bet cost you a coin. %u left.",
            "Le pari vous a coûté une pièce. Il en reste %u." },
        { "Nothing bet, but right is right.",
            "Rien de parié, mais vu juste quand même." },
        { "Nothing bet, nothing lost.", "Rien de parié, rien de perdu." },
        { "Predict again", "Pronostiquer encore" },
        { "Race again", "Courir encore" },
        { "Bet %u coin - %u back", "Parier %u pièce - %u en retour" },
        { "1st", "1er" },
        { "2nd", "2e" },
        { "3rd", "3e" },
        { "4th", "4e" },

        // ---------------------------------------------------- the dice duel
        { "One roll each", "Un lancer chacun" },
        { "Highest roll takes it. A draw hands your %u back.",
            "Le plus haut lancer l'emporte. Une égalité vous rend vos %u." },
        { "Roll for free", "Lancer pour rien" },
        { "%u coins to play", "%u pièces pour jouer" },
        { "Ten arrive on each new day you open the app.",
            "Dix arrivent chaque nouveau jour où vous ouvrez l'app." },
        { "%d all - nobody takes it", "%d partout - personne ne l'emporte" },
        { "%d beats %d", "%d bat %d" },
        { "%d loses to %d", "%d perd contre %d" },
        { "Your %u came back. %u to spend.",
            "Vos %u vous reviennent. %u à dépenser." },
        { "The bet cost you %u. %u left.",
            "Le pari vous a coûté %u. Il en reste %u." },
        { "Nothing bet, and nothing decided.", "Rien de parié, et rien de décidé." },
        { "Nothing bet, but a win is a win.",
            "Rien de parié, mais une victoire est une victoire." },
        { "%u in a row", "%u d'affilée" },
        { "Roll again", "Relancer" },
        { "Bet %u coins - %u back", "Parier %u pièces - %u en retour" },

        // ------------------------------------------------- the lantern wheel
        { "%u coins for a spin", "%u pièces pour un tour" },
        { "Watch it for nothing, or put %u coins on it. Every lantern pays "
          "something and %d of the %d are a puzzle piece. You have %u.",
            "Regardez pour rien, ou misez %u pièces. Chaque lanterne paie quelque "
            "chose et %d des %d sont une pièce de puzzle. Vous en avez %u." },
        { "Spin for nothing", "Tourner pour rien" },
        { "A piece of %s", "Une pièce de %s" },
        { "The piece lantern", "La lanterne du puzzle" },
        { "The %u lantern", "La lanterne à %u" },
        { "Piece %d, and the panel will say the wheel brought it. %u coins left.",
            "Pièce %d, et le panneau dira que la roue l'a apportée. Il reste %u "
            "pièces." },
        { "%u coins to spend. A spin costs %u.",
            "%u pièces à dépenser. Un tour coûte %u." },
        { "Nothing was on it - that one only pays when you have staked.",
            "Rien n'était misé - celle-là ne paie que si vous avez misé." },
        { "Nothing staked, so nothing won. It would have paid %u.",
            "Rien de misé, donc rien de gagné. Elle aurait payé %u." },
        { "Spin again", "Tourner encore" },
        { "Bet %u coins", "Miser %u pièces" },

        // ---------------------------------------------------- the bandit
        { "A coin a spin", "Une pièce le tour" },
        { "A coin a spin, and %u to spend. X changes machines.",
            "Une pièce le tour, et %u à dépenser. X change de machine." },
        { "THREE SYMBOLS", "TROIS SYMBOLES" },
        { "FIVE SYMBOLS", "CINQ SYMBOLES" },
        { "three symbols", "trois symboles" },
        { "five symbols", "cinq symboles" },
        { "Spin for free", "Tourner pour rien" },
        { "Bet %u coin", "Miser %u pièce" },
        { "Two sevens", "Deux 7" },
        { "Two bells", "Deux cloches" },
        { "Two the same", "Deux identiques" },
        { "Any two the same", "Deux identiques" },
        { "Nothing", "Rien" },
        { "Three %s", "Trois %s" },
        { "suns", "soleils" },
        { "moons", "lunes" },
        { "bells", "cloches" },
        { "sevens", "7" },
        { "bars", "barres" },
        { "a line about every 3 spins", "une combinaison tous les 3 tours environ" },
        { "a line about every 4 spins", "une combinaison tous les 4 tours environ" },
        { "%u back, and %u to spend.", "%u en retour, et %u à dépenser." },
        { "That coin is gone. %u left.",
            "Cette pièce est perdue. Il en reste %u." },
        { "Nothing staked, so nothing won - it would have paid %u.",
            "Rien de misé, donc rien de gagné - ça aurait payé %u." },
        { "Nothing staked, and nothing to stake it on.",
            "Rien de misé, et rien à miser." },

        // ---------------------------------------------------- the Mii tower
        { "Stack them up", "Empilez-les" },
        { "A drops whoever is swinging. Miss the shoulders below and they fall; "
          "drift too far from the base and the lot goes over.",
            "A lâche celui qui se balance. Ratez les épaules du dessous et il "
            "tombe ; écartez-vous trop de la base et tout s'écroule." },
        { "drop", "lâcher" },
        { "lean", "inclinaison" },
        { "%d floors", "%d étages" },
        { "best %u floors", "record %u étages" },
        { "floors, best %u", "étages, record %u" },
        { "%s had nothing to stand on.", "%s n'avait rien sous les pieds." },
        { "It went over with %s on top.",
            "Tout est parti avec %s tout en haut." },

        // ---------------------------------------------------- plaza dash
        { "Jump the market", "Sautez le marché" },
        { "A to jump, and hold it to jump higher. It gets quicker until you hit "
          "something.",
            "A pour sauter, maintenez pour sauter plus haut. Ça accélère jusqu'à "
            "ce que vous tapiez dans quelque chose." },
        { "%u m", "%u m" },
        { "best %u m", "record %u m" },
        { "Best is %u m.", "Le record est de %u m." },
        { "A new best.", "Un nouveau record." },
        { "a new best", "un nouveau record" },

        // ------------------------------------------- shared by the games
        { "You", "Vous" },
        { "A stranger", "Un inconnu" },

        // ---------------------------------------------------- the trophies
        { "show %s", "voir %s" },
        { "earned only", "obtenus seulement" },
        { "still to earn", "restant à obtenir" },
        { "all of them", "tous" },
        { "bronze", "bronze" },
        { "silver", "argent" },
        { "gold", "or" },
        { "platinum", "platine" },
        { "%d of %zu", "%d sur %zu" },
        { "earned", "obtenu" },
        { "earned %s", "obtenu %s" },
        { "Nothing earned yet. Go and meet somebody.",
            "Rien d'obtenu pour l'instant. Allez croiser quelqu'un." },
        { "Everything here is earned.", "Tout ici est obtenu." },
        { "A %s trophy.", "Un trophée %s." },
        { "%d trophies earned", "%d trophées obtenus" },
        { "Your record is in the trophies tab.",
            "Votre palmarès est dans l'onglet des trophées." },

        // The seventy-two, in the order they are listed in trophies.cpp: the
        // name, then what earns it.
        { "Somebody's out there", "Il y a quelqu'un" },
        { "Cross one other console.", "Croisez une autre console." },
        { "Properly introduced", "Présentations faites" },
        { "Give your pass a name, a greeting and something to carry.",
            "Donnez à votre carte un nom, un message et quelque chose à "
            "transporter." },
        { "Fully laden", "Chargé à bloc" },
        { "Carry four things on your pass at once.",
            "Transportez quatre choses à la fois sur votre carte." },
        { "A corner of something", "Un coin de quelque chose" },
        { "Take your first puzzle piece off a crossing.",
            "Récupérez votre première pièce de puzzle sur un croisement." },
        { "Worth keeping", "À garder" },
        { "Star a card.", "Mettez une étoile à une carte." },
        { "Ten faces", "Dix visages" },
        { "Cross ten different consoles.", "Croisez dix consoles différentes." },
        { "A week of it", "Une semaine" },
        { "Open the app on seven different days.",
            "Ouvrez l'app sept jours différents." },
        { "Two down", "Deux de faits" },
        { "Finish two puzzles.", "Terminez deux puzzles." },
        { "First purchase", "Premier achat" },
        { "Buy something in the shop.", "Achetez quelque chose dans la boutique." },
        { "Beginner's luck", "Chance du débutant" },
        { "Win a bet in the games tab.", "Gagnez un pari dans l'onglet des jeux." },
        { "First blood", "Premier sang" },
        { "Win a duel at the dice.", "Gagnez un duel aux dés." },
        { "Six all", "Six partout" },
        { "Roll a six against a six.", "Faites un six contre un six." },
        { "Lined up", "Alignés" },
        { "Line up three of a kind at the bandit.",
            "Alignez trois symboles identiques au bandit." },
        { "The wheel's own", "Cadeau de la roue" },
        { "Win a puzzle piece on the lantern wheel.",
            "Gagnez une pièce de puzzle à la roue aux lanternes." },
        { "Ten storeys", "Dix étages" },
        { "Stack ten floors in the Mii tower.",
            "Empilez dix étages dans la tour de Mii." },
        { "Dead centre", "En plein centre" },
        { "Land a floor within six pixels of the one below.",
            "Posez un étage à moins de six pixels de celui du dessous." },
        { "Off the mark", "Lancé" },
        { "Run a hundred metres in the plaza dash.",
            "Courez cent mètres dans Plaza Dash." },
        { "Your own face", "Votre propre visage" },
        { "Make a face of your own in the Mii maker.",
            "Créez un visage à vous dans l'éditeur de Mii." },
        { "Redecorated", "Redécoré" },
        { "Give your card a look of your own.",
            "Donnez à votre carte un look qui n'appartient qu'à vous." },
        { "Out in the world", "Dans la nature" },
        { "Have your pass reach somebody.",
            "Faites parvenir votre carte à quelqu'un." },
        { "Matching outfits", "Assortis" },
        { "Cross somebody whose card wears the same look as yours.",
            "Croisez quelqu'un dont la carte porte le même look que la vôtre." },
        { "Same taste", "Mêmes goûts" },
        { "Cross somebody carrying something you carry too.",
            "Croisez quelqu'un qui transporte quelque chose que vous transportez "
            "aussi." },
        { "The strong silent type", "Le taciturne" },
        { "Cross a pass with nothing written on it.",
            "Croisez une carte sans rien d'écrit dessus." },
        { "A busy afternoon", "Un après-midi chargé" },
        { "Cross ten consoles in one day.",
            "Croisez dix consoles en une seule journée." },
        { "Fifty faces", "Cinquante visages" },
        { "Cross fifty different consoles.",
            "Croisez cinquante consoles différentes." },
        { "A hundred faces", "Cent visages" },
        { "Cross a hundred different consoles.",
            "Croisez cent consoles différentes." },
        { "A hundred hellos", "Cent bonjours" },
        { "Cross people a hundred times, the same ones included.",
            "Croisez du monde cent fois, les mêmes personnes comprises." },
        { "A regular", "Un habitué" },
        { "Cross the same console ten times.",
            "Croisez la même console dix fois." },
        { "Around town", "Dans le quartier" },
        { "Cross people in five different places.",
            "Croisez du monde dans cinq lieux différents." },
        { "Ten places", "Dix lieux" },
        { "Cross people in ten different places.",
            "Croisez du monde dans dix lieux différents." },
        { "The late train", "Le dernier train" },
        { "Cross somebody between midnight and four.",
            "Croisez quelqu'un entre minuit et quatre heures." },
        { "Every day of the week", "Tous les jours de la semaine" },
        { "Cross somebody on each of the seven weekdays.",
            "Croisez quelqu'un chacun des sept jours de la semaine." },
        { "A fortnight", "Quinze jours" },
        { "Open the app on fourteen different days.",
            "Ouvrez l'app quatorze jours différents." },
        { "An old friend", "Une vieille connaissance" },
        { "Keep a card for six months.", "Gardez une carte pendant six mois." },
        { "A picture at last", "Une image, enfin" },
        { "Finish a puzzle.", "Terminez un puzzle." },
        { "Halfway through", "À mi-chemin" },
        { "Hold half of every piece there is.",
            "Détenez la moitié de toutes les pièces qui existent." },
        { "A picture by committee", "Une image à plusieurs mains" },
        { "Take pieces from ten different people.",
            "Prenez des pièces à dix personnes différentes." },
        { "A shortlist", "Une sélection" },
        { "Star twenty-five cards.", "Mettez une étoile à vingt-cinq cartes." },
        { "Nothing left unread", "Rien de non lu" },
        { "Open every card you hold, with fifty of them.",
            "Ouvrez toutes les cartes que vous détenez, avec cinquante d'entre "
            "elles." },
        { "Reciprocated", "Renvoyé" },
        { "Send something back to twenty-five people.",
            "Renvoyez quelque chose à vingt-cinq personnes." },
        { "Namesake", "Homonyme" },
        { "Cross somebody who chose the same name as you.",
            "Croisez quelqu'un qui a choisi le même nom que vous." },
        { "Somebody with a life", "Quelqu'un qui a une vie" },
        { "Cross a pass carrying a thousand hours in one title.",
            "Croisez une carte affichant mille heures sur un seul jeu." },
        { "The shop's best customer", "Meilleur client de la boutique" },
        { "Spend five hundred coins.", "Dépensez cinq cents pièces." },
        { "Saving up", "Bas de laine" },
        { "Have five hundred coins at once.",
            "Ayez cinq cents pièces d'un seul coup." },
        { "On a run", "En série" },
        { "Win five duels at the dice in a row.",
            "Gagnez cinq duels aux dés d'affilée." },
        { "A good day at the tables", "Une bonne journée aux tables" },
        { "Win fifty coins betting in the games tab.",
            "Gagnez cinquante pièces en pariant dans l'onglet des jeux." },
        { "Sevens", "Trois 7" },
        { "Line up three sevens at the bandit.", "Alignez trois 7 au bandit." },
        { "Twice lucky", "Deux fois chanceux" },
        { "Win two puzzle pieces on the lantern wheel.",
            "Gagnez deux pièces de puzzle à la roue aux lanternes." },
        { "Twenty up", "Vingt étages" },
        { "Stack twenty floors in the Mii tower.",
            "Empilez vingt étages dans la tour de Mii." },
        { "Steady hands", "Main sûre" },
        { "Land five floors in a row dead centre.",
            "Posez cinq étages d'affilée en plein centre." },
        { "Talked it down", "Rattrapée de justesse" },
        { "Lean a tower to the edge of going over, and bring it back.",
            "Penchez une tour au bord de la chute, et rattrapez-la." },
        { "A good run", "Une belle course" },
        { "Run five hundred metres in the plaza dash.",
            "Courez cinq cents mètres dans Plaza Dash." },
        { "Five hundred faces", "Cinq cents visages" },
        { "Cross five hundred different consoles.",
            "Croisez cinq cents consoles différentes." },
        { "A thousand faces", "Mille visages" },
        { "Cross a thousand different consoles.",
            "Croisez mille consoles différentes." },
        { "A thousand hellos", "Mille bonjours" },
        { "Cross people a thousand times, the same ones included.",
            "Croisez du monde mille fois, les mêmes personnes comprises." },
        { "Practically neighbours", "Presque voisins" },
        { "Cross the same console fifty times.",
            "Croisez la même console cinquante fois." },
        { "Far travelled", "Grand voyageur" },
        { "Cross people in twenty-five different places.",
            "Croisez du monde dans vingt-cinq lieux différents." },
        { "Fifty places", "Cinquante lieux" },
        { "Cross people in fifty different places.",
            "Croisez du monde dans cinquante lieux différents." },
        { "A month of it", "Un mois" },
        { "Open the app on thirty different days.",
            "Ouvrez l'app trente jours différents." },
        { "A hundred days", "Cent jours" },
        { "Open the app on a hundred different days.",
            "Ouvrez l'app cent jours différents." },
        { "A year of it", "Une année" },
        { "Open the app on three hundred and sixty-five different days.",
            "Ouvrez l'app trois cent soixante-cinq jours différents." },
        { "Known you for a year", "Un an de connaissance" },
        { "Keep a card for a year.", "Gardez une carte pendant un an." },
        { "Every picture", "Toutes les images" },
        { "Finish every puzzle.", "Terminez tous les puzzles." },
        { "By hand alone", "Sans rien acheter" },
        { "Finish a puzzle without buying a single piece.",
            "Terminez un puzzle sans acheter une seule pièce." },
        { "The full set", "La collection complète" },
        { "Line up three of a kind of every symbol the bandit has.",
            "Alignez trois fois chaque symbole du bandit." },
        { "Five from the wheel", "Cinq à la roue" },
        { "Win five puzzle pieces on the lantern wheel.",
            "Gagnez cinq pièces de puzzle à la roue aux lanternes." },
        { "Thirty storeys", "Trente étages" },
        { "Stack thirty floors in the Mii tower.",
            "Empilez trente étages dans la tour de Mii." },
        { "A kilometre of plaza", "Un kilomètre de plaza" },
        { "Run a thousand metres in the plaza dash.",
            "Courez mille mètres dans Plaza Dash." },
        { "Seven in a row", "Sept d'affilée" },
        { "Win seven duels at the dice in a row.",
            "Gagnez sept duels aux dés d'affilée." },
        { "The bookmaker's problem", "Le cauchemar du bookmaker" },
        { "Win two hundred coins betting in the games tab.",
            "Gagnez deux cents pièces en pariant dans l'onglet des jeux." },
        { "Fifty contributors", "Cinquante contributeurs" },
        { "Take pieces from fifty different people.",
            "Prenez des pièces à cinquante personnes différentes." },
        { "The whole plaza", "Tout le plaza" },
        { "Earn everything else.", "Obtenez tout le reste." },

        // ------------------------------------------- settings, the rest of it
        { "following the console, currently %s",
            "suit la console, actuellement %s" },
        { "light", "clair" },
        { "dark", "sombre" },
        { "light by default", "clair par défaut" },
        { "http://host:port of your own server",
            "http://hôte:port de votre propre serveur" },
        { "where this build trades passes",
            "où cette version échange les cartes" },
        { "no Wi-Fi name to match on - wired, or not connected",
            "aucun nom de Wi-Fi sur lequel s'accorder - filaire, ou non connecté" },
        { "consoles on \"%s\" share this",
            "les consoles sur \"%s\" partagent ceci" },
        { "Nobody is blocked here. A asks the plaza to drop any it still has",
            "Personne n'est bloqué ici. A demande au plaza d'oublier ceux qu'il "
            "garde encore" },
        { "Nobody is blocked here. Needs the plaza, and this console is offline",
            "Personne n'est bloqué ici. Nécessite le plaza, et cette console est "
            "hors ligne" },
        { " - blocked %s", " - bloqué %s" },
        { ". A lets them cross you again",
            ". A les autorise à vous croiser de nouveau" },
        { ". Offline - unblocking needs the plaza",
            ". Hors ligne - débloquer nécessite le plaza" },
        { "Clears all %zu at once, on this console and on the plaza",
            "Débloque les %zu d'un coup, sur cette console et sur le plaza" },
        { "Offline - clearing the list needs the plaza, or the two would disagree",
            "Hors ligne - vider la liste nécessite le plaza, sinon les deux ne "
            "seraient plus d'accord" },
        { "Off. Turn it on before reporting a problem, then off again",
            "Désactivé. Activez-le avant de signaler un problème, puis "
            "désactivez-le" },
        { "plaza.log, beside your pass on the SD card - it names this console and "
          "the server",
            "plaza.log, à côté de votre carte sur la carte SD - il nomme cette "
            "console et le serveur" },

        // -------------------------------------- the rest of the hint strip
        //
        // The verbs, from screens whose own words are still English: the strip
        // along the bottom is chrome, like the tabs, and reads as one thing
        // wherever you are rather than changing language with the screen.
        { "buy", "acheter" },
        { "star", "mettre une étoile" },
        { "unstar", "retirer l'étoile" },
        { "delete", "supprimer" },
        { "save", "enregistrer" },
        { "load this face", "charger ce visage" },
        { "look at it", "la regarder" },
        { "look at them", "les regarder" },
        { "carry it", "l'emporter" },
        { "put it back", "la remettre" },
        { "trade back", "renvoyer" },
        { "toggle", "basculer" },
        { "block - offline", "bloquer - hors ligne" },
        { "make my pass", "créer ma carte" },
        { "not now", "plus tard" },
        { "your details", "vos infos" },
        { "your mii", "votre mii" },

        // ------------------------------------------------------ how long ago
        //
        // The one place a French sentence will not take the English shape:
        // "3 days ago" puts the number in the middle, "il y a 3 jours" puts it
        // at the end, which is why relativeTime() hands the whole line to
        // format() rather than gluing a number to a word.
        { "unknown", "inconnu" },
        { "just now", "à l'instant" },
        { "%llum ago", "il y a %llu min" },
        { "%lluh ago", "il y a %llu h" },
        { "Yesterday", "Hier" },
        { "%d days ago", "il y a %d jours" },
        // A date older than a week: the day, then the month, which happens to
        // be the order both languages use.
        { "%d %s", "%d %s" },
        { "Jan", "janv." },
        { "Feb", "févr." },
        { "Mar", "mars" },
        { "Apr", "avr." },
        { "May", "mai" },
        { "Jun", "juin" },
        { "Jul", "juil." },
        { "Aug", "août" },
        { "Sep", "sept." },
        { "Oct", "oct." },
        { "Nov", "nov." },
        { "Dec", "déc." },
        // The weekdays, for whenever something asks for one: weekdayShort()
        // has no caller today.
        { "Sun", "dim." },
        { "Mon", "lun." },
        { "Tue", "mar." },
        { "Wed", "mer." },
        { "Thu", "jeu." },
        { "Fri", "ven." },
        { "Sat", "sam." },

        // ------------------------------------------------------- the plaza
        { "the plaza - today", "le plaza - aujourd'hui" },
        { "Nobody has crossed you yet", "Personne ne vous a encore croisé" },
        { "1 person crossed your path", "1 personne a croisé votre chemin" },
        { "%u people crossed your path", "%u personnes ont croisé votre chemin" },
        { "1 pass waiting for you", "1 carte vous attend" },
        { "%u passes waiting for you", "%u cartes vous attendent" },
        { "Nothing new today", "Rien de nouveau aujourd'hui" },
        { "Take the console outside, or join a busier network. Passes arrive on "
          "their own.",
            "Sortez la console, ou rejoignez un réseau plus fréquenté. Les cartes "
            "arrivent d'elles-mêmes." },
        { "Somewhere out there.", "Quelque part par là." },
        { ", ", ", " },
        { ", and ", " et " },
        { "Unopened passes", "Cartes non ouvertes" },
        { "nothing yet", "rien pour l'instant" },
        { "all caught up", "tout est à jour" },
        { "%u waiting", "%u en attente" },
        { " - oldest %s", " - la plus ancienne %s" },
        { " - %u crossings, %u place", " - %u croisements, %u lieu" },
        { " - %u crossings, %u places", " - %u croisements, %u lieux" },
        { "new", "nouveau" },
        { "hidden title", "jeu masqué" },
        { "%s - %s", "%s - %s" },
        { "%d more", "%d de plus" },
        { "The plaza is empty", "Le plaza est vide" },
        { "Your console trades a pass with anyone whose Switch is on the same "
          "network, or nearby on the internet. Leave it running and come back.",
            "Votre console échange une carte avec quiconque a sa Switch sur le même "
            "réseau, ou à proximité sur internet. Laissez tourner et revenez." },
        { "Looking for passes", "Recherche de cartes" },
        { "Asking the plaza who else has been around.",
            "On demande au plaza qui est passé par là." },

        // ------------------------------------------------------ the collection
        { "Starred %s", "%s mis en favori" },
        { "Unstarred %s", "%s retiré des favoris" },
        { "Kept when the collection fills up.",
            "Conservé quand la collection est pleine." },
        { "No longer kept when the collection fills up.",
            "Plus conservé quand la collection est pleine." },
        { "%u %s met", "%u %s croisées" },
        { "person", "personne" },
        { "people", "personnes" },
        { "%s - %u crossings in total", "%s - %u croisements en tout" },
        { "by name", "par nom" },
        { "starred first", "favoris d'abord" },
        { "most recent first", "plus récents d'abord" },
        { "sort by name", "trier par nom" },
        { "sort by starred", "trier par favoris" },
        { "sort by recent", "trier par date" },
        { "Nothing collected yet. The first pass you receive lands here and stays.",
            "Rien de collecté pour l'instant. La première carte reçue arrive ici et "
            "y reste." },
        { "met %u times", "croisé %u fois" },

        // ------------------------------------------------------- the Square
        { "Everyone you cross paths with turns up here.",
            "Tous ceux que vous croisez se retrouvent ici." },
        { "A few of the people you have met, milling about.",
            "Quelques personnes que vous avez croisées, qui traînent par là." },

        // --------------------------------------------------------- nearby
        { "live - scanning", "en direct - recherche" },
        { "live - trading", "en direct - échange" },
        { "offline", "hors ligne" },
        { "cannot reach the plaza", "le plaza est injoignable" },
        { "1 console awake near you", "1 console active près de vous" },
        { "%d consoles awake near you", "%d consoles actives près de vous" },
        { "Passes exchange on their own. You do not have to sit here.",
            "Les cartes s'échangent toutes seules. Rien ne vous oblige à rester "
            "là." },
        { "Nobody else has checked in here yet. Leave the app open; the plaza "
          "fills faster if you do.",
            "Personne d'autre ne s'est encore signalé ici. Laissez l'app ouverte ; "
            "le plaza se remplit plus vite ainsi." },
        { "waiting", "en attente" },
        { "exchanging...", "échange en cours..." },
        { "passed", "croisé" },
        { "out of range", "hors de portée" },
        { "same network", "même réseau" },
        { "nearby network", "réseau voisin" },
        { "somewhere else", "ailleurs" },
        { "Up to %d per day while the app is open",
            "Jusqu'à %d par jour tant que l'app est ouverte" },
        { " - matching on \"%s\"", " - accordé sur \"%s\"" },
        { " - on \"%s\", no name to match",
            " - sur \"%s\", aucun nom sur lequel s'accorder" },

        // ----------------------------------------------------- a peer nearby
        { "awake near you", "active près de vous" },
        { "Someone", "Quelqu'un" },
        { "How close", "Distance" },
        { "Playing", "En train de jouer" },
        { "Right now", "En ce moment" },
        { "Trading passes", "Échange de cartes" },
        { "You have already crossed", "Vous vous êtes déjà croisés" },
        { "Drifted out of range", "Sorti de portée" },
        { "Waiting for the next round", "En attente du prochain tour" },
        { "Passes trade on their own with everyone in range. There is nothing to "
          "press here, and nothing to wait for.",
            "Les cartes s'échangent toutes seules avec tout le monde à portée. Il "
            "n'y a rien à presser ici, et rien à attendre." },

        // -------------------------------------------------------- the puzzles
        { "Nothing to collect yet", "Rien à collecter pour l'instant" },
        { "Every puzzle is finished", "Tous les puzzles sont terminés" },
        { "%d of %zu finished", "%d sur %zu terminés" },
        { "Every console you cross brings one piece. Pick which puzzle they go "
          "into.",
            "Chaque console croisée apporte une pièce. Choisissez dans quel puzzle "
            "elles vont." },
        { "filling", "en cours" },
        { "finished", "terminé" },
        { "%d of %u pieces", "%d pièces sur %u" },
        { "already finished", "déjà terminé" },
        { "already filling", "déjà en cours" },
        { "fill this one next", "remplir celui-ci ensuite" },
        { "%s is finished", "%s est terminé" },
        { "Pieces go into the first puzzle that is not.",
            "Les pièces vont dans le premier puzzle qui ne l'est pas." },
        { "Already filling %s", "%s est déjà en cours" },
        { "Every crossing brings a piece of this one.",
            "Chaque croisement apporte une pièce de celui-ci." },
        { "Filling %s", "%s en cours" },
        { "Crossings go into this one until you pick another.",
            "Les croisements vont dans celui-ci jusqu'à ce que vous en choisissiez "
            "un autre." },
        { "One person brought this", "Une seule personne a apporté ceci" },
        { "%d people brought this", "%d personnes ont apporté ceci" },
        { " - finished %s", " - terminé %s" },
        { "Piece %d", "Pièce %d" },
        { "Brought by", "Apportée par" },
        { "Not found yet", "Pas encore trouvée" },
        { "someone", "quelqu'un" },
        { "Cross someone while this puzzle is the one being filled.",
            "Croisez quelqu'un pendant que ce puzzle est celui en cours." },
        { "Collected before the app started keeping track.",
            "Collectée avant que l'app ne garde une trace." },

        // ---------------------------------------------------------- the shop
        { "%u earned - %u spent", "%u gagnées - %u dépensées" },
        { "A piece you do not have yet of %s - the puzzle your crossings are "
          "filling, %d of %u so far. Twice the price, because you get to say "
          "which picture it goes into.",
            "Une pièce de %s que vous n'avez pas encore - le puzzle que vos "
            "croisements remplissent, %d sur %u pour l'instant. Deux fois le prix, "
            "parce que c'est vous qui choisissez l'image." },
        { "%s is finished. Choose another to fill on the puzzles screen, or take "
          "your chances with any puzzle.",
            "%s est terminé. Choisissez-en un autre à remplir sur l'écran des "
            "puzzles, ou tentez votre chance sur n'importe quel puzzle." },
        { "A piece of any puzzle", "Une pièce de n'importe quel puzzle" },
        { "Half the price, and you do not choose: one piece drawn from every "
          "unfinished puzzle at once - %d still to find across %d of them.",
            "Moitié prix, et vous ne choisissez pas : une pièce tirée parmi tous "
            "les puzzles inachevés à la fois - %d encore à trouver dans %d "
            "d'entre eux." },
        { "Every puzzle is finished. There is nothing left to sell you.",
            "Tous les puzzles sont terminés. Il n'y a plus rien à vous vendre." },
        { "Nothing on the shelf yet.", "Rien en rayon pour l'instant." },
        { "%u more coins needed. Ten arrive on every new day you open the app.",
            "Il manque %u pièces. Dix arrivent chaque nouveau jour où vous ouvrez "
            "l'app." },
        { "sold out", "épuisé" },
        { "%u coins short", "Il manque %u pièces" },
        { "Nothing to sell there", "Rien à vendre de ce côté" },
        { "There are no pieces left to find there.",
            "Il n'y a plus aucune pièce à trouver là." },
        { "Piece %d of %s", "Pièce %d de %s" },
        { "%u coins left.", "Il reste %u pièces." },
        { "Buy %s for %u coins?", "Acheter %s pour %u pièces ?" },
        { "%s That leaves you %u.", "%s Il vous restera %u." },
        { "One you do not hold yet, into the puzzle you are filling.",
            "Une que vous n'avez pas encore, dans le puzzle en cours." },
        { "Drawn from every unfinished puzzle at once, so it may not be the one "
          "you are filling.",
            "Tirée parmi tous les puzzles inachevés, ce ne sera donc pas forcément "
            "celui en cours." },
        { "Buy it", "Acheter" },

        // -------------------------------------------------------- your pass
        { "your pass", "votre carte" },
        { "This is what they see", "Voilà ce qu'ils voient" },
        { "%s theme", "thème %s" },
        { "(no greeting yet)", "(pas encore de message)" },
        { "Name", "Nom" },
        { "not set yet", "pas encore défini" },
        { "Greeting", "Message" },
        { "%zu of 60 characters", "%zu caractères sur 60" },
        { "Card theme", "Thème de la carte" },
        { "%s - 6 unlocked", "%s - 6 débloqués" },
        { "What you carry", "Ce que vous transportez" },
        { "Title on your pass", "Jeu sur votre carte" },
        { "hidden by privacy settings", "masqué par les réglages de confidentialité" },
        { "reading the play history...", "lecture de l'historique de jeu..." },
        { "nothing played on this console yet",
            "rien joué sur cette console pour l'instant" },
        { "hidden", "masqué" },
        { "%s - %uh", "%s - %uh" },
        { "edit", "modifier" },
        { "edit your mii", "modifier votre mii" },
        { "next theme", "thème suivant" },
        { "next title", "jeu suivant" },
        { "The name on your pass", "Le nom sur votre carte" },
        { "Up to 60 characters", "Jusqu'à 60 caractères" },
        { "Your pass never carries your account name, your friend code, or a "
          "precise position - only what you typed and, if you allow it, the place "
          "you named.",
            "Votre carte ne transporte jamais le nom de votre compte, votre code "
            "ami ni une position précise - seulement ce que vous avez écrit et, si "
            "vous l'autorisez, le lieu que vous avez nommé." },

        // ------------------------------------------------------ what you carry
        { "on your pass", "sur votre carte" },
        { "%zu of %d", "%zu sur %d" },
        { "Four is the limit", "Quatre au maximum" },
        { "Put something back before picking this up.",
            "Reposez quelque chose avant de prendre ceci." },

        // -------------------------------------------------------- a crossing
        { "crossed paths - %s", "chemins croisés - %s" },
        { "playing %s", "joue à %s" },
        { "somewhere on the network", "quelque part sur le réseau" },
        { "carrying", "transporte" },
        { "time crossed", "fois croisé" },
        { "times crossed", "fois croisés" },
        { "game installed", "jeu installé" },
        { "games installed", "jeux installés" },
        { "hours played", "heures de jeu" },
        { "in %s", "sur %s" },
        { "Already traded back", "Déjà renvoyé" },
        { "Trade something back", "Renvoyer quelque chose" },
        { "Take it, send something back", "Le prendre, renvoyer quelque chose" },
        { "Star this card", "Mettre cette carte en favori" },
        { "Unstar this card", "Retirer cette carte des favoris" },
        { "Sent something back to %s", "Quelque chose renvoyé à %s" },
        { "It travels with your pass the next time you cross.",
            "Ça voyagera avec votre carte au prochain croisement." },
        { "Blocking has to reach the server, or they would keep receiving your "
          "pass. Try again when the dot by the tabs is green.",
            "Bloquer doit passer par le serveur, sinon ils continueraient à "
            "recevoir votre carte. Réessayez quand le point près des onglets est "
            "vert." },
        { "Block %s?", "Bloquer %s ?" },
        { "Their pass is deleted and this console can never cross you again. They "
          "are not told.",
            "Leur carte est supprimée et cette console ne pourra plus jamais vous "
            "croiser. Ils n'en sont pas informés." },
        { "Block and forget", "Bloquer et oublier" },

        // ------------------------------------------------------- your Mii
        { "your face", "votre visage" },
        { "Make your Mii", "Créez votre Mii" },
        { "how others see you", "comme les autres vous voient" },
        { "yes", "oui" },
        { "no", "non" },
        { "%d of %d", "%d sur %d" },
        { "Face shape", "Forme du visage" },
        { "Skin tone", "Teint" },
        { "Hair", "Cheveux" },
        { "Hair colour", "Couleur des cheveux" },
        { "Hair flipped", "Cheveux inversés" },
        { "Eyes", "Yeux" },
        { "Eye colour", "Couleur des yeux" },
        { "Eyebrows", "Sourcils" },
        { "Eyebrow colour", "Couleur des sourcils" },
        { "Nose", "Nez" },
        { "Mouth", "Bouche" },
        { "Lip colour", "Couleur des lèvres" },
        { "Glasses", "Lunettes" },
        { "Glasses colour", "Couleur des lunettes" },
        { "Moustache", "Moustache" },
        { "Beard", "Barbe" },
        { "Facial hair colour", "Couleur de la barbe" },
        { "Wrinkles", "Rides" },
        { "Makeup", "Maquillage" },
        { "Mole", "Grain de beauté" },
        { "Headwear", "Chapeau" },
        { "Build", "Corpulence" },
        { "Height", "Taille" },
        { "Favourite colour", "Couleur préférée" },
        { "Eye spacing", "Écart des yeux" },
        { "Eye height", "Hauteur des yeux" },
        { "Eye width", "Largeur des yeux" },
        { "Eye height scale", "Étirement des yeux" },
        { "Eye tilt", "Inclinaison des yeux" },
        { "Eyebrow spacing", "Écart des sourcils" },
        { "Eyebrow height", "Hauteur des sourcils" },
        { "Eyebrow width", "Largeur des sourcils" },
        { "Eyebrow thickness", "Épaisseur des sourcils" },
        { "Eyebrow tilt", "Inclinaison des sourcils" },
        { "Nose height", "Hauteur du nez" },
        { "Nose size", "Taille du nez" },
        { "Mouth height", "Hauteur de la bouche" },
        { "Mouth width", "Largeur de la bouche" },
        { "Mouth thickness", "Épaisseur de la bouche" },
        { "Moustache height", "Hauteur de la moustache" },
        { "Moustache size", "Taille de la moustache" },
        { "Beard height", "Hauteur de la barbe" },
        { "Beard size", "Taille de la barbe" },
        { "Glasses height", "Hauteur des lunettes" },
        { "Glasses size", "Taille des lunettes" },
        { "Mole across", "Grain de beauté, horizontal" },
        { "Mole down", "Grain de beauté, vertical" },
        { "Mole size", "Taille du grain de beauté" },

        // -------------------------------------------------- faces on the card
        { "faces on the sd card", "visages sur la carte sd" },
        { "Save and load", "Sauver et charger" },
        { "Save this face...", "Sauver ce visage..." },
        { "Name this face", "Nommez ce visage" },
        { "Needs a name", "Il faut un nom" },
        { "That name has nothing in it a file can be called.",
            "Ce nom ne contient rien dont un fichier puisse porter le nom." },
        { "Saved", "Sauvé" },
        { "%s is in the export folder.", "%s est dans le dossier d'export." },
        { "Not saved", "Non sauvé" },
        { "Replace %s?", "Remplacer %s ?" },
        { "There is already a face saved under that name. Replacing it cannot be "
          "undone.",
            "Un visage est déjà sauvé sous ce nom. Le remplacer est sans retour." },
        { "Replace", "Remplacer" },
        { "Delete %s?", "Supprimer %s ?" },
        { "It is removed from the SD card for good. Anyone you sent it to still "
          "has their copy.",
            "Il est retiré de la carte SD pour de bon. Ceux à qui vous l'avez "
            "envoyé gardent leur copie." },
        { "Delete", "Supprimer" },
        { "Deleted", "Supprimé" },
        { "%s is gone from the export folder.",
            "%s a disparu du dossier d'export." },
        { "Not deleted", "Non supprimé" },
        { "Cannot load that one", "Impossible de charger celui-là" },
        { "Not a face this version can read",
            "Pas un visage que cette version sait lire" },
        { "Nothing saved yet. Faces you save show up here, and so does anything "
          "you drop into the folder yourself.",
            "Rien de sauvé pour l'instant. Les visages que vous sauvez "
            "apparaissent ici, ainsi que tout ce que vous déposez vous-même dans "
            "le dossier." },

        // --------------------------------------------------------- first run
        { "Leave it open.", "Laissez-la ouverte." },
        { "While this is open, your console swaps a small pass with the others "
          "that are open too - a face, a greeting, whatever you chose to carry. "
          "Who you meet is whoever is awake within the reach you set.",
            "Tant que ceci est ouvert, votre console échange une petite carte avec "
            "les autres qui le sont aussi - un visage, un message, ce que vous "
            "avez choisi de transporter. Vous croisez toutes les consoles actives "
            "dans la portée que vous avez réglée." },
        { "Make your pass - a face, a greeting, one thing to trade",
            "Créez votre carte - un visage, un message, une chose à échanger" },
        { "Leave it open; it checks in on its own",
            "Laissez-la ouverte ; elle se signale toute seule" },
        { "Open the plaza and see who you crossed",
            "Ouvrez le plaza et voyez qui vous avez croisé" },
        { "A greeting, up to 60 characters", "Un message, jusqu'à 60 caractères" },
        { "pair this console", "appairer cette console" },
        { "A picture of this console's id. It lives on the SD card, is tied to "
          "nothing about the hardware, and you can throw it away.",
            "Une image de l'identifiant de cette console. Elle vit sur la carte "
            "SD, n'est liée à rien du matériel, et vous pouvez la jeter." },

        // ------------------------------------------- updates, and the app itself
        { "Version %s is out", "La version %s est sortie" },
        { "Settings -> About -> Check for updates to install it.",
            "Paramètres -> À propos -> Chercher une mise à jour pour l'installer." },
        { "Puzzle art downloaded", "Images des puzzles téléchargées" },
        { "Restart the app to see the pictures.",
            "Relancez l'app pour voir les images." },
        { "Update installed", "Mise à jour installée" },
        { "Restart from Settings -> \"About\" to run version %s.",
            "Relancez depuis Paramètres -> \"À propos\" pour lancer la version %s." },
        { "Up to date", "À jour" },
        { "%s is the latest release.", "%s est la dernière version." },
        { "Could not download puzzle art",
            "Téléchargement des images impossible" },
        { "Could not check for updates",
            "Impossible de chercher une mise à jour" },
        { "Install version %s?", "Installer la version %s ?" },
        { "The new version is downloaded, checked, and only then written over "
          "this one. The copy you are running now is kept until the new one has "
          "been verified.",
            "La nouvelle version est téléchargée, vérifiée, et seulement ensuite "
            "écrite par-dessus celle-ci. La copie que vous utilisez est conservée "
            "jusqu'à ce que la nouvelle soit vérifiée." },
        { "Install", "Installer" },
        { "Restart into version %s?", "Relancer sur la version %s ?" },
        { "The app closes and opens again on the new version. Your passes and "
          "your collection are untouched.",
            "L'app se ferme et se rouvre sur la nouvelle version. Vos cartes et "
            "votre collection ne sont pas touchées." },
        { "Restart", "Relancer" },
        { "%s passed you", "%s vous a croisé" },
        { "%s and %zu others passed you", "%s et %zu autres vous ont croisé" },
        { "%zu new passes", "%zu nouvelles cartes" },
        { "a piece of %s", "une pièce de %s" },
        { "%d pieces of %s", "%d pièces de %s" },
        { "one of them is carrying %s", "l'un d'eux transporte %s" },
        { "Open the plaza to see who.", "Ouvrez le plaza pour voir qui." },
        { "Keep everything", "Ne rien toucher" },

        // ------------------------------------------- settings, the dialogs
        { "Where are you? A district, a station, a shop",
            "Où êtes-vous ? Un quartier, une gare, un magasin" },
        { "Which city", "Quelle ville" },
        { "Plaza server address", "Adresse du serveur du plaza" },
        { "Clear any block the plaza still has?",
            "Effacer les blocages que le plaza garde encore ?" },
        { "Clear all %zu?", "Tous les débloquer, les %zu ?" },
        { "This console has none listed, but the plaza keeps its own copy and a "
          "restored backup can leave the two disagreeing. Nothing happens if it "
          "has none either.",
            "Cette console n'en liste aucun, mais le plaza garde sa propre copie "
            "et une sauvegarde restaurée peut laisser les deux en désaccord. Rien "
            "ne se passe s'il n'en a aucun non plus." },
        { "Every console you have blocked can cross you again, except any that "
          "blocked you as well. Their old passes are not coming back; only the "
          "block is lifted.",
            "Chaque console que vous avez bloquée peut vous croiser de nouveau, "
            "sauf celles qui vous ont bloqué aussi. Leurs anciennes cartes ne "
            "reviennent pas ; seul le blocage est levé." },
        { "Clear them", "Les débloquer" },
        { "Block list cleared", "Liste de blocage vidée" },
        { "This console has none left, and the plaza has been asked to drop the "
          "blocks it was holding for you.",
            "Cette console n'en a plus, et on a demandé au plaza d'oublier les "
            "blocages qu'il gardait pour vous." },
        { "Delete every pass?", "Supprimer toutes les cartes ?" },
        { "Every pass you have collected is removed from this console. Your own "
          "pass and your identity stay.",
            "Toutes les cartes que vous avez collectées sont retirées de cette "
            "console. Votre propre carte et votre identité restent." },
        { "Delete them all", "Tout supprimer" },
        { "Collection cleared", "Collection vidée" },
        { "The plaza is empty again.", "Le plaza est vide à nouveau." },
        { "Start over as someone new?", "Repartir de zéro sous une autre identité ?" },
        { "This console gets a brand new id. Passes you already handed out can no "
          "longer be linked to you, and your collection is deleted.",
            "Cette console reçoit un identifiant tout neuf. Les cartes déjà "
            "distribuées ne peuvent plus être reliées à vous, et votre collection "
            "est supprimée." },
        { "Start over", "Repartir de zéro" },
        { "You are someone new", "Vous êtes quelqu'un d'autre" },
        { "Your code is now %s", "Votre code est maintenant %s" },

        // ------------------------------------------- what the plaza is doing
        //
        // These come off the sync and update threads, and are read back on
        // screen in Settings and in Nearby. Whole sentences are translated;
        // the protocol diagnostics they sometimes carry - an HTTP status, a
        // libnx result code - stay as they are, because they exist to be
        // pasted into a bug report.
        { "Trading passes...", "Échange de cartes..." },
        { "Connected over LAN: matching by network area only.",
            "Connecté en réseau local : accord par zone réseau seulement." },
        { "Nobody else is awake here yet.",
            "Aucune autre console active ici pour l'instant." },
        { "No plaza server set yet. Settings > Plaza server.",
            "Aucun serveur de plaza défini. Paramètres > Serveur du plaza." },
        { "Server sent something that was not JSON",
            "Le serveur a envoyé autre chose que du JSON" },
        { "Server said %ld on %s", "Le serveur a répondu %ld sur %s" },
        { "Server said %ld on hello", "Le serveur a répondu %ld à hello" },
        { "Checking for updates", "Recherche de mises à jour" },
        { "Downloading", "Téléchargement" },
        { "Unpacking", "Décompression" },
        { "Installing", "Installation" },
        { "nx-plaza %s is the latest version.",
            "nx-plaza %s est la dernière version." },
        { "Release %s has no .nro to install.",
            "La version %s n'a aucun .nro à installer." },
        { "Version %s is available.", "La version %s est disponible." },
        { "The download is version %s, not %s.",
            "Le téléchargement est la version %s, pas %s." },
        { "Version %s is installed. Restart to run it.",
            "La version %s est installée. Relancez pour l'utiliser." },
        { "%d puzzle pictures downloaded. Restart to see them.",
            "%d images de puzzle téléchargées. Relancez pour les voir." },

        // ------------------------------------------- the last of the settings
        { "Asking GitHub for the latest release",
            "On demande à GitHub la dernière version" },
        { "Press A to download and install it",
            "Appuyez sur A pour la télécharger et l'installer" },
        { "Press A to restart into it", "Appuyez sur A pour relancer dessus" },
        { "You are on the latest release", "Vous avez la dernière version" },
        { "up to date", "à jour" },
        { "Looks at the releases on GitHub",
            "Consulte les versions publiées sur GitHub" },
        { "If the puzzles show numbered squares, get the pictures here and "
          "restart",
            "Si les puzzles affichent des cases numérotées, récupérez les images "
            "ici et relancez" },
        { "failed", "échec" },
        { "Somebody", "Quelqu'un" },
        { "keep everything", "ne rien toucher" },
        { "switch tab", "changer d'onglet" },

        // ------------------------------------------------- the puzzle pictures
        { "Forest in the Rain", "Forêt sous la pluie" },
        { "Mountain in the Fog", "Montagne dans la brume" },
        { "Mystical Swamp", "Marais mystique" },
        { "Old Farm", "Vieille ferme" },
        { "Castle Blue", "Château bleu" },
        { "Beach at Dusk", "Plage au crépuscule" },

        // --------------------------------------------------- the card themes
        { "Amber lantern", "Lanterne d'ambre" },
        { "Dusk market", "Marché au crépuscule" },
        { "Tidepool", "Bassin de marée" },
        { "Paper lantern", "Lanterne de papier" },
        { "Rose market", "Marché rose" },
        { "Night express", "Express de nuit" },

        // ----------------------------------------------- the pass you start with
        { "Traveller", "Voyageur" },
        { "Just passing through.", "Juste de passage." },

        // ------------------------------------------- what the plaza is saying
        { "Looking around...", "Recherche autour de vous..." },
        { "Publishing your pass...", "Publication de votre carte..." },
        { "Blocking a console...", "Blocage d'une console..." },
        { "Unblocking a console...", "Déblocage d'une console..." },
        { "Clearing the block list...", "Effacement de la liste de blocage..." },
        { "Asking the server to forget us...",
            "On demande au serveur de nous oublier..." },
        { "Daily crossing limit reached. Back tomorrow.",
            "Limite de croisements du jour atteinte. À demain." },
        { "Networking is unavailable.", "Le réseau est indisponible." },
        { "No internet connection.", "Aucune connexion internet." },

        // ------------------------------------------- when an update goes wrong
        { "This console would not start the update worker.",
            "Cette console n'a pas pu démarrer le processus de mise à jour." },
        { "The release list could not be read.",
            "La liste des versions n'a pas pu être lue." },
        { "The release list carried no version.",
            "La liste des versions ne contenait aucune version." },
        { "The download did not finish: %s",
            "Le téléchargement n'a pas abouti : %s" },
        { "The downloaded archive held no nx-plaza build.",
            "L'archive téléchargée ne contenait aucune version de nx-plaza." },
        { "The downloaded file is not a valid nx-plaza build.",
            "Le fichier téléchargé n'est pas une version valide de nx-plaza." },
        { "This build does not know its own path, so it cannot replace itself.",
            "Cette version ne connaît pas son propre chemin, elle ne peut donc pas "
            "se remplacer." },
        { "The current version could not be backed up.",
            "La version actuelle n'a pas pu être sauvegardée." },
        { "The update could not be written over the current one - %s",
            "La mise à jour n'a pas pu être écrite par-dessus l'actuelle - %s" },
        { "The installed update did not verify, so it was rolled back.",
            "La mise à jour installée n'a pas été vérifiée, elle a donc été "
            "annulée." },
        { "That release publishes no puzzle art.",
            "Cette version ne publie aucune image de puzzle." },
        { "That release's archive holds no puzzle art.",
            "L'archive de cette version ne contient aucune image de puzzle." },
        { "The puzzle art that arrived was not readable.",
            "Les images de puzzle reçues n'étaient pas lisibles." },
        { "The folder for the puzzle art could not be made.",
            "Le dossier des images de puzzle n'a pas pu être créé." },
        { "The puzzle art could not be written - %s",
            "Les images de puzzle n'ont pas pu être écrites - %s" },

        // ------------------------------------------ the settings sections
        //
        // The eight names down the left of the screen, and the pills on the
        // rows that offer a choice.
        { "Privacy", "Confidentialité" },
        { "Exchange", "Échange" },
        { "Appearance", "Apparence" },
        { "Languages", "Langues" },
        { "This console", "Cette console" },
        { "Data", "Données" },
        { "About", "À propos" },
        { "Off", "Désactivé" },
        { "District", "Quartier" },
        { "City", "Ville" },
        { "Same network", "Même réseau" },
        { "Anywhere", "Partout" },
        { "Light", "Clair" },
        { "Dark", "Sombre" },

        // -------------------------------------- when the SD card says no
        { "Could not make the export folder on the SD card.",
            "Le dossier d'export n'a pas pu être créé sur la carte SD." },
        { "Could not write to the SD card.",
            "Écriture impossible sur la carte SD." },
        { "That file could not be read.", "Ce fichier n'a pas pu être lu." },
        { "That is not a face this app saved.",
            "Ce n'est pas un visage sauvé par cette app." },
        { "That face was saved by a different version of the app.",
            "Ce visage a été sauvé par une autre version de l'app." },
        { "Could not delete that file from the SD card.",
            "Ce fichier n'a pas pu être supprimé de la carte SD." },
        { "could not make the backup folder",
            "le dossier de sauvegarde n'a pas pu être créé" },
        { "could not copy %s", "impossible de copier %s" },
        { "there was nothing to back up", "il n'y avait rien à sauvegarder" },

        // ------------------------------------------------------- your plaza
        //
        // The stats page off the pass. Its captions sit under a number, so
        // they are fragments rather than sentences: "croisements en tout"
        // under 214, the way a caption reads on a card.
        { "your record", "votre parcours" },
        { "Everything so far", "Le bilan jusqu'ici" },
        { "crossings in total", "croisements en tout" },
        { "the one you cross most", "la personne la plus croisée" },
        { "places", "lieux" },
        { "your oldest card", "votre plus vieille carte" },
        { "starred", "en favoris" },
        { "days you have crossed somebody",
            "les jours où vous avez croisé quelqu'un" },
        { "%u days", "%u jours" },
        { "%u months", "%u mois" },
        { "%u years", "%u ans" },
        { "sent something back", "renvois" },
        { "still unopened", "encore non ouvertes" },
        { "pieces", "pièces" },
        { "pictures finished", "images terminées" },
        { "people gave you a piece", "personnes vous ont donné une pièce" },
        { "coins", "pièces" },
        { "earned by turning up", "gagnées en venant" },
        { "spent", "dépensées" },
        { "won at the games", "gagnées aux jeux" },
        { "the longest run", "la plus longue course" },
        { "the tallest tower", "la plus haute tour" },
        { "duels in a row", "duels d'affilée" },

        // ------------------------------------------------- the same in French
        //
        // Listed on purpose rather than left out, so the scanner can tell a
        // word that needs no translation from one nobody has looked at yet.
        // tr() drops these rows: falling back to the English is the same
        // answer, and cheaper.
        { "Plaza", "Plaza" },
        { "Collection", "Collection" },
        { "collection", "collection" },
        { "Puzzles", "Puzzles" },
        { "puzzles", "puzzles" },
        { "plaza dash", "plaza dash" },
        { "sections", "sections" },
        { "m", "m" },
        { "-", "-" },
        { "%d", "%d" },
        { "Notifications", "Notifications" },
        { "Console", "Console" },
        { "nx-plaza ", "nx-plaza " },
        { "(none)", "(none)" },
        { "NX Plaza - Online StreetPass | ", "NX Plaza - Online StreetPass | " },
        { "https://github.com/Insektaure/NX-Plaza",
            "https://github.com/Insektaure/NX-Plaza" },
    };

} // namespace

Catalog frenchCatalog()
{
    return Catalog { kEntries, sizeof(kEntries) / sizeof(kEntries[0]) };
}

} // namespace nxp
