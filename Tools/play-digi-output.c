#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PlayerState {
    AudioQueueRef queue;
    UInt32 framesWritten;
    UInt32 maxFrames;
    double phase;
    double phaseStep;
    int done;
} PlayerState;

static int cfstring_contains(CFStringRef value, const char *needle) {
    char buffer[512];
    if (value == NULL) {
        return 0;
    }
    if (!CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return 0;
    }
    return strstr(buffer, needle) != NULL;
}

static OSStatus find_digi_device_uid(CFStringRef *outUID) {
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size);
    if (err != noErr) {
        return err;
    }

    UInt32 count = size / sizeof(AudioDeviceID);
    AudioDeviceID *devices = calloc(count, sizeof(AudioDeviceID));
    if (devices == NULL) {
        return -1;
    }
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, devices);
    if (err != noErr) {
        free(devices);
        return err;
    }

    for (UInt32 i = 0; i < count; ++i) {
        CFStringRef name = NULL;
        UInt32 nameSize = sizeof(name);
        AudioObjectPropertyAddress nameAddr = {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        if (AudioObjectGetPropertyData(devices[i], &nameAddr, 0, NULL, &nameSize, &name) != noErr) {
            continue;
        }
        int isDigi = cfstring_contains(name, "Digi 003 FireWire");
        if (name != NULL) {
            CFRelease(name);
        }
        if (!isDigi) {
            continue;
        }

        CFStringRef uid = NULL;
        UInt32 uidSize = sizeof(uid);
        AudioObjectPropertyAddress uidAddr = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        err = AudioObjectGetPropertyData(devices[i], &uidAddr, 0, NULL, &uidSize, &uid);
        if (err == noErr && uid != NULL) {
            *outUID = uid;
            free(devices);
            return noErr;
        }
    }

    free(devices);
    return kAudioHardwareBadObjectError;
}

static void output_callback(void *userData, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    PlayerState *state = (PlayerState *)userData;
    if (state->done) {
        return;
    }

    const UInt32 channels = 8;
    const UInt32 framesPerBuffer = 512;
    int32_t *samples = (int32_t *)buffer->mAudioData;
    UInt32 frames = framesPerBuffer;
    if (state->framesWritten + frames > state->maxFrames) {
        frames = state->maxFrames - state->framesWritten;
    }

    for (UInt32 frame = 0; frame < frames; ++frame) {
        double s = sin(state->phase) * 0.12;
        int32_t sample = (int32_t)(s * 2147483647.0);
        samples[frame * channels + 0] = sample;
        samples[frame * channels + 1] = sample;
        for (UInt32 channel = 2; channel < channels; ++channel) {
            samples[frame * channels + channel] = 0;
        }
        state->phase += state->phaseStep;
        if (state->phase >= 2.0 * M_PI) {
            state->phase -= 2.0 * M_PI;
        }
    }

    buffer->mAudioDataByteSize = frames * channels * sizeof(int32_t);
    state->framesWritten += frames;
    if (frames > 0) {
        AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
    }
    if (state->framesWritten >= state->maxFrames) {
        state->done = 1;
        AudioQueueStop(queue, false);
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 2.0;
    double frequency = argc > 2 ? atof(argv[2]) : 440.0;
    if (seconds < 0.1) {
        seconds = 2.0;
    }
    if (frequency < 20.0 || frequency > 20000.0) {
        frequency = 440.0;
    }

    PlayerState state = {};
    state.maxFrames = (UInt32)(44100.0 * seconds);
    state.phaseStep = (2.0 * M_PI * frequency) / 44100.0;

    CFStringRef uid = NULL;
    OSStatus err = find_digi_device_uid(&uid);
    if (err != noErr) {
        fprintf(stderr, "Digi 003 FireWire CoreAudio device not found: %d\n", (int)err);
        return 3;
    }

    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = 44100.0;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = 8 * 4;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 8 * 4;
    asbd.mChannelsPerFrame = 8;
    asbd.mBitsPerChannel = 32;

    err = AudioQueueNewOutput(&asbd,
                              output_callback,
                              &state,
                              CFRunLoopGetCurrent(),
                              kCFRunLoopCommonModes,
                              0,
                              &state.queue);
    if (err != noErr) {
        fprintf(stderr, "AudioQueueNewOutput failed: %d\n", (int)err);
        CFRelease(uid);
        return 4;
    }

    err = AudioQueueSetProperty(state.queue, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    if (err != noErr) {
        fprintf(stderr, "AudioQueueSetProperty(CurrentDevice) failed: %d\n", (int)err);
        return 5;
    }

    for (int i = 0; i < 3; ++i) {
        AudioQueueBufferRef buffer = NULL;
        err = AudioQueueAllocateBuffer(state.queue, 512 * 8 * 4, &buffer);
        if (err != noErr) {
            fprintf(stderr, "AudioQueueAllocateBuffer failed: %d\n", (int)err);
            return 6;
        }
        output_callback(&state, state.queue, buffer);
    }

    err = AudioQueueStart(state.queue, NULL);
    if (err != noErr) {
        fprintf(stderr, "AudioQueueStart failed: %d\n", (int)err);
        return 7;
    }

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds + 2.0, false);
    AudioQueueStop(state.queue, true);
    AudioQueueDispose(state.queue, true);
    printf("frames=%u\n", state.framesWritten);
    return state.framesWritten > 0 ? 0 : 8;
}
