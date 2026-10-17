/*
 * VST3 build of the Compressor on the VST3 SDK directly (#171), on the shared
 * shell (VSTs/common/vst3/LoudioVst3.h). No JUCE: CompressorCore does the
 * audio, as in the LPI build (lpi_compressor.cpp); on Windows the editor hosts
 * the plugin's cuif UI in a child window. Everything format-specific lives
 * in the shell; this file is only what makes it the Compressor.
 */
#include "vst3/LoudioVst3.h"
#include "CompressorCore.h"
#ifdef LOUDIO_VST3_EDITOR
#include "CompressorUi.h"
#endif

struct CompressorVst3Traits {
    using Core = CompressorCore;

    static constexpr const char* kName = "Loudio Compressor";
    static constexpr const char* kVendor = "Loudio";
    static constexpr const char* kUrl = "https://github.com/agentchieflou/v-loudio-t";
    static constexpr const char* kEmail = "";
    static constexpr const char* kCategories = "Fx|Dynamics";
    static constexpr Steinberg::uint32 kProcessorUid[4] = { 0x43360442, 0xC5554BEE, 0x8829293D, 0xB9FA7B38 };
    static constexpr Steinberg::uint32 kControllerUid[4] = { 0x9A680CCF, 0x2F1E4CAE, 0x913B4EB4, 0x8F1992EC };

    static std::vector<const char*> choiceLabels(int index) {
        if (index == kCompressorDetector) return { "Peak", "RMS" };
        return {};
    }

    /* One meter frame per block: in, out, gain reduction (dB). Dropped
     * when the ring is full, which only costs a meter update. */
    static void afterProcess(Core& core, cuif_spsc_ring_buffer& dspToUi) {
        float frame[3] = { core.meterInputDb(), core.meterOutputDb(), core.meterGainReductionDb() };
        loudio::vst3::pushFrame(dspToUi, 'M', frame);
    }

#ifdef LOUDIO_VST3_EDITOR
    using Ui = CompressorUi;

    static constexpr int kPanelWidth = kCompressorPanelWidth;
    static constexpr int kPanelHeight = kCompressorPanelHeight;
    static constexpr const char* kWindowTitle = "Loudio Compressor";

    static Ui* uiCreate(float w, float h, void* context, loudio::vst3::SendParamsFn sendParams) {
        return compressorUiCreate(w, h, { context, sendParams });
    }
    static void uiDestroy(Ui* ui) { compressorUiDestroy(ui); }
    static cuif_widget* uiRoot(Ui* ui) { return ui->root; }
    static void uiSyncFromParams(Ui* ui, const float* params) { compressorUiSyncFromParams(ui, params); }

    static void pollDspToUi(Ui* ui, cuif_spsc_ring_buffer& fromDsp) {
        unsigned char tag;
        float frame[3];
        while (cuif_spsc_readable(&fromDsp) >= sizeof(tag) + sizeof(frame)) {
            if (cuif_spsc_read(&fromDsp, &tag, sizeof(tag)) != sizeof(tag)) break;
            cuif_spsc_read(&fromDsp, (unsigned char*)frame, sizeof(frame));
            if (tag == 'M') compressorUiFeedMeters(ui, frame[0], frame[1], frame[2]);
        }
    }
#endif
};

LOUDIO_VST3_DEFINE_PLUGIN(CompressorVst3Traits)
