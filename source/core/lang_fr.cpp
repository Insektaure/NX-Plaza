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
            "Joué avec les visages et les noms de votre collection : plus vous "
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
        { "none", "aucune" },
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
        { "https://github.com/Insektaure/NX-Plaza",
            "https://github.com/Insektaure/NX-Plaza" },
    };

} // namespace

Catalog frenchCatalog()
{
    return Catalog { kEntries, sizeof(kEntries) / sizeof(kEntries[0]) };
}

} // namespace nxp
