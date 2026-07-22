#pragma once

#include <vector>
#include <algorithm>

/**
    A simple mono float ring buffer. Used for:
      - accumulating host-supplied samples until a full RAVE frame is ready
      - holding the decoded output aligned to the plugin's reported latency
      - a fixed-length dry delay line so Dry/Wet blending stays phase-aligned

    Single-threaded, synchronous use only (this plugin runs inference inline
    on the audio thread - see PluginProcessor.h for the rationale/tradeoff).
*/
class RingBuffer
{
public:
    void setSize(int numSamples)
    {
        buffer.assign((size_t) std::max(1, numSamples), 0.0f);
        writePos = 0;
        readPos = 0;
        filled = 0;
    }

    void clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        readPos = 0;
        filled = 0;
    }

    int capacity() const noexcept { return (int) buffer.size(); }
    int availableToRead() const noexcept { return filled; }
    int availableToWrite() const noexcept { return (int) buffer.size() - filled; }

    void push(const float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[(size_t) writePos] = data[i];
            writePos = (writePos + 1) % (int) buffer.size();
        }
        filled = std::min((int) buffer.size(), filled + numSamples);
    }

    void pop(float* dest, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = buffer[(size_t) readPos];
            readPos = (readPos + 1) % (int) buffer.size();
        }
        filled = std::max(0, filled - numSamples);
    }

    /** Peek the last `numSamples` samples pushed, without consuming them.
        Used to grab the tail of a just-written frame for crossfading. */
    void peekTail(float* dest, int numSamples) const
    {
        int pos = writePos - numSamples;
        auto n = (int) buffer.size();
        pos = ((pos % n) + n) % n;

        for (int i = 0; i < numSamples; ++i)
        {
            dest[i] = buffer[(size_t) pos];
            pos = (pos + 1) % n;
        }
    }

private:
    std::vector<float> buffer;
    int writePos = 0;
    int readPos = 0;
    int filled = 0;
};
