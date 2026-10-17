/*
 * VST3 build of the Limiter on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: LimiterCore does the
 * audio, as in the LPI build (lpi_limiter.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Limiter.
 */
#include "vst3/LoudioVst3.h"
#include "LimiterCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "LimiterUi.h"
#endif

struct LimiterVst3Traits {
    using Core = LimiterCore;

    static constexpr const char* kName = "Loudio Limiter";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Dynamics";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x6B229C3B, 0x8A6046B9, 0x898A19B0, 0x9E4EF9AA };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0xF853CC7F, 0xB563474C, 0x8257BE39, 0x6FEEF064 };

    static std::vector<const char*> choiceLabels(int index) {
        if (index == kLimiterOversampling) return { "Off", "2x", "4x" };
        return {};
    }

    /* One meter frame per block: in, out, gain reduction (dB). */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[3] = { core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb() };
        loudio::vst3::pushFrame(dspToUi, 'M', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = LimiterUi;

    static constexpr int kPanelWidth = kLimiterPanelWidth;
    static constexpr int kPanelHeight = kLimiterPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Limiter";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return limiterUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { limiterUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { limiterUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[3];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') limiterUiFeedMeters(ui, frame[0], frame[1], frame[2]);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(LimiterVst3Traits)
