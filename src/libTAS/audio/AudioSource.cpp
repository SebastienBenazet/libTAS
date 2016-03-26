/*
    Copyright 2015-2016 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "AudioSource.h"
#include "audiomixing.h"
#include <iterator>     // std::back_inserter
#include <algorithm>    // std::copy
#include "../logging.h"
extern "C" {
    #include <libavutil/opt.h>
}
#include <stdlib.h>

/* Helper function to convert ticks into a number of bytes in the audio buffer */
static int ticksToBytes(struct timespec ticks, int bitDepth, int nbChannels, int frequency)
{
    uint64_t nsecs = ((uint64_t) ticks.tv_sec) * 1000000000 + ticks.tv_nsec;
    uint64_t bytes = (nsecs * (bitDepth / 8) * nbChannels * frequency) / 1000000000;
    return (int) bytes;
}

void gen_random(char *s, const int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < len-4; ++i) {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    s[len-4] = '.';
    s[len-3] = 'w';
    s[len-2] = 'a';
    s[len-1] = 'v';
    s[len] = '\0';
}

AudioSource::AudioSource(void)
{
    id = 0;
    position = 0;
    volume = 1.0f;
    source = SOURCE_UNDETERMINED;
    looping = false;
    state = SOURCE_INITIAL;
    queue_index = 0;

    avr = avresample_alloc_context();
}

int AudioSource::nbQueue()
{
    return buffer_queue.size();
}

int AudioSource::nbQueueProcessed()
{
    return queue_index;
}

int AudioSource::queueSize()
{
    int totalSize = 0;
    for (auto& buffer : buffer_queue) {
        totalSize += buffer->size;
    }
    return totalSize;
}

int AudioSource::getPosition()
{
    int totalPos = 0;
    for (int i=0; i<queue_index; i++) {
        totalPos += buffer_queue[i]->size;
    }
    totalPos += position;

    return totalPos;
}

void AudioSource::setPosition(int pos)
{
    int localPos = pos;
    
    if (looping) {
        localPos %= queueSize();
    }

    for (int i=0; i<buffer_queue.size(); i++) {
        AudioBuffer* ab = buffer_queue[i];
        if (localPos == -1) {
            /* We have set the position already.
             * We have to mark the remaining buffers as unprocessed
             */
            ab->processed = false;
        }
        else if (localPos < ab->size) {
            /* We set the position in this buffer */

            /* Fix unaligned position */
            localPos -= localPos % (ab->nbChannels * ab->bitDepth / 8);
            position = localPos;
            ab->processed = false;
            localPos = -1;
        }
        else {
            /* We traverse the buffer */
            ab->processed = true;
            localPos -= ab->size;
        }
    }
}

