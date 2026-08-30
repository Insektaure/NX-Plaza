#pragma once

#include "gfx/renderer.h"
#include "core/mii.h"
#include "platform/input.h"

#include <functional>

#include <memory>
#include <string>
#include <vector>

namespace nxp {

class App;
struct Peer;

class Scene {
public:
    virtual ~Scene() = default;

    virtual void onEnter(App& app) { }
    virtual void update(App& app, const Input& input, float dt) { }
    virtual void draw(App& app, Renderer& r) = 0;

    // Overlays that own the whole screen (first run) hide the navigation.
    virtual bool coversChrome() const { return false; }
};

std::unique_ptr<Scene> makePlazaScene();
std::unique_ptr<Scene> makeNearbyScene();
std::unique_ptr<Scene> makeCollectionScene();

// The Square: your Mii and up to 29 from the collection, milling about on a
// ground plane. Billboards, not models - there is no 3D pipeline here and the
// crowd faces the camera anyway.
std::unique_ptr<Scene> makeSquareScene();
std::unique_ptr<Scene> makePassportScene();
std::unique_ptr<Scene> makeSettingsScene();
// One collected pass, close up. `siblings` is the list the caller was showing,
// in the order it was showing it, so the shoulder buttons can step through it
// without going back to the grid. Empty means no stepping.
std::unique_ptr<Scene> makeEncounterScene(const std::string& crossingId,
    std::vector<std::string> siblings = {});
// One console on the radar, close up. Read-only: a peer is not a crossing.
std::unique_ptr<Scene> makePeerScene(const Peer& peer);
std::unique_ptr<Scene> makeFirstRunScene();

// The Mii maker. `onDone` receives the finished face, or is not called at all
// if the user backs out.
std::unique_ptr<Scene> makeMiiEditorScene(const Mii& start,
    std::function<void(const Mii&)> onDone);

// The things you carry, picked from a catalogue rather than typed. `onDone`
// receives the chosen list, which is at most four.
std::unique_ptr<Scene> makeCarryScene(std::vector<std::string> chosen,
    std::function<void(std::vector<std::string>)> onDone);

// Saved faces on the SD card. Saves `current` under a name the user types, and
// hands `onPick` a face loaded back out of the folder. `defaultName` is what
// the name prompt opens with - the handle the face belongs to.
std::unique_ptr<Scene> makeMiiFilesScene(const Mii& current, const std::string& defaultName,
    std::function<void(const Mii&)> onPick);

// Asks for a name and writes one face into the export folder, confirming first
// if that name is taken. The card screens use this directly: a face that is not
// yours is something to keep a copy of, never something to load over.
void promptSaveMii(App& app, const Mii& face, const std::string& defaultName,
    std::function<void()> onSaved = nullptr);

} // namespace nxp
