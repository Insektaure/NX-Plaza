#include "platform/audio.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nxp {

namespace {
    enum Wave : uint8_t {
        Wave_Sine = 0,
        Wave_Triangle,
        Wave_Square,
        Wave_Noise,
    };

    // One note of one sound. Times are seconds, pitches are hertz, and `to`
    // of zero means "hold `from`" rather than "slide to silence" - every
    // sound here either holds a pitch or bends it, and none of them bends to
    // nothing.
    struct Note {
        float delay;
        float length;
        float from;
        float to;
        float gain;
        uint8_t wave;
    };

    // The sounds. Pitches are the C major pentatonic an octave or two above
    // middle C, so two of them landing at once - which happens constantly,
    // since a floor can clear while a coin is still sounding - is a chord
    // rather than a clash.
    const Note kMove[] = { { 0.000f, 0.030f, 1318.5f, 0.0f, 0.22f, Wave_Sine } };
    const Note kSelect[] = { { 0.000f, 0.070f, 880.0f, 1318.5f, 0.40f, Wave_Sine } };
    const Note kBack[] = { { 0.000f, 0.080f, 659.3f, 440.0f, 0.32f, Wave_Sine } };
    const Note kToast[] = {
        { 0.000f, 0.110f, 784.0f, 0.0f, 0.34f, Wave_Sine },
        { 0.090f, 0.190f, 1046.5f, 0.0f, 0.34f, Wave_Sine },
    };
    const Note kCoin[] = {
        { 0.000f, 0.045f, 1568.0f, 0.0f, 0.32f, Wave_Sine },
        { 0.040f, 0.110f, 2093.0f, 0.0f, 0.30f, Wave_Sine },
    };
    const Note kTrophy[] = {
        { 0.000f, 0.130f, 523.3f, 0.0f, 0.30f, Wave_Sine },
        { 0.100f, 0.130f, 659.3f, 0.0f, 0.30f, Wave_Sine },
        { 0.200f, 0.130f, 784.0f, 0.0f, 0.30f, Wave_Sine },
        { 0.300f, 0.320f, 1046.5f, 0.0f, 0.32f, Wave_Sine },
    };
    // A blow is a knock and a thump together: the noise is the contact and
    // the falling square is the weight behind it.
    const Note kHit[] = {
        { 0.000f, 0.050f, 0.0f, 0.0f, 0.30f, Wave_Noise },
        { 0.000f, 0.090f, 200.0f, 90.0f, 0.30f, Wave_Square },
    };
    // The three of these overlap, so their gains are lower than the other
    // sounds' on purpose: summed they come to 0.76, which is where the
    // loudest thing in the app belongs rather than through the clamp.
    const Note kCrit[] = {
        { 0.000f, 0.060f, 0.0f, 0.0f, 0.30f, Wave_Noise },
        { 0.000f, 0.160f, 300.0f, 110.0f, 0.28f, Wave_Square },
        { 0.010f, 0.070f, 1568.0f, 1046.5f, 0.18f, Wave_Sine },
    };
    const Note kHeal[] = {
        { 0.000f, 0.240f, 880.0f, 1318.5f, 0.24f, Wave_Sine },
        { 0.080f, 0.220f, 1046.5f, 1568.0f, 0.16f, Wave_Sine },
    };
    const Note kFall[] = { { 0.000f, 0.340f, 440.0f, 110.0f, 0.32f, Wave_Triangle } };
    const Note kWin[] = {
        { 0.000f, 0.110f, 523.3f, 0.0f, 0.28f, Wave_Sine },
        { 0.090f, 0.110f, 659.3f, 0.0f, 0.28f, Wave_Sine },
        { 0.180f, 0.110f, 784.0f, 0.0f, 0.28f, Wave_Sine },
        { 0.270f, 0.300f, 1046.5f, 0.0f, 0.30f, Wave_Sine },
    };
    const Note kLose[] = {
        { 0.000f, 0.220f, 392.0f, 329.6f, 0.28f, Wave_Triangle },
        { 0.180f, 0.400f, 261.6f, 196.0f, 0.28f, Wave_Triangle },
    };
    const Note kTick[] = { { 0.000f, 0.022f, 1046.5f, 0.0f, 0.20f, Wave_Square } };

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

    // Everything is mixed at full float and clipped once at the end, so the
    // level lives here. One note lands around a third of full scale, which is
    // audible without being the loudest thing the console does; the two or
    // three that overlap in a blow or a fanfare come out near the top, and
    // the clamp is what stops them going over. The volume anybody actually
    // uses is the slider on the console.
    constexpr float kMaster = 1.15f;

    // A quarter of a sine, mirrored twice. 1024 points is inaudibly close to
    // the real thing at these pitches and costs 4 KiB.
    constexpr size_t kTableSize = 1024;
    float g_sine[kTableSize];
    bool g_tableReady = false;

    void buildTable()
    {
        if (g_tableReady)
            return;
        for (size_t i = 0; i < kTableSize; i++) {
            g_sine[i] = std::sin(6.2831853f * float(i) / float(kTableSize));
        }
        g_tableReady = true;
    }

    // The 0x1000 alignment is the device's, not ours: `audout` refuses a
    // buffer that is not page aligned in both address and size.
    alignas(0x1000) int16_t g_pcm[3][0x1000 / sizeof(int16_t)];
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
    // periodic job with forty milliseconds of slack, not a real-time one.
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
    // A full list drops the sound rather than waiting. Sixteen queued between
    // two buffers is already more than anything can be heard through, and the
    // one thing this must never do is hold up whoever is drawing.
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
    // Only a flag: the mixer owns the voices, and it reads this before it
    // touches them.
    m_stop = true;
}

void Audio::spawn(Sfx which)
{
    const Recipe& recipe = kSounds[size_t(which)];
    for (size_t n = 0; n < recipe.count; n++) {
        const Note& note = recipe.notes[n];

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

        int32_t len = std::max(1, int32_t(note.length * float(m_rate)));
        slot->live = true;
        slot->wait = int32_t(note.delay * float(m_rate));
        slot->at = 0;
        slot->len = len;
        // Six milliseconds on and forty off, or a third and a half of a very
        // short note, whichever is less. Without the ramps every one of these
        // starts and ends with a click, which is louder than the note.
        slot->attack = std::max(1, std::min(int32_t(0.006f * float(m_rate)), len / 3));
        slot->release = std::max(1, std::min(int32_t(0.040f * float(m_rate)), len / 2));
        slot->from = note.from;
        slot->to = note.to > 0.0f ? note.to : note.from;
        slot->gain = note.gain;
        slot->phase = 0.0f;
        slot->seed = 0x9E3779B9u + uint32_t(slot - m_voices) * 2654435761u;
        slot->wave = note.wave;
    }
}

void Audio::fill(int16_t* out, size_t frames)
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_stop) {
            for (Voice& v : m_voices)
                v.live = false;
            m_stop = false;
        }
        for (size_t i = 0; i < m_pendingCount; i++)
            spawn(static_cast<Sfx>(m_pending[i]));
        m_pendingCount = 0;
    }

    for (size_t f = 0; f < frames; f++) {
        float mix = 0.0f;
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

            float t = float(v.at) / float(v.len);
            float freq = v.from + (v.to - v.from) * t;
            v.phase += freq * m_invRate;
            if (v.phase >= 1.0f)
                v.phase -= std::floor(v.phase);

            float shape = 0.0f;
            switch (v.wave) {
            case Wave_Triangle:
                shape = 4.0f * std::fabs(v.phase - 0.5f) - 1.0f;
                break;
            case Wave_Square:
                // Two thirds, because a square at the same nominal gain as a
                // sine is about half again as loud and twice as rude.
                shape = (v.phase < 0.5f ? 1.0f : -1.0f) * 0.66f;
                break;
            case Wave_Noise:
                v.seed = v.seed * 1664525u + 1013904223u;
                shape = float((v.seed >> 9) & 0xFFFFu) / 32768.0f - 1.0f;
                break;
            default:
                shape = g_sine[size_t(v.phase * float(kTableSize)) & (kTableSize - 1)];
                break;
            }

            float rise = std::min(1.0f, float(v.at) / float(v.attack));
            float fall = std::min(1.0f, float(v.len - v.at) / float(v.release));
            mix += shape * rise * fall * v.gain;
            v.at++;
        }

        mix *= kMaster;
        mix = std::min(1.0f, std::max(-1.0f, mix));
        int16_t sample = int16_t(mix * 32000.0f);
        for (uint32_t c = 0; c < m_channels; c++)
            out[f * m_channels + c] = sample;
    }
}

} // namespace nxp
