/*
  ==============================================================================

    PluginProcessor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessor::BusesProperties
UltraLowLatencyCallAudioProcessor::makeBusesProperties()
{
    // One stereo input, one mandatory main-mix output, and kMaxClients optional
    // per-client outputs.  Names are zero-padded ("Client 01".."Client 20") so
    // they sort correctly in DAW channel-strip lists.
    auto props = juce::AudioProcessor::BusesProperties()
                     .withInput  ("Input",    juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Main Mix", juce::AudioChannelSet::stereo(), true);

    for (int i = 1; i <= kMaxClients; ++i)
        props = props.withOutput ("Client " + juce::String (i).paddedLeft ('0', 2),
                                  juce::AudioChannelSet::stereo(),
                                  false);   // optional — disabled by default
    return props;
}

//==============================================================================
UltraLowLatencyCallAudioProcessor::UltraLowLatencyCallAudioProcessor()
    : AudioProcessor (makeBusesProperties())
{
    int error = OPUS_OK;

    // ── Create encoder ────────────────────────────────────────────────────────
    // OPUS_APPLICATION_RESTRICTED_LOWDELAY disables look-ahead and minimises
    // algorithmic delay — essential for real-time voice transmission.
    encoder = opus_encoder_create (48000,  // sample rate (Hz)
                                   1,      // channels: 1 = mono mic
                                   OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                   &error);
    if (error != OPUS_OK)
        DBG ("opus_encoder_create failed: " + juce::String (opus_strerror (error)));

    // ── Create decoder ────────────────────────────────────────────────────────
    error = OPUS_OK;
    decoder = opus_decoder_create (48000,  // sample rate (Hz)
                                   1,      // channels: 1 = mono
                                   &error);
    if (error != OPUS_OK)
        DBG ("opus_decoder_create failed: " + juce::String (opus_strerror (error)));

    // ── Configure encoder ─────────────────────────────────────────────────────
    if (encoder != nullptr)
    {
        // 64 000 bps — good voice quality at low bandwidth.
        // At 5 ms frames this yields ~40 bytes per packet.
        opus_encoder_ctl (encoder, OPUS_SET_BITRATE (64000));

        // Complexity 0 (lowest CPU) to 10 (highest quality).
        // 5 balances CPU usage and voice quality for real-time use.
        opus_encoder_ctl (encoder, OPUS_SET_COMPLEXITY (5));
    }

    // Zero-initialise the PCM and packet buffers.
    std::memset (inputBuffer,    0, sizeof (inputBuffer));
    std::memset (outputBuffer,   0, sizeof (outputBuffer));
    std::memset (compressedData, 0, sizeof (compressedData));

    // ── Bind UDP socket ───────────────────────────────────────────────────────
    // Port 9877 is the local (client-side) receiving port.
    // No background thread — receive is polled non-blocking in processBlock().
    if (socket.bindToPort (9877))
        DBG ("UDP socket bound to port 9877");
    else
        DBG ("Failed to bind UDP socket to port 9877");
}

UltraLowLatencyCallAudioProcessor::~UltraLowLatencyCallAudioProcessor()
{
    // Close the UDP socket, then release codec handles.
    // No background thread to join — receive runs inside processBlock only.
    socket.shutdown();

    if (encoder != nullptr) { opus_encoder_destroy (encoder); encoder = nullptr; }
    if (decoder != nullptr) { opus_decoder_destroy (decoder); decoder = nullptr; }
}

//==============================================================================
void UltraLowLatencyCallAudioProcessor::prepareToPlay (double sampleRate,
                                                       int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void UltraLowLatencyCallAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool UltraLowLatencyCallAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Main output must be stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Main input must be stereo.
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Client buses (index 1..kMaxClients) must be stereo or disabled.
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& bus = layouts.outputBuses.getReference (i);
        if (bus != juce::AudioChannelSet::stereo() &&
            bus != juce::AudioChannelSet::disabled())
            return false;
    }

    return true;
  #endif
}
#endif

//==============================================================================
// processBlock() — encode mic → send to server, receive from server → output
//
// Signal flow per block (480 samples = 10 ms):
//
//   [SEND]   inputBuffer → opus_encode_float() → socket.write(192.168.10.117:9876)
//   [RECV]   socket.waitUntilReady(0ms) → socket.read() → opus_decode_float() → outputBuffer
//   [OUT]    outputBuffer → ch0 (silence if no packet arrived this block)
//            ch0 → ch1  (mono → stereo)
void UltraLowLatencyCallAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    // Skip if either codec handle failed to initialise.
    if (encoder == nullptr || decoder == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();

    // Opus requires exactly kFrameSize (480) samples per call.
    if (numSamples != kFrameSize)
        return;

    // ── [1] Copy mic input (ch 0) into the staging buffer ────────────────────
    std::memcpy (inputBuffer, buffer.getReadPointer (0), kFrameSize * sizeof (float));

    // ── [2] Encode mic input with Opus ────────────────────────────────────────
    const auto encodeStart = juce::Time::getMillisecondCounterHiRes();

    const opus_int32 encodedBytes = opus_encode_float (encoder,
                                                       inputBuffer,
                                                       kFrameSize,
                                                       compressedData,
                                                       kMaxPacketBytes);
    if (encodedBytes < 0)
    {
        DBG ("opus_encode_float error: " + juce::String (opus_strerror ((int) encodedBytes)));
        buffer.clear();
        return;
    }

    // ── [3] Send encoded packet to the session server ─────────────────────────
    if (! socket.write ("192.168.10.117", 9876, compressedData, (int) encodedBytes))
        DBG ("UDP send failed");

    DBG ("encode+send: "
         + juce::String (juce::Time::getMillisecondCounterHiRes() - encodeStart, 3)
         + " ms  |  " + juce::String (encodedBytes) + " bytes");

    // ── [4] Non-blocking receive — poll once, return immediately if no data ───
    hasReceivedData = false;

    if (socket.waitUntilReady (false, 0))   // 0 ms timeout = don't wait
    {
        juce::String senderIP;
        int          senderPort = 0;
        const int received = socket.read (receivePacket,
                                          sizeof (receivePacket),
                                          false,       // non-blocking
                                          senderIP,
                                          senderPort);
        if (received > 0)
        {
            // Decode the incoming Opus packet directly into outputBuffer.
            const int decodedSamples = opus_decode_float (decoder,
                                                          receivePacket,
                                                          received,
                                                          outputBuffer,
                                                          kFrameSize,
                                                          0 /* FEC off */);
            if (decodedSamples > 0)
                hasReceivedData = true;
            else
                DBG ("opus_decode_float error: "
                     + juce::String (opus_strerror (decodedSamples)));
        }
    }

    // ── [5] Write output — decoded audio or silence ───────────────────────────
    if (hasReceivedData)
        std::memcpy (buffer.getWritePointer (0), outputBuffer,
                     kFrameSize * sizeof (float));
    else
        buffer.clear (0, 0, numSamples);   // no packet this block → silence

    // ── [6] Copy left channel (0) to right channel (1) ───────────────────────
    if (buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

//==============================================================================
bool UltraLowLatencyCallAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* UltraLowLatencyCallAudioProcessor::createEditor()
{
    return new UltraLowLatencyCallAudioProcessorEditor (*this);
}

//==============================================================================
void UltraLowLatencyCallAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ignoreUnused (destData);
}

void UltraLowLatencyCallAudioProcessor::setStateInformation (const void* data,
                                                             int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
const juce::String UltraLowLatencyCallAudioProcessor::getName() const
    { return JucePlugin_Name; }

bool UltraLowLatencyCallAudioProcessor::acceptsMidi() const
{
  #if JucePlugin_WantsMidiInput
    return true;
  #else
    return false;
  #endif
}

bool UltraLowLatencyCallAudioProcessor::producesMidi() const
{
  #if JucePlugin_ProducesMidiOutput
    return true;
  #else
    return false;
  #endif
}

bool UltraLowLatencyCallAudioProcessor::isMidiEffect() const
{
  #if JucePlugin_IsMidiEffect
    return true;
  #else
    return false;
  #endif
}

double UltraLowLatencyCallAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    UltraLowLatencyCallAudioProcessor::getNumPrograms()              { return 1; }
int    UltraLowLatencyCallAudioProcessor::getCurrentProgram()           { return 0; }
void   UltraLowLatencyCallAudioProcessor::setCurrentProgram (int)       {}

const juce::String UltraLowLatencyCallAudioProcessor::getProgramName (int)
    { return {}; }

void UltraLowLatencyCallAudioProcessor::changeProgramName (int,
                                                           const juce::String&) {}

//==============================================================================
// Factory function — called by JUCE to create a new plugin instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UltraLowLatencyCallAudioProcessor();
}