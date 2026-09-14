#include "platform/audio.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nxp {

namespace {
    // ------------------------------------------------------------- timbres

    // What a note is made of: a fundamental and the partials above it, with
    // how much of each. The gains sum to one, so a note's own gain means the
    // same thing whichever timbre it is played on.
    //
    // This is the whole difference between this and a chip. A bare sine is a
    // test tone and a square is a buzzer; four sines in the ratios a struck
    // object actually rings at is a struck object.
    struct Timbre {
        struct Partial {
            float mul; // times the fundamental
            float gain;
        };
        Partial part[4];
        int count;
        float attack; // seconds of fade-in
        float tilt;   // one-pole coefficient: 1 is open, lower is softer
    };

    enum TimbreId : uint8_t {
        // Glass. The partials are deliberately not whole numbers - a real
        // bell is inharmonic, and that slight detune is what makes one
        // shimmer rather than sit there.
        Timbre_Bell = 0,
        // A wooden bar: a strong fundamental, a distant fourth partial, and
        // gone almost at once. Every tick in the app is made of this.
        Timbre_Wood,
        // Nearly a sine, with a touch of octave, and a slow enough attack to
        // sound kind rather than struck.
        Timbre_Soft,
        // The body of a drum: low, a little second partial, and it bends
        // down. Carries the weight of a blow without a square wave in sight.
        Timbre_Body,
        // Air, not noise: white through a hard one-pole lowpass, which is a
        // brush rather than a hiss.
        Timbre_Air,
        Timbre_Count,
    };

