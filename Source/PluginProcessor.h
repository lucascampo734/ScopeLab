#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ScopeHub.h"

//==============================================================================
class ScopeLabAudioProcessor : public juce::AudioProcessor
{
public:
    ScopeLabAudioProcessor();
    ~ScopeLabAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ScopeLab"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Ableton (VST3) informa acá el nombre y color de la pista
    void updateTrackProperties (const TrackProperties& properties) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ScopeHub&  getHub() noexcept      { return *hub; }
    ScopeSlot& getSlot() noexcept     { return slotIndex >= 0 ? hub->slot (slotIndex) : *privateSlot; }
    int        getSlotIndex() const noexcept { return slotIndex; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };

private:
    juce::SharedResourcePointer<ScopeHub> hub;
    int slotIndex = -1;
    std::unique_ptr<ScopeSlot> privateSlot;   // solo si se llenan los 16 lugares del hub

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeLabAudioProcessor)
};
