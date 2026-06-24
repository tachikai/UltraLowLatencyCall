/*
  ==============================================================================

    PluginProcessor.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <opus/opus.h>     // Opus codec — installed via vcpkg

//==============================================================================
class UltraLowLatencyCallAudioProcessor  : public juce::AudioProcessor
{
public:
    //==========================================================================
    UltraLowLatencyCallAudioProcessor();
    ~UltraLowLatencyCallAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    // processBlock() is called repeatedly by the host.
    // Mic audio (ch 0) is encoded with Opus, decoded immediately, then sent to
    // both output channels.
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    //==========================================================================
    // Opus encoder and decoder handles.
    // Created in the constructor; destroyed in the destructor.
    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;

    // ── Buffer sizes ──────────────────────────────────────────────────────────
    // Opus supports frame sizes of 120, 240, 480, 960, 1920, or 2880 samples
    // at 48 kHz.  480 samples = 10 ms — matches the host's actual buffer size.
    static constexpr int kFrameSize      = 480;   // samples per Opus frame
    static constexpr int kMaxPacketBytes = 4000;  // max compressed bytes per frame

    // Float PCM buffers used as staging areas between JUCE and Opus.
    float         inputBuffer   [kFrameSize];     // mic input  (ch 0 → Opus)
    float         outputBuffer  [kFrameSize];     // decoded    (Opus → ch 0)

    // Byte buffer that holds the compressed Opus packet between encode and decode.
    unsigned char compressedData[kMaxPacketBytes];

    // ── Timing measurement ────────────────────────────────────────────────────
    // 48 000 Hz / 480 samples = 100 blocks per second.
    static constexpr int kBlocksPerSecond  = 100;
    double accumulatedEncodeTimeMs         = 0.0;
    double accumulatedDecodeTimeMs         = 0.0;
    int    blockCount                      = 0;
    double accumulatedTimeMs               = 0.0;
    int    frameCount                      = 0;

    // ── UDP networking (non-blocking, runs entirely inside processBlock) ────────
    // No separate thread — receive is polled with waitUntilReady(timeout=0)
    // so it never blocks the audio callback.
    juce::DatagramSocket socket { false };          // false = no broadcast
    unsigned char        receivePacket[4000];       // raw incoming Opus packet
    bool                 hasReceivedData = false;   // true when outputBuffer holds fresh decoded PCM

    // ── Multi-client parallel output ──────────────────────────────────────────
    static constexpr int kMaxClients = 20;

    // Per-client decoded PCM.  clientBuffers[i] is written by the receive path
    // and read by the output path — both happen in the same processBlock call,
    // so no cross-thread synchronisation is needed.
    float clientBuffers[kMaxClients][kFrameSize] = {};

    int  numConnectedClients  = 0;     // number of active client slots (0..kMaxClients)
    bool isParallelOutputMode = false; // true = write each client to its own bus

    // Builds the initial BusesProperties: 1 main stereo output + kMaxClients
    // optional stereo client buses.  Called once from the constructor initialiser.
    static juce::AudioProcessor::BusesProperties makeBusesProperties();

    //==========================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltraLowLatencyCallAudioProcessor)
};