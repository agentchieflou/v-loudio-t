/*
 * VST3 build of the Reverb on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: ReverbCore does the
 * audio, as in the LPI build (lpi_reverb.cpp); on Windows the editor hosts
 * the shared ReverbUi widget tree in a cuif child window. Everything
 * format-specific lives in the shell; this file is only what makes it the
 * Reverb.
 *
 * Parameters, ids, ranges and order all come from kReverbParams; state is
 * ReverbCore's "LRVB" blob, the same bytes the LPI build saves.
 */
#include "vst3/LoudioVst3.h"
#include "ReverbCore.h"

#include <algorithm>
#include <cmath>
#ifdef LOUDIO_VST3_EDITOR
#include "ReverbUi.h"
#endif

struct ReverbVst3Traits {
    using Core = ReverbCore;

    static constexpr const char* kName = "Loudio Reverb";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Reverb";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x374E3326, 0x55CA4828, 0xB7484DD9, 0xEC5E5F84 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0xCAE8B0E6, 0x0A2442AA, 0xB0649D51, 0xD5E60D1E };

    static std::vector<const char*> choiceLabels(int index) {
        if (index == kParamMode) return { "Room", "Hall", "Plate", "Cathedral", "Spring" };
        if (index == kParamUiTheme) return { "Default", "Hello Kitty", "Greens" }; /* cosmetic, persisted like any other setting (#68) */
        return {};
    }

    /* Freeze holds the tail forever. Otherwise the longest it can ring:
     * pre-delay plus the longest band's decay (decay time x its band
     * multiplier), taken at the ranges' maxima so it never needs updating
     * when a parameter moves. */
    static Steinberg::uint32 tailSamples(const float* values, double sampleRate) {
        if (values[kParamFreeze] >= 0.5f) return Steinberg::Vst::kInfiniteTail;
        const auto& t = Core::table();
        const double seconds = t[kParamPreDelay].maxValue / 1000.0
            + (double)t[kParamDecayTime].maxValue * std::max({ t[kParamDecayLow].maxValue, t[kParamDecayMid].maxValue, t[kParamDecayHigh].maxValue });
        return (Steinberg::uint32)std::ceil(seconds * sampleRate);
    }

    /* Analyzer frames, audio thread -> UI. If a host's block is large
     * enough for the 1024-sample FFT to complete more than once per block
     * (offline bounce), only the last completion is pushed; cosmetic only. */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float magnitudes[64];
        if (core.isLeftAnalyzerFrameDirty()) {
            core.getLeftAnalyzerMagnitudes(magnitudes);
            loudio::vst3::pushFrame(dspToUi, 'L', magnitudes);
        }
        if (core.isRightAnalyzerFrameDirty()) {
            core.getRightAnalyzerMagnitudes(magnitudes);
            loudio::vst3::pushFrame(dspToUi, 'R', magnitudes);
        }
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = ReverbUi;

    static constexpr int kPanelWidth = 800;
    static constexpr int kPanelHeight = 600;
    static constexpr const char* kWindowTitle = "Reverb Panel";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return reverbUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { reverbUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { reverbUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(ui->leftSpectrum)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            float* dst = tag == 'L' ? ui->leftSpectrum : (tag == 'R' ? ui->rightSpectrum : nullptr);
            if (dst) {
                cuif_spsc_read(&fromDsp, (unsigned char*)dst, sizeof(ui->leftSpectrum));
            } else {
                float discard[64];
                cuif_spsc_read(&fromDsp, (unsigned char*)discard, sizeof(discard));
            }
        }
    }
#endif
};

/* No lookahead anywhere in ReverbCore's chain (pre-delay is a musical
 * effect, not processing latency), so the reported latency is 0. */

LOUDIO_VST3_DEFINE_PLUGIN(ReverbVst3Traits)
