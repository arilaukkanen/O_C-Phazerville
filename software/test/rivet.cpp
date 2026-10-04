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

    dsp.SetModes(RivetDSP::CV_OFF, RivetDSP::CV_PITCH);
    dsp.SetCV(0, 0); dsp.UpdateControls();
    root = dsp.BaseIncrement();
    dsp.SetCV(0, 1536); dsp.UpdateControls();
    octave = dsp.BaseIncrement();
    assert(octave > 2 * root - root / 1000);
    assert(octave < 2 * root + root / 1000);

    dsp.SetModes(RivetDSP::CV_PITCH, RivetDSP::CV_PITCH);
    dsp.SetCV(768, 768); dsp.UpdateControls();
    assert(dsp.BaseIncrement() > 2 * root - root / 1000);
}

static void cv_assignments() {
    for (int target = RivetDSP::CV_OFF;
         target < RivetDSP::CV_TARGET_COUNT; ++target) {
        RivetDSP plain, modulated;
        plain.SetModes(RivetDSP::CV_OFF, target);
        modulated.SetModes(RivetDSP::CV_OFF, target);
        plain.SetCV(0, 0);
        modulated.SetCV(0, 3072);
        plain.UpdateControls();
        modulated.UpdateControls();
        plain.Trigger(false);
        modulated.Trigger(false);
        uint64_t difference = 0;
        for (int i = 0; i < 3000; ++i) {
            int env1, env2;
            int a = plain.Render(env1);
            int b = modulated.Render(env2);
            difference += abs(a - b) + abs(env1 - env2);
        }
        if (target == RivetDSP::CV_OFF) assert(difference == 0);
        else assert(difference > 0);
    }
}

static void paired_cv_assignments() {
    struct Pair { int combo, first, second; };
    const Pair pairs[] = {
        {RivetDSP::CV_METAL_FOLD, RivetDSP::CV_METAL, RivetDSP::CV_FOLD},
        {RivetDSP::CV_FOLD_NOISE, RivetDSP::CV_FOLD, RivetDSP::CV_NOISE},
        {RivetDSP::CV_NOISE_DECAY, RivetDSP::CV_NOISE, RivetDSP::CV_DECAY},
        {RivetDSP::CV_DECAY_SWEEP, RivetDSP::CV_DECAY, RivetDSP::CV_SWEEP},
        {RivetDSP::CV_SPREAD_METAL, RivetDSP::CV_SPREAD, RivetDSP::CV_METAL},
        {RivetDSP::CV_FOLD_LPF, RivetDSP::CV_FOLD, RivetDSP::CV_LPF},
    };
    for (const Pair &pair : pairs) {
        for (int cv : {-1536, 3072}) {
            RivetDSP combined, independent;
            combined.SetModes(pair.combo, RivetDSP::CV_OFF);
            independent.SetModes(pair.first, pair.second);
            combined.SetCV(cv, 0);
            independent.SetCV(cv, cv);
            combined.UpdateControls();
            independent.UpdateControls();
            combined.Trigger(false);
            independent.Trigger(false);
            for (int i = 0; i < 4000; ++i) {
                int combined_env, independent_env;
                assert(combined.Render(combined_env) == independent.Render(independent_env));
                assert(combined_env == independent_env);
            }
        }
    }
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
    applet.OnEncoderMove(3); // RANDOM follows LPF, CV1 and CV2
    applet.View();
    applet.OnButtonPress();
    assert(!applet.editing);
    const uint64_t randomized = applet.OnDataRequest();
    assert(randomized != original);
    assert(Unpack(randomized, PackLocation{42, 6}) == 47);
    assert(Unpack(randomized, PackLocation{48, 4}) == RivetDSP::CV_PITCH);
    assert(Unpack(randomized, PackLocation{52, 4}) == RivetDSP::CV_TIMBRE);
    for (int i = 0; i < 8; ++i)
        assert(Unpack(randomized, PackLocation{i * 6, 6}) <= 63);
    Rivet restored;
    restored.Start();
    restored.OnDataReceive(randomized);
    assert(restored.OnDataRequest() == randomized);
    restored.OnDataReceive(randomized & ((uint64_t(1) << 42) - 1));
    assert(Unpack(restored.OnDataRequest(), PackLocation{42, 6}) == 20);
    assert(Unpack(restored.OnDataRequest(), PackLocation{48, 4}) == RivetDSP::CV_PITCH);
    assert(Unpack(restored.OnDataRequest(), PackLocation{52, 4}) == RivetDSP::CV_TIMBRE);
    assert(Unpack(restored.OnDataRequest(), PackLocation{62, 1}) == 1);
    assert(Unpack(restored.OnDataRequest(), PackLocation{63, 1}) == 1);
    applet.OnButtonPress();
    assert(applet.OnDataRequest() != randomized);
    assert(Unpack(applet.OnDataRequest(), PackLocation{42, 6}) == 47);
}

static void cv_mode_persistence() {
    Rivet applet;
    applet.Start();
    applet.OnEncoderMove(8); // CV1
    applet.OnButtonPress();
    applet.OnEncoderMove(3); // Pitch -> Fold
    applet.OnButtonPress();
    applet.OnEncoderMove(1); // CV2
    applet.OnButtonPress();
    applet.OnEncoderMove(4); // Timbre -> Decay
    applet.OnButtonPress();
    const uint64_t saved = applet.OnDataRequest();
    assert(Unpack(saved, PackLocation{48, 4}) == RivetDSP::CV_FOLD);
    assert(Unpack(saved, PackLocation{52, 4}) == RivetDSP::CV_DECAY);
    Rivet restored;
    restored.Start();
    restored.OnDataReceive(saved);
    assert(restored.OnDataRequest() == saved);
    applet.OnEncoderMove(1); // RANDOM after CV2
    applet.OnButtonPress();
    assert(Unpack(applet.OnDataRequest(), PackLocation{48, 4}) == RivetDSP::CV_FOLD);
    assert(Unpack(applet.OnDataRequest(), PackLocation{52, 4}) == RivetDSP::CV_DECAY);
    // Prior preset format has no assignment marker and restores defaults.
    uint64_t old_preset = saved & ~(uint64_t(1) << 62);
    old_preset = (old_preset & ~(uint64_t(63) << 42)) | (uint64_t(47) << 42);
    restored.OnDataReceive(old_preset);
    assert(Unpack(restored.OnDataRequest(), PackLocation{48, 4}) == RivetDSP::CV_PITCH);
    assert(Unpack(restored.OnDataRequest(), PackLocation{52, 4}) == RivetDSP::CV_TIMBRE);
    assert(Unpack(restored.OnDataRequest(), PackLocation{42, 6}) == 47);

    Rivet paired;
    paired.Start();
    paired.OnEncoderMove(8); // CV1
    paired.OnButtonPress();
    paired.OnEncoderMove(14); // Pitch -> F+LPF, highest 4-bit mode
    paired.OnButtonPress();
    const uint64_t paired_saved = paired.OnDataRequest();
    assert(Unpack(paired_saved, PackLocation{48, 4}) == RivetDSP::CV_FOLD_LPF);
    restored.OnDataReceive(paired_saved);
    assert(restored.OnDataRequest() == paired_saved);
}

int main() {
    bounds_and_decay();
    pitch_tracking();
    cv_assignments();
    paired_cv_assignments();
    low_pass_range();
    retrigger_and_persistence();
    randomize_control();
    cv_mode_persistence();
    std::puts("Rivet DSP and applet tests passed");
}
