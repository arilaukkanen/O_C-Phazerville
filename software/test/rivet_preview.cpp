#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../src/applets/RivetDSP.h"

static void u16(FILE *f, uint16_t value) {
    fputc(value & 255, f); fputc(value >> 8, f);
}
static void u32(FILE *f, uint32_t value) {
    u16(f, value & 65535); u16(f, value >> 16);
}
static void preview(const char *path, RivetDSP::Settings settings, int kind) {
    const uint32_t frames = RivetDSP::kRate * (kind == 3 ? 3 : 2);
    FILE *f = fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    fwrite("RIFF", 1, 4, f); u32(f, 36 + frames * 2);
    fwrite("WAVEfmt ", 1, 8, f); u32(f, 16); u16(f, 1); u16(f, 1);
    u32(f, RivetDSP::kRate); u32(f, RivetDSP::kRate * 2);
    u16(f, 2); u16(f, 16);
    fwrite("data", 1, 4, f); u32(f, frames * 2);

    RivetDSP dsp;
    dsp.settings = settings;
    dsp.SetCV(0, 0);
    dsp.UpdateControls();
    for (uint32_t i = 0; i < frames; ++i) {
        if (i == 0 || (kind == 3 && i % (RivetDSP::kRate / 2) == 0))
            dsp.Trigger(i && kind == 3);
        if (kind == 3 && i % 16 == 0) {
            dsp.SetCV(0, -6144 + int64_t(i) * 12288 / frames);
            dsp.UpdateControls();
        }
        int envelope;
        int sample = dsp.Render(envelope);
        u16(f, (int16_t)(sample * 10));
    }
    fclose(f);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char path[512];
    const char *names[] = {"kick", "metal", "noise", "timbre_sweep"};
    RivetDSP::Settings settings[] = {
        {8,34,3,4,12,3,55},
        {28,27,52,58,51,10,22},
        {25,16,38,35,35,60,22},
        {21,28,34,25,24,20,28}
    };
    for (int i = 0; i < 4; ++i) {
        snprintf(path, sizeof(path), "%s/%s.wav", argv[1], names[i]);
        preview(path, settings[i], i);
    }
}
