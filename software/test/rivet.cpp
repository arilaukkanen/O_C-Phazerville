#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

long random(long bound) {
    static uint32_t state = 0x23456789;
    state = state * 1664525 + 1013904223;
    return (state >> 16) % bound;
}

template<typename T> T constrain(T value, T lo, T hi) {
    return value < lo ? lo : value > hi ? hi : value;
}
struct PackLocation { int offset, width; };
void Pack(uint64_t &data, PackLocation p, int value) {
    data |= (uint64_t(value) & ((uint64_t(1) << p.width) - 1)) << p.offset;
}
int Unpack(uint64_t data, PackLocation p) {
    return (data >> p.offset) & ((uint64_t(1) << p.width) - 1);
}
enum { HELP_DIGITAL1, HELP_DIGITAL2, HELP_CV1, HELP_CV2,
       HELP_OUT1, HELP_OUT2, HELP_EXTRA1, HELP_EXTRA2 };
class HemisphereApplet {
public:
    virtual ~HemisphereApplet() = default;
    virtual void Unload() {}
    bool clocks[2] = {};
    int inputs[2] = {}, outputs[2] = {};
    const char *help[8] = {};
    bool editing = false;
    bool Clock(int ch) { bool c = clocks[ch]; clocks[ch] = false; return c; }
    int In(int ch) { return inputs[ch]; }
    void Out(int ch, int cv) { outputs[ch] = cv; }
    bool EditMode() { return editing; }
    void CursorToggle() { editing = !editing; }
    template<typename T> void MoveCursor(T &c, int d, int max) {
        c = constrain(c + d, 0, max);
    }
    void gfxPrint(int, int, const char *) {}
    void gfxPrint(int, int, int) {}
    void gfxCursor(int, int, int, int) {}
};

#include "../src/applets/Rivet.h"

static void bounds_and_decay() {
    for (int level = 0; level <= 63; level += 63) {
        RivetDSP dsp;
        dsp.settings = {(uint8_t)level,(uint8_t)level,(uint8_t)level,
                        (uint8_t)level,(uint8_t)level,(uint8_t)level,(uint8_t)level,
                        (uint8_t)level};
        for (int cv : {-6144, 0, 6144}) {
            dsp.SetCV(cv, cv);
            dsp.UpdateControls();
            dsp.Trigger(true);
            bool heard = false;
            for (int i = 0; i < 2 * (int)RivetDSP::kRate; ++i) {
                int env = -1;
                int sample = dsp.Render(env);
                assert(sample >= -3072 && sample <= 3072);
                assert(env >= 0 && env <= 7680);
                heard |= sample != 0;
            }
            assert(heard);
            assert(!dsp.Active());
        }
    }
}

static void pitch_tracking() {
    RivetDSP dsp;
    dsp.SetCV(0, 0); dsp.UpdateControls();
    uint32_t root = dsp.BaseIncrement();
    dsp.SetCV(1536, 0); dsp.UpdateControls();
    uint32_t octave = dsp.BaseIncrement();
    assert(octave > 2 * root - root / 1000);
    assert(octave < 2 * root + root / 1000);
}

static void low_pass_range() {
    uint64_t energy[2] = {};
    for (int mode = 0; mode < 2; ++mode) {
        RivetDSP dsp;
        dsp.settings = {54, 40, 50, 0, 0, 0, 0,
                        (uint8_t)(mode ? 63 : 0)};
        dsp.UpdateControls();
        dsp.Trigger(false);
        for (int i = 0; i < 5000; ++i) {
            int env;
            int32_t sample = dsp.Render(env);
            energy[mode] += (int64_t)sample * sample;
        }
    }
    assert(energy[1] > energy[0] * 2);
}

static void retrigger_and_persistence() {
    Rivet applet;
    applet.Start();
    applet.editing = true;
    applet.OnEncoderMove(11);
    applet.editing = false;
    applet.OnEncoderMove(3);
    applet.editing = true;
    applet.OnEncoderMove(22);
    auto packed = applet.OnDataRequest();
    Rivet restored;
    restored.Start();
    restored.OnDataReceive(packed);
    assert(restored.OnDataRequest() == packed);

    applet.clocks[0] = applet.clocks[1] = true; // accent takes precedence
    applet.Controller();
    assert(!applet.clocks[0] && !applet.clocks[1]);
    for (int i = 0; i < 100; ++i) applet.Controller();
    assert(applet.outputs[1] > 0);
    int before = applet.outputs[0];
    applet.clocks[0] = true; // retrigger mid-decay fades out before restart
    applet.Controller();
    assert(abs(applet.outputs[0]) <= abs(before));
    for (int i = 0; i < 20; ++i) applet.Controller();
    assert(applet.outputs[1] > 0);
    applet.Unload();
    assert(applet.OnDataRequest() == packed);
    applet.Controller();
    assert(applet.outputs[0] == 0 && applet.outputs[1] == 0);
    assert(restored.OnDataRequest() == packed);
}

static void randomize_control() {
    Rivet applet;
    applet.Start();
    applet.OnButtonPress();
    assert(applet.editing);
    applet.OnButtonPress();
    assert(!applet.editing);
    applet.OnEncoderMove(7); // LPF
    applet.OnButtonPress();
    applet.OnEncoderMove(27); // LPF 20 -> 47
    applet.OnButtonPress();
    const uint64_t original = applet.OnDataRequest();
    assert(Unpack(original, PackLocation{42, 6}) == 47);
    applet.OnEncoderMove(1); // RANDOM follows LPF
    applet.View();
    applet.OnButtonPress();
    assert(!applet.editing);
    const uint64_t randomized = applet.OnDataRequest();
    assert(randomized != original);
    assert(Unpack(randomized, PackLocation{42, 6}) == 47);
    for (int i = 0; i < 8; ++i)
        assert(Unpack(randomized, PackLocation{i * 6, 6}) <= 63);
    Rivet restored;
    restored.Start();
    restored.OnDataReceive(randomized);
    assert(restored.OnDataRequest() == randomized);
    restored.OnDataReceive(randomized & ((uint64_t(1) << 42) - 1));
    assert(Unpack(restored.OnDataRequest(), PackLocation{42, 6}) == 20);
    assert(Unpack(restored.OnDataRequest(), PackLocation{63, 1}) == 1);
    applet.OnButtonPress();
    assert(applet.OnDataRequest() != randomized);
    assert(Unpack(applet.OnDataRequest(), PackLocation{42, 6}) == 47);
}

int main() {
    bounds_and_decay();
    pitch_tracking();
    low_pass_range();
    retrigger_and_persistence();
    randomize_control();
    std::puts("Rivet DSP and applet tests passed");
}
