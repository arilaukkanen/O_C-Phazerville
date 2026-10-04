#include <cassert>
#include <cstdint>
#include <cstdlib>

constexpr int HEMISPHERE_MAX_INPUT_CV = 7680;
constexpr bool NorthernLightModular = false;
constexpr int DOWN_BTN_ICON = 0;
constexpr int SCALE_ICON = 0;
#define ForEachChannel(ch) for (int ch = 0; ch < 2; ++ch)

template <typename T> T constrain(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}
int Proportion(int value, int total, int portion) { return value * portion / total; }
void randomSeed(unsigned long) {}
unsigned long micros() { return 1; }
void noInterrupts() {}
void interrupts() {}
long random(long low, long high) {
    if (low < 0 && high == 1 - low) {
        static bool alternate = false;
        alternate = !alternate;
        return alternate ? low : high - 1;
    }
    return low + (high - low) / 2;
}

struct PackLocation { int offset, width; };
void Pack(uint64_t &data, PackLocation p, int value) {
    data |= (uint64_t(value) & ((uint64_t(1) << p.width) - 1)) << p.offset;
}
int Unpack(uint64_t data, PackLocation p) {
    return (data >> p.offset) & ((uint64_t(1) << p.width) - 1);
}

namespace OC {
namespace CORE { uint32_t ticks = 100000; }
enum { CONTROL_BUTTON_A = 1, CONTROL_BUTTON_B = 2 };
struct FakeUi {
    int buttons = 0;
    bool screensaver_preempted = false;
    bool read_immediate(int button) { return buttons & button; }
    void _preemptScreensaver(bool value) { screensaver_preempted = value; }
} ui;
const char *scale_names_short[] = {"TEST"};
}
namespace HS {
constexpr uint32_t HEMISPHERE_DOUBLE_CLICK_TIME = 8000;
void QuantizerEdit(int) {}
}
namespace PhzIcons { const uint8_t shredder[] = {0}; }

enum { HELP_DIGITAL1, HELP_DIGITAL2, HELP_CV1, HELP_CV2,
       HELP_OUT1, HELP_OUT2, HELP_EXTRA1, HELP_EXTRA2 };

class HemisphereApplet {
public:
    int io_offset = 0;
    int outputs[2] = {};
    bool clocks[2] = {};
    int inputs[2] = {};
    bool editing = false;
    const char *help[8] = {};
    bool Clock(int ch) { bool result = clocks[ch]; clocks[ch] = false; return result; }
    int DetentedIn(int ch) { return inputs[ch]; }
    int In(int ch) { return inputs[ch]; }
    int ProportionCV(int value, int limit, int max) { return Proportion(value, max, limit); }
    int Quantize(int, int value) { return value; }
    void Out(int ch, int value) { outputs[ch] = value; }
    bool EditMode() { return editing; }
    void CursorToggle() { editing = !editing; }
    void CancelEdit() { editing = false; }
    template <typename T> void MoveCursor(T &cursor, int direction, int max) {
        cursor = constrain(cursor + direction, 0, max);
    }
    int GetScale(int) { return 0; }
    void SetScale(int, int) {}
    const char *OutputLabel(int ch) { return ch ? "B" : "A"; }
    void gfxPrint(int, int, const char *) {}
    void gfxPrint(int, int, char) {}
    void gfxPrint(const char *) {}
    void gfxPrint(int) {}
    void gfxPos(int, int) {}
    void gfxPrintVoltage(int) {}
    void gfxInvert(int, int, int, int) {}
    void gfxSpicyCursor(int, int, int) {}
    void gfxCursor(int, int, int) {}
    void gfxIcon(int, int, int) {}
    void gfxFrame(int, int, int, int) {}
    void gfxDottedLine(int, int, int, int, int) {}
    void gfxRect(int, int, int, int) {}
};

#define private public
#include "../src/applets/Shredder.h"
#undef private

static void tick(Shredder &s, bool reset = false) {
    s.clocks[0] = true;
    s.clocks[1] = reset;
    s.Controller();
}