void AudioSource::mixWith( struct timespec ticks, uint8_t* outSamples, int outBytes, int outBitDepth, int outNbChannels, int outFrequency, float outVolume)
{
    //debuglog(LCF_SOUND | LCF_FRAME, "Start mixing buffer ", id, " of state ", state);
    if (state != SOURCE_PLAYING)
        return;

    if (buffer_queue.empty())
        return;

    debuglog(LCF_SOUND | LCF_FRAME, "Start mixing source ", id);

    AudioBuffer* curBuf = buffer_queue[queue_index];

    /* Get the sample format */
    AVSampleFormat inFormat, outFormat;
    if (curBuf->bitDepth == 8)
        inFormat = AV_SAMPLE_FMT_U8;
    if (curBuf->bitDepth == 16)
        inFormat = AV_SAMPLE_FMT_S16;
    if (outBitDepth == 8)
        outFormat = AV_SAMPLE_FMT_U8;
    if (outBitDepth == 16)
        outFormat = AV_SAMPLE_FMT_S16;

    /* Check if AV context is opened.
     * If not, set parameters and init it
     */
    if (! avresample_is_open(avr)) {
        /* Set channel layout */
        if (curBuf->nbChannels == 1)
            av_opt_set_int(avr, "in_channel_layout", AV_CH_LAYOUT_MONO, 0);
        if (curBuf->nbChannels == 2)
            av_opt_set_int(avr, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        if (outNbChannels == 1)
            av_opt_set_int(avr, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
        if (outNbChannels == 2)
            av_opt_set_int(avr, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);

        /* Set sample format */
        av_opt_set_int(avr, "in_sample_fmt", inFormat, 0);
        av_opt_set_int(avr, "out_sample_fmt", outFormat, 0);

        /* Set sampling frequency */
        av_opt_set_int(avr, "in_sample_rate", curBuf->frequency, 0);
        av_opt_set_int(avr, "out_sample_rate", outFrequency, 0);

        /* Open the context */
        avresample_open(avr);

        /* Init the WAV out */
        char filename[10];
        gen_random(filename, 9);
        file = SndfileHandle(filename, SFM_WRITE, SF_FORMAT_WAV | ((outBitDepth==16)?SF_FORMAT_PCM_16:SF_FORMAT_PCM_U8), outNbChannels, outFrequency);
    }

    /* Mixing source volume and master volume.
     * Taken from openAL doc:
     * "The implementation is free to clamp the total gain (effective gain
     * per-source multiplied by the listener gain) to one to prevent overflow."
     *
     * TODO: This is where we can support panning.
     */
    float resultVolume = std::max(1.0f, volume * outVolume);
    int lvas = (int)(resultVolume * 65536);
    int rvas = (int)(resultVolume * 65536);

    /* Number of bytes to advance in the buffer samples */
    int inBytes = ticksToBytes(ticks, curBuf->bitDepth, curBuf->nbChannels, curBuf->frequency);

    int oldPosition = position;
    int newPosition = position + inBytes;

    /* Allocate the mixed audio array */
    uint8_t* mixedSamples;
    int linesize;
    int inNSamples = inBytes / (curBuf->bitDepth / 8);
    int outNSamples = outBytes / (outBitDepth / 8);
    av_samples_alloc(&mixedSamples, &linesize, outNbChannels, outNSamples, outFormat, 0);

    int convOutSamples = 0;
    uint8_t* begSamples = &curBuf->samples[oldPosition];
    if (newPosition < curBuf->size) {
        /* We did not reach the end of the buffer, easy case */

        position = newPosition;
        debuglog(LCF_SOUND | LCF_FRAME, "Source ", id, " plays buffer ", curBuf->id, " in range ", oldPosition, " - ", position);
        convOutSamples = avresample_convert(avr, &mixedSamples, outNSamples, outNSamples, &begSamples, inNSamples, inNSamples);
    }
    else {
        /* We reached the end of the buffer */

        int nSamples = (curBuf->size - oldPosition) / (curBuf->bitDepth / 8);
        avresample_convert(avr, nullptr, 0, 0, &begSamples, nSamples, nSamples);

        int remainingBytes = inBytes - (curBuf->size - oldPosition);
        int queue_size = buffer_queue.size();
        int finalIndex;
        int finalPos;

        /* Our for loop conditions are different if we are looping or not */
        if (looping) {
            for (int i=(queue_index+1)%queue_size; remainingBytes>0; i=(i+1)%queue_size) {
                AudioBuffer* loopbuf = buffer_queue[i];
                begSamples = &loopbuf->samples[0];
                if (remainingBytes > loopbuf->size) {
                    nSamples = loopbuf->size / (loopbuf->bitDepth / 8);
                    avresample_convert(avr, nullptr, 0, 0, &begSamples, nSamples, nSamples);
                    loopbuf->processed = true; // Are buffers in a loop ever processed??
                    remainingBytes -= loopbuf->size;
                }
                else {
                    nSamples = remainingBytes / (loopbuf->bitDepth / 8);
                    avresample_convert(avr, nullptr, 0, 0, &begSamples, nSamples, nSamples);
                    finalIndex = i;
                    finalPos = remainingBytes;
                    remainingBytes = 0;
                }
            }
        }
        else {
            for (int i=queue_index+1; (remainingBytes>0) && (i<queue_size); i++) {
                AudioBuffer* loopbuf = buffer_queue[i];
                begSamples = &loopbuf->samples[0];
                if (remainingBytes > loopbuf->size) {
                    nSamples = remainingBytes / (loopbuf->bitDepth / 8);
                    avresample_convert(avr, nullptr, 0, 0, &begSamples, nSamples, nSamples);
                    loopbuf->processed = true;
                    remainingBytes -= loopbuf->size;
                }
                else {
                    nSamples = remainingBytes / (loopbuf->bitDepth / 8);
                    avresample_convert(avr, nullptr, 0, 0, &begSamples, nSamples, nSamples);
                    finalIndex = i;
                    finalPos = remainingBytes;
                    remainingBytes = 0;
                }
            }
        }

        /* Get the mixed samples */
        convOutSamples = avresample_read(avr, &mixedSamples, outNSamples);

        if (remainingBytes > 0) {
            /* We reached the end of the buffer queue */
            position = 0;
            queue_index = 0;
            state = SOURCE_STOPPED;
            avresample_close(avr);
            debuglog(LCF_SOUND | LCF_FRAME, "Source ", id, " plays from buffer ", curBuf->id, " until the end of the queue");
        }
        else {
            /* Update the position in the buffer */
            queue_index = finalIndex;
            position = finalPos;
            debuglog(LCF_SOUND | LCF_FRAME, "Source ", id, " plays from buffer ", curBuf->id, " to some other buffers");
        }
    }

    if (outBitDepth == 8)
        file.writeRaw((void*)mixedSamples, std::max(convOutSamples, outBytes));
    if (outBitDepth == 16)
        file.write((int16_t*)mixedSamples, std::max(convOutSamples, outNSamples));
}

