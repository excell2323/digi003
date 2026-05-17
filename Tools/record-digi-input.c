#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RecorderState {
    FILE *file;
    AudioQueueRef queue;
    UInt32 bytesWritten;
    UInt32 maxBytes;
    int done;
} RecorderState;

static void write_le16(FILE *file, UInt16 value) {
    fputc(value & 0xff, file);
    fputc((value >> 8) & 0xff, file);
}

static void write_le32(FILE *file, UInt32 value) {
    fputc(value & 0xff, file);
    fputc((value >> 8) & 0xff, file);
    fputc((value >> 16) & 0xff, file);
    fputc((value >> 24) & 0xff, file);
}

static void write_wav_header(FILE *file, UInt32 dataBytes) {
    fwrite("RIFF", 1, 4, file);
    write_le32(file, 36 + dataBytes);
    fwrite("WAVE", 1, 4, file);
    fwrite("fmt ", 1, 4, file);
    write_le32(file, 16);
    write_le16(file, 1);
    write_le16(file, 8);
    write_le32(file, 44100);
    write_le32(file, 44100 * 8 * 4);
    write_le16(file, 8 * 4);
    write_le16(file, 32);
    fwrite("data", 1, 4, file);
    write_le32(file, dataBytes);
}

static void patch_wav_header(FILE *file, UInt32 dataBytes) {
    fseek(file, 0, SEEK_SET);
    write_wav_header(file, dataBytes);
    fflush(file);
}

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

static void input_callback(void *userData,
                           AudioQueueRef queue,
                           AudioQueueBufferRef buffer,
                           const AudioTimeStamp *startTime,
                           UInt32 packetCount,
                           const AudioStreamPacketDescription *packetDescriptions) {
    (void)startTime;
    (void)packetCount;
    (void)packetDescriptions;
    RecorderState *state = (RecorderState *)userData;
    if (state->done) {
        return;
    }

    UInt32 bytes = buffer->mAudioDataByteSize;
    if (state->bytesWritten + bytes > state->maxBytes) {
        bytes = state->maxBytes - state->bytesWritten;
    }
    if (bytes > 0) {
        fwrite(buffer->mAudioData, 1, bytes, state->file);
        state->bytesWritten += bytes;
    }
    if (state->bytesWritten >= state->maxBytes) {
        state->done = 1;
        AudioQueueStop(queue, false);
        CFRunLoopStop(CFRunLoopGetCurrent());
        return;
    }
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "Captures/coreaudio-digi003-test.wav";
    double seconds = argc > 2 ? atof(argv[2]) : 0.5;
    if (seconds < 0.1) {
        seconds = 0.5;
    }
    RecorderState state = {};
    state.maxBytes = (UInt32)(44100.0 * seconds) * 8 * 4;
    state.file = fopen(path, "wb+");
    if (state.file == NULL) {
        perror(path);
        return 2;
    }
    write_wav_header(state.file, 0);

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

    err = AudioQueueNewInput(&asbd,
                             input_callback,
                             &state,
                             CFRunLoopGetCurrent(),
                             kCFRunLoopCommonModes,
                             0,
                             &state.queue);
    if (err != noErr) {
        fprintf(stderr, "AudioQueueNewInput failed: %d\n", (int)err);
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
        err = AudioQueueAllocateBuffer(state.queue, 1024 * 8 * 4, &buffer);
        if (err != noErr) {
            fprintf(stderr, "AudioQueueAllocateBuffer failed: %d\n", (int)err);
            return 6;
        }
        err = AudioQueueEnqueueBuffer(state.queue, buffer, 0, NULL);
        if (err != noErr) {
            fprintf(stderr, "AudioQueueEnqueueBuffer failed: %d\n", (int)err);
            return 7;
        }
    }

    err = AudioQueueStart(state.queue, NULL);
    if (err != noErr) {
        fprintf(stderr, "AudioQueueStart failed: %d\n", (int)err);
        return 8;
    }

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds + 2.0, false);
    AudioQueueStop(state.queue, true);
    AudioQueueDispose(state.queue, true);
    patch_wav_header(state.file, state.bytesWritten);
    fclose(state.file);

    printf("%s\nbytes=%u\n", path, state.bytesWritten);
    return state.bytesWritten > 0 ? 0 : 9;
}