int main() {
    Shredder left, right;
    right.io_offset = 2;
    left.Start();
    right.Start();
    left.cursor = Shredder::QUANT_EDIT;
    left.OnEncoderMove(1);
    assert(left.cursor == Shredder::WRITE1);
    left.OnEncoderMove(1);
    assert(left.cursor == Shredder::WRITE2);
    left.OnEncoderMove(1);
    assert(left.cursor == Shredder::UNDO);
    left.OnEncoderMove(1);
    assert(left.cursor == Shredder::RESETSHRED1);
    left.OnEncoderMove(1);
    assert(left.cursor == Shredder::RESETSHRED2);
    left.OnEncoderMove(1);
    assert(left.cursor == Shredder::RESETSHRED2);
    left.range[0] = right.range[0] = 5;
    left.range[1] = right.range[1] = 5;
    for (int i = 0; i < 16; ++i) {
        left.sequence[0][i] = i * 100;
        left.sequence[1][i] = 2000 + i;
        right.sequence[0][i] = 3000 + i;
    }
    left.cursor = Shredder::WRITE1;
    left.OnButtonPress();
    assert(left.recording_channel == 0 && left.held_voltage == 0);
    assert(OC::ui.screensaver_preempted);
    assert(left.sequence[0][0] == 0);
    left.OnEncoderMove(10);
    tick(left); // Armed without the side button: playback only.
    assert(left.sequence[0][0] == 0 && left.held_voltage == 0);
    OC::ui.buttons |= OC::CONTROL_BUTTON_A;
    left.OnEncoderMove(10);
    assert(left.nominal_voltage == 3200 && abs(left.held_voltage - 3200) <= 80);
    assert(left.outputs[0] == left.held_voltage && left.sequence[0][0] == 0);
    tick(left);
    assert(left.sequence[0][1] == left.held_voltage && left.step == 1);
    tick(left);
    assert(left.sequence[0][2] == left.held_voltage && left.sequence[1][2] == 2002);
    OC::ui.buttons &= ~OC::CONTROL_BUTTON_A;
    tick(left); // Releasing the side button pauses writes without disarming.
    assert(left.recording_channel == 0 && left.sequence[0][3] == 300);
    assert(left.outputs[0] == 300);
    OC::ui.buttons |= OC::CONTROL_BUTTON_A;
    left.OnEncoderMove(14);
    assert(left.held_voltage == 7680 && left.outputs[0] == 7680);
    left.OnEncoderMove(1);
    assert(left.held_voltage == 7680);
    for (int i = 0; i < 17; ++i) tick(left);
    assert(left.step == 4 && left.sequence[0][4] == 7680);
    left.OnEncoderMove(-24);
    assert(left.held_voltage == 0);
    left.OnButtonPress();
    assert(left.recording_channel == -1 && !left.editing);
    assert(!OC::ui.screensaver_preempted);
    OC::ui.buttons &= ~OC::CONTROL_BUTTON_A;
    assert(left.outputs[0] == left.sequence[0][left.step]);
    left.cursor = Shredder::UNDO;
    const int playhead = left.step;
    left.OnButtonPress();
    assert(left.step == playhead && left.sequence[0][1] == 100);
    assert(left.sequence[1][1] == 2001);
    left.OnEncoderMove(-1); // Navigation alone cannot change the restored loop.
    assert(left.sequence[0][1] == 100);

    left.cursor = Shredder::WRITE1;
    left.OnButtonPress();
    left.OnEncoderMove(3);
    assert(left.held_voltage == left.sequence[0][left.step]); // Button up: no voltage edit.
    OC::ui.buttons |= OC::CONTROL_BUTTON_A;
    left.OnEncoderMove(3);
    assert(left.sequence[0][left.step] != left.held_voltage); // No clock, only a preview.
    left.inputs[0] = HEMISPHERE_MAX_INPUT_CV;
    left.inputs[1] = HEMISPHERE_MAX_INPUT_CV;
    tick(left);
    assert(left.step == 15 && left.sequence[0][15] == left.held_voltage);
    left.Unload();
    assert(left.recording_channel == -1 && !left.editing);
    OC::ui.buttons &= ~OC::CONTROL_BUTTON_A;
    left.inputs[0] = left.inputs[1] = 0;

    right.cursor = Shredder::WRITE1;
    right.OnButtonPress();
    OC::ui.buttons |= OC::CONTROL_BUTTON_B;
    right.OnEncoderMove(5);
    tick(right);
    assert(right.recording_channel == 0 && left.recording_channel == -1);
    assert(abs(right.sequence[0][0] - (3000 + 5 * 320)) <= 80);
    right.OnEncoderMove(1);
    tick(right, true);
    assert(right.recording_channel == -1 && right.step == 0);
    assert(abs(right.sequence[0][0] - (3000 + 5 * 320)) <= 80);
    assert(right.sequence[0][1] == 3001); // Coincident reset never writes.

    right.shred_on_reset[0] = true;
    right.cursor = Shredder::WRITE1;
    right.OnButtonPress();
    tick(right, true);
    assert(right.recording_channel == -1 && !right.undo_valid);
    right.shred_on_reset[0] = false;

    right.cursor = Shredder::WRITE2;
    right.OnButtonPress();
    right.OnUIExit();
    assert(right.recording_channel == -1);
    right.OnDataReceive(right.OnDataRequest());
    assert(!right.undo_valid && right.recording_channel == -1);
    OC::ui.buttons &= ~OC::CONTROL_BUTTON_B;

    left.cursor = Shredder::WRITE1;
    right.cursor = Shredder::WRITE2;
    left.OnButtonPress();
    right.OnButtonPress();
    OC::ui.buttons |= OC::CONTROL_BUTTON_A | OC::CONTROL_BUTTON_B;

    left.OnEncoderMove(2);
    right.OnEncoderMove(4);
    tick(left);
    tick(right);
    assert(left.recording_channel == 0 && right.recording_channel == 1);
    assert(left.sequence[0][left.step] != right.sequence[1][right.step]);
    left.OnButtonPress();
    right.OnButtonPress();
    OC::ui.buttons = 0;

    Shredder sweep;
    sweep.Start();
    for (int is_bipolar = 0; is_bipolar <= 1; ++is_bipolar) {
        for (int volts = 1; volts <= (is_bipolar ? 3 : 5); ++volts) {
            const int maximum = volts * 1536;
            const int minimum = is_bipolar ? -maximum : 0;
            sweep.range[0] = volts;
            sweep.bipolar[0] = is_bipolar;
            sweep.sequence[0][sweep.step] = minimum;
            sweep.cursor = Shredder::WRITE1;
            sweep.OnButtonPress();
            OC::ui.buttons |= OC::CONTROL_BUTTON_A;
            sweep.OnEncoderMove(1);
            const int increment = (maximum - minimum) / 24;
            assert(sweep.nominal_voltage == minimum + increment);
            assert(abs(sweep.held_voltage - sweep.nominal_voltage) == increment / 4);
            for (int i = 1; i < 24; ++i) sweep.OnEncoderMove(1);
            assert(sweep.held_voltage == maximum);
            sweep.OnEncoderMove(1);
            assert(sweep.held_voltage == maximum);
            for (int i = 0; i < 24; ++i) sweep.OnEncoderMove(-1);
            assert(sweep.held_voltage == minimum);
            sweep.OnButtonPress();
            OC::ui.buttons &= ~OC::CONTROL_BUTTON_A;
        }
    }

    Shredder variation;
    variation.Start();
    variation.range[0] = 5;
    variation.sequence[0][variation.step] = 0;
    variation.cursor = Shredder::WRITE1;
    variation.OnButtonPress();
    OC::ui.buttons |= OC::CONTROL_BUTTON_A;
    variation.OnEncoderMove(1);
    const int first_voltage = variation.held_voltage;
    tick(variation);
    assert(variation.sequence[0][0] == first_voltage); // Clock holds the chosen value.
    variation.OnEncoderMove(-1);
    assert(variation.held_voltage == 0);
    variation.OnEncoderMove(1);
    assert(variation.nominal_voltage == 320);
    assert(abs(first_voltage - 320) == 80);
    assert(abs(variation.held_voltage - 320) == 80);
    assert(variation.held_voltage != first_voltage); // Same detent can yield another value.
    variation.OnButtonPress();
    OC::ui.buttons &= ~OC::CONTROL_BUTTON_A;
}
