#pragma once

#include <switch.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace nxp {

// The noises the app can make.
//
// Named for what happened rather than for how it sounds, so a sound can be
// retuned without every call site being a lie afterwards.
enum class Sfx : uint8_t {
    Move,   // the cursor stepped
    Select, // A, on something that does a thing
    Back,   // B
    Toast,  // something arrived, or the app has something to say
    Coin,   // coins changed hands
    Trophy, // one was earned
    Hit,    // a blow, in the quest
    Crit,   // a blow that told
    Heal,   // a Mender caught somebody
    Fall,   // somebody went down
    Win,    // a floor cleared, a bet won
    Lose,   // the party wiped, a bet lost
    Tick,   // a reel stopping, a lantern passing
    Count,
};

// Sound, synthesised.
//
// Nothing is sampled: every noise is a few struck notes and a small room,
// mixed here and pushed to `audout`. That is the whole reason the app can have
// sound at all without the .nro growing - an app whose entire artwork is drawn
// from arithmetic should not carry a megabyte of wav files to say "a coin".
//
// A console that cannot open the audio device keeps working in silence: every
// entry point checks and returns.
class Audio {
public:
    static Audio& get();

    // Opens the device and starts the mixing thread. False means no sound;
    // it is never a reason to stop launching.
    bool init();
    void exit();

    // Queues one. Safe from any thread, and cheap enough to call from a draw
    // loop: it copies a byte into a pending list and returns.
    void play(Sfx which);

    // Off while the console is showing somebody the HOME menu. The app keeps
    // running in the background to carry on trading.
    void setMuted(bool muted);

    // The setting. Silence takes effect on the next buffer, so anything
    // already sounding stops within about twenty milliseconds.
    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }

private:
    // One struck note, while it is ringing.
    struct Voice {
        bool live = false;
        int32_t wait = 0;   // frames before it is struck
        int32_t at = 0;     // frames since
        int32_t len = 1;    // frames until it has decayed to nothing
        int32_t attack = 1; // frames of fade-in, so it does not click
        float from = 440.0f;
        float to = 440.0f; // the same, unless the note bends
        float gain = 0.5f;
        float phase = 0.0f;
        float decay = 1.0f; // per-frame multiplier on the envelope
        float env = 1.0f;   // where that multiplier has got to
        float lp = 0.0f;    // one-pole state, for the noise timbre
        uint32_t seed = 0x1234567u;
        uint8_t timbre = 0;
    };

    static constexpr size_t kVoices = 24;
    static constexpr size_t kPending = 16;
    static constexpr size_t kBuffers = 3;
    // 0x1000-aligned, which the device requires of both the memory and the
    // size. One page is 1024 stereo frames, about 21 ms, and three of them
    // queued is the whole reason to keep them small: a tick that answers a
    // button press 130 ms later is not an answer. Three pages is 64 ms of
    // queued sound - 64 ms of slack against a frame that runs long, and at
    // worst 64 ms before a new sound is heard.
    static constexpr size_t kBufferBytes = 0x1000;

    static void threadEntry(void* self);
    void run();
    void fill(int16_t* out, size_t frames);
    void spawn(Sfx which);
    void silence();

    Thread m_thread {};
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_started { false };
    std::atomic<bool> m_enabled { true };
    std::atomic<bool> m_muted { false };

    std::mutex m_lock;
    uint8_t m_pending[kPending] = {};
    size_t m_pendingCount = 0;
    // Set by whoever asks for quiet, acted on by the mixer: the voices belong
    // to the mixing thread and nobody else may reach into them.
    bool m_stop = false;

    AudioOutBuffer m_slots[kBuffers] = {};
    Voice m_voices[kVoices] = {};

    // The room: two delay lines of different lengths, cross-fed and damped.
    // The cheapest thing that stops every note sounding like it was played
    // inside a telephone.
    size_t m_roomL = 0; // how far back each side reads
    size_t m_roomR = 0;
    size_t m_roomAt = 0; // where both of them write
    float m_dampL = 0.0f;
    float m_dampR = 0.0f;

    uint32_t m_rate = 48000;
    uint32_t m_channels = 2;
    size_t m_frames = 0;
    float m_invRate = 1.0f / 48000.0f;
};

// Shorthand, because the alternative at four hundred call sites is a line that
// says Audio::get() twice as often as it says what happened.
inline void playSfx(Sfx which) { Audio::get().play(which); }

} // namespace nxp