    const Timbre kTimbres[Timbre_Count] = {
        // bell
        { { { 1.00f, 0.60f }, { 2.01f, 0.24f }, { 3.02f, 0.11f }, { 5.43f, 0.05f } },
            4, 0.003f, 0.55f },
        // wood
        { { { 1.00f, 0.74f }, { 3.92f, 0.19f }, { 9.10f, 0.07f }, { 0.0f, 0.0f } },
            3, 0.002f, 0.45f },
        // soft
        { { { 1.00f, 0.90f }, { 2.00f, 0.10f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
            2, 0.012f, 0.35f },
        // body
        { { { 1.00f, 0.88f }, { 1.50f, 0.12f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
            2, 0.002f, 0.25f },
        // air
        { { { 1.00f, 1.00f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
            1, 0.001f, 0.18f },
    };

    // -------------------------------------------------------------- sounds

    // One struck note. `bend` of zero holds the pitch; anything else is where
    // it ends up, and it gets there exponentially rather than in a straight
    // line - a linear sweep is a siren, an exponential one is a drum.
    //
    // `decay` is how long it takes to ring down to nothing, which is also how
    // long the note lasts: nothing here sustains, because nothing in the app
    // is held down.
    struct Note {
        float delay;
        float pitch;
        float bend;
        float decay;
        float gain;
        uint8_t timbre;
    };

    // Pitches are the C major pentatonic, two or three octaves above middle
    // C, so two sounds landing at once - which happens constantly, since a
    // floor can clear while a coin is still ringing - is a chord rather than
    // a clash.
    const Note kMove[] = { { 0.000f, 1760.0f, 0.0f, 0.055f, 0.16f, Timbre_Wood } };
    const Note kSelect[] = {
        { 0.000f, 1046.5f, 0.0f, 0.090f, 0.22f, Timbre_Wood },
        { 0.045f, 1568.0f, 0.0f, 0.130f, 0.17f, Timbre_Wood },
    };
    const Note kBack[] = {
        { 0.000f, 784.0f, 0.0f, 0.100f, 0.20f, Timbre_Wood },
        { 0.040f, 523.3f, 0.0f, 0.140f, 0.16f, Timbre_Wood },
    };
    const Note kToast[] = {
        { 0.000f, 1046.5f, 0.0f, 0.500f, 0.21f, Timbre_Bell },
        { 0.100f, 1568.0f, 0.0f, 0.750f, 0.17f, Timbre_Bell },
    };
    const Note kCoin[] = {
        { 0.000f, 1568.0f, 0.0f, 0.180f, 0.20f, Timbre_Bell },
        { 0.055f, 2349.3f, 0.0f, 0.320f, 0.17f, Timbre_Bell },
    };
    const Note kTrophy[] = {
        { 0.000f, 1046.5f, 0.0f, 0.340f, 0.19f, Timbre_Bell },
        { 0.090f, 1318.5f, 0.0f, 0.340f, 0.19f, Timbre_Bell },
        { 0.180f, 1568.0f, 0.0f, 0.340f, 0.19f, Timbre_Bell },
        { 0.270f, 2093.0f, 0.0f, 0.950f, 0.22f, Timbre_Bell },
    };
    // A blow is a body and a brush of air, not a buzz and a hiss.
    const Note kHit[] = {
        { 0.000f, 150.0f, 70.0f, 0.130f, 0.32f, Timbre_Body },
        { 0.000f, 0.0f, 0.0f, 0.050f, 0.10f, Timbre_Air },
    };
    const Note kCrit[] = {
        { 0.000f, 190.0f, 72.0f, 0.190f, 0.34f, Timbre_Body },
        { 0.000f, 0.0f, 0.0f, 0.070f, 0.13f, Timbre_Air },
        { 0.010f, 1318.5f, 0.0f, 0.240f, 0.11f, Timbre_Bell },
    };
    const Note kHeal[] = {
        { 0.000f, 880.0f, 0.0f, 0.360f, 0.16f, Timbre_Soft },
        { 0.070f, 1318.5f, 0.0f, 0.460f, 0.13f, Timbre_Soft },
    };
    const Note kFall[] = { { 0.000f, 330.0f, 90.0f, 0.460f, 0.26f, Timbre_Body } };
    const Note kWin[] = {
        { 0.000f, 1046.5f, 0.0f, 0.300f, 0.19f, Timbre_Bell },
        { 0.085f, 1318.5f, 0.0f, 0.300f, 0.19f, Timbre_Bell },
        { 0.170f, 1568.0f, 0.0f, 0.300f, 0.19f, Timbre_Bell },
        { 0.260f, 2093.0f, 0.0f, 1.000f, 0.22f, Timbre_Bell },
    };
    // A soft falling third.
    const Note kLose[] = {
        { 0.000f, 392.0f, 0.0f, 0.420f, 0.19f, Timbre_Soft },
        { 0.160f, 293.7f, 0.0f, 0.780f, 0.19f, Timbre_Soft },
    };
    const Note kTick[] = { { 0.000f, 1318.5f, 0.0f, 0.045f, 0.18f, Timbre_Wood } };

    struct Recipe {
        const Note* notes;
        size_t count;
    };

#define NXP_SOUND(table) { table, sizeof(table) / sizeof(table[0]) }
    const Recipe kSounds[] = {
        NXP_SOUND(kMove),
        NXP_SOUND(kSelect),
        NXP_SOUND(kBack),
        NXP_SOUND(kToast),
        NXP_SOUND(kCoin),
        NXP_SOUND(kTrophy),
        NXP_SOUND(kHit),
        NXP_SOUND(kCrit),
        NXP_SOUND(kHeal),
        NXP_SOUND(kFall),
        NXP_SOUND(kWin),
        NXP_SOUND(kLose),
        NXP_SOUND(kTick),
    };
#undef NXP_SOUND

    static_assert(sizeof(kSounds) / sizeof(kSounds[0]) == size_t(Sfx::Count),
        "every Sfx needs a recipe, in the order the enum lists them");

    // ---------------------------------------------------------------- level

    // Everything is mixed at full float and clipped once at the end, so the
    // level lives here. It is set from what the sounds actually measure rather
    // than by ear, since one of the two is available to somebody editing a
    // table of numbers - tools/sfx_preview.py renders the lot and prints this:
    //
    //     move 0.27   select 0.43   back 0.42   toast 0.47   coin 0.37
    //     trophy 0.51   hit 0.77   crit 0.78   heal 0.34   fall 0.66
    //     win 0.50   lose 0.51   tick 0.30
    //
    // Ticks a third of full scale, things that happened about half, a blow
    // three quarters, and nothing near the clamp. The volume anybody actually
    // uses is the slider on the console.
    constexpr float kMaster = 2.8f;

    // How much of the room comes back. Enough to hear, not enough to smear
    // the next thing that happens.
    constexpr float kRoomSend = 0.26f;
    constexpr float kRoomFeed = 0.30f;
    constexpr float kRoomDamp = 0.38f;

    // ------------------------------------------------------------ the tables

    constexpr size_t kTableSize = 1024;
    float g_sine[kTableSize];
    bool g_tableReady = false;

    void buildTable()
    {
        if (g_tableReady)
            return;
        for (size_t i = 0; i < kTableSize; i++)
            g_sine[i] = std::sin(6.2831853f * float(i) / float(kTableSize));
        g_tableReady = true;
    }

    inline float sine(float phase)
    {
        return g_sine[size_t(phase * float(kTableSize)) & (kTableSize - 1)];
    }

    // The 0x1000 alignment is the device's, not ours: `audout` refuses a
    // buffer that is not page aligned in both address and size.
    alignas(0x1000) int16_t g_pcm[3][0x1000 / sizeof(int16_t)];

    // A sixth of a second a side at 48 kHz, which is all a room this small
    // needs. Floats rather than samples, because it is fed back into itself
    // and sixteen bits of rounding per pass turns into grit.
    constexpr size_t kRoomMax = 8192;
    float g_roomL[kRoomMax];
    float g_roomR[kRoomMax];
}

Audio& Audio::get()
{
    static Audio instance;
    return instance;
}

bool Audio::init()
{
    if (m_started)
        return true;

    static_assert(sizeof(g_pcm) / sizeof(g_pcm[0]) == kBuffers, "buffers disagree");
    static_assert(sizeof(g_pcm[0]) == kBufferBytes, "buffer size disagrees");

    buildTable();

    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        LOG("audio: no device (0x%x); the app runs silent", rc);
        return false;
    }

    m_rate = audoutGetSampleRate();
    m_channels = audoutGetChannelCount();
    // Everything here is written for signed 16-bit frames. Another format is
    // not a crash, it is silence: the console gets to keep working.
    if (audoutGetPcmFormat() != PcmFormat_Int16 || m_channels < 1 || m_channels > 2
        || m_rate < 8000) {
        LOG("audio: device is %u Hz, %u channels, format %d - not one we can fill",
            m_rate, m_channels, int(audoutGetPcmFormat()));
        audoutExit();
        return false;
    }
    m_invRate = 1.0f / float(m_rate);
    m_frames = kBufferBytes / (m_channels * sizeof(int16_t));

    // 71 and 97 milliseconds: unrelated lengths, neither a multiple of the
    // other, so the two sides never line up into a flutter.
    m_roomL = std::min(kRoomMax - 1, size_t(0.071f * float(m_rate)));
    m_roomR = std::min(kRoomMax - 1, size_t(0.097f * float(m_rate)));
    m_roomAt = 0;
    memset(g_roomL, 0, sizeof(g_roomL));
    memset(g_roomR, 0, sizeof(g_roomR));

    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        LOG("audio: startAudioOut failed (0x%x)", rc);
        audoutExit();
        return false;
    }

    // Primed with silence, all of them, so the device has something to play
    // while the thread is still starting.
    for (size_t i = 0; i < kBuffers; i++) {
        memset(g_pcm[i], 0, kBufferBytes);
        m_slots[i].next = nullptr;
        m_slots[i].buffer = g_pcm[i];
        m_slots[i].buffer_size = kBufferBytes;
        m_slots[i].data_size = m_frames * m_channels * sizeof(int16_t);
        m_slots[i].data_offset = 0;
        audoutAppendAudioOutBuffer(&m_slots[i]);
    }

    m_running = true;
    // 32 KiB is generous for a loop that does float arithmetic into a static
    // buffer, and the priority is the one the sync worker runs at: this is a
    // periodic job with sixty milliseconds of slack, not a real-time one.
    rc = threadCreate(&m_thread, threadEntry, this, nullptr, 32 * 1024, 0x2C, -2);
    if (R_FAILED(rc)) {
        LOG("audio: threadCreate failed (0x%x)", rc);
        m_running = false;
        audoutStopAudioOut();
        audoutExit();
        return false;
    }
    rc = threadStart(&m_thread);
    if (R_FAILED(rc)) {
        LOG("audio: threadStart failed (0x%x)", rc);
        threadClose(&m_thread);
        m_running = false;
        audoutStopAudioOut();
        audoutExit();
        return false;
    }

    m_started = true;
    LOG("audio: %u Hz, %u channels, %zu frames a buffer", m_rate, m_channels, m_frames);
    return true;
}

void Audio::exit()
{
    if (!m_started)
        return;

    // The thread waits on the device with a timeout rather than for ever, so
    // clearing this is all it takes to bring it home.
    m_running = false;
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);

    audoutStopAudioOut();
    audoutExit();
    m_started = false;
}

void Audio::threadEntry(void* self)
{
    static_cast<Audio*>(self)->run();
}

void Audio::run()
{
    while (m_running) {
        AudioOutBuffer* released = nullptr;
        u32 count = 0;
        // A tenth of a second, so a shutdown is never waiting on the device
        // to want another buffer.
        Result rc = audoutWaitPlayFinish(&released, &count, 100000000ULL);
        if (R_FAILED(rc) || released == nullptr)
            continue;

        fill(static_cast<int16_t*>(released->buffer), m_frames);
        released->data_size = m_frames * m_channels * sizeof(int16_t);
        released->data_offset = 0;
        audoutAppendAudioOutBuffer(released);
    }
}

void Audio::play(Sfx which)
{
    if (!m_started || !m_enabled || m_muted)
        return;
    if (which >= Sfx::Count)
        return;

    std::lock_guard<std::mutex> lock(m_lock);
    // A full list drops the sound rather than waiting. Sixteen queued inside
    // twenty milliseconds is already more than anything can be heard through,
    // and the one thing this must never do is hold up whoever is drawing.
    if (m_pendingCount >= kPending)
        return;
    m_pending[m_pendingCount++] = uint8_t(which);
}

void Audio::setMuted(bool muted)
{
    m_muted = muted;
    if (muted)
        silence();
}

void Audio::setEnabled(bool on)
{
    m_enabled = on;
    if (!on)
        silence();
}

void Audio::silence()
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_pendingCount = 0;
    // Only a flag: the mixer owns the voices and the room, and it reads this
    // before it touches either.
    m_stop = true;
}

void Audio::spawn(Sfx which)
{
    const Recipe& recipe = kSounds[size_t(which)];
    for (size_t n = 0; n < recipe.count; n++) {
        const Note& note = recipe.notes[n];
        const Timbre& timbre = kTimbres[note.timbre < Timbre_Count ? note.timbre : 0];

        // The oldest voice loses, which is right for a sound bank this small:
        // the newest thing that happened is the thing somebody is looking at.
        Voice* slot = nullptr;
        for (Voice& v : m_voices) {
            if (!v.live) {
                slot = &v;
                break;
            }
            if (!slot || v.at > slot->at)
                slot = &v;
        }
        if (!slot)
            return;

        int32_t len = std::max(2, int32_t(note.decay * float(m_rate)));
        slot->live = true;
        slot->wait = int32_t(note.delay * float(m_rate));
        slot->at = 0;
        slot->len = len;
        // Two to twelve milliseconds by timbre, and never more than a third
        // of the note. Without it every one of these starts with a click,
        // which is louder than the note.
        slot->attack
            = std::max(1, std::min(int32_t(timbre.attack * float(m_rate)), len / 3));
        slot->from = note.pitch;
        slot->to = note.bend > 0.0f ? note.bend : note.pitch;
        slot->gain = note.gain;
        slot->phase = 0.0f;
        // Struck, not held: the envelope is one multiplication per frame that
        // reaches a thousandth of where it started after `len` of them, which
        // is what a ringing object actually does. Working the factor out here
        // is also what keeps the mixer down to that one multiply.
        slot->decay = std::exp(-6.9f / float(len));
        slot->env = 1.0f;
        slot->lp = 0.0f;
        slot->seed = 0x9E3779B9u + uint32_t(slot - m_voices) * 2654435761u;
        slot->timbre = note.timbre;
    }
}

void Audio::fill(int16_t* out, size_t frames)
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_stop) {
            for (Voice& v : m_voices)
                v.live = false;
            memset(g_roomL, 0, sizeof(g_roomL));
            memset(g_roomR, 0, sizeof(g_roomR));
            m_dampL = 0.0f;
            m_dampR = 0.0f;
            m_stop = false;
        }
        for (size_t i = 0; i < m_pendingCount; i++)
            spawn(static_cast<Sfx>(m_pending[i]));
        m_pendingCount = 0;
    }

    for (size_t f = 0; f < frames; f++) {
        float dry = 0.0f;

        for (Voice& v : m_voices) {
            if (!v.live)
                continue;
            if (v.wait > 0) {
                v.wait--;
                continue;
            }
            if (v.at >= v.len) {
                v.live = false;
                continue;
            }

            const Timbre& timbre = kTimbres[v.timbre < Timbre_Count ? v.timbre : 0];
            float shape = 0.0f;

            if (v.timbre == Timbre_Air) {
                v.seed = v.seed * 1664525u + 1013904223u;
                float white = float((v.seed >> 9) & 0xFFFFu) / 32768.0f - 1.0f;
                // Air rather than hiss: the same one pole the other timbres
                // use, run much harder.
                v.lp += (white - v.lp) * timbre.tilt;
                shape = v.lp;
            } else {
                // Exponential rather than linear, so a falling pitch sounds
                // like something losing energy instead of a siren winding
                // down.
                float u = float(v.at) / float(v.len);
                float freq = v.from == v.to ? v.from
                                            : v.from * std::pow(v.to / v.from, u);
                v.phase += freq * m_invRate;
                if (v.phase >= 1.0f)
                    v.phase -= std::floor(v.phase);

                float sum = 0.0f;
                for (int p = 0; p < timbre.count; p++)
                    sum += sine(v.phase * timbre.part[p].mul) * timbre.part[p].gain;
                // One pole of roll-off, which takes the glare off the top
                // partials without needing them to be absent.
                v.lp += (sum - v.lp) * timbre.tilt;
                shape = v.lp;
            }

            float rise = std::min(1.0f, float(v.at) / float(v.attack));
            dry += shape * v.env * rise * v.gain;
            v.env *= v.decay;
            v.at++;
        }

        dry *= kMaster;

        // The room. Read both taps, damp what comes back, and cross-feed it,
        // so the tail wanders from one side to the other rather than sitting
        // in the middle of somebody's head.
        size_t readL = (m_roomAt + kRoomMax - m_roomL) % kRoomMax;
        size_t readR = (m_roomAt + kRoomMax - m_roomR) % kRoomMax;
        float tailL = g_roomL[readL];
        float tailR = g_roomR[readR];
        m_dampL += (tailL - m_dampL) * kRoomDamp;
        m_dampR += (tailR - m_dampR) * kRoomDamp;
        g_roomL[m_roomAt] = dry * kRoomSend + m_dampR * kRoomFeed;
        g_roomR[m_roomAt] = dry * kRoomSend + m_dampL * kRoomFeed;
        m_roomAt = (m_roomAt + 1) % kRoomMax;

        float left = std::min(1.0f, std::max(-1.0f, dry + tailL));
        float right = std::min(1.0f, std::max(-1.0f, dry + tailR));
        if (m_channels >= 2) {
            out[f * m_channels] = int16_t(left * 32000.0f);
            out[f * m_channels + 1] = int16_t(right * 32000.0f);
        } else {
            out[f] = int16_t((left + right) * 0.5f * 32000.0f);
        }
    }
}

} // namespace nxp
