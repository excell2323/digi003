#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    kControlPort = 0x0e,
    kDebugUserClientType = 0x44494749,
    kDebugSelectorMidiMessage = 0,
    kDebugSelectorMidiBytes = 2,
    kMaxRawMidiBytesPerCall = 512,
    kDecodedRecentCapacity = 256,
    kFaderFeedbackChannelCount = 8,
    kSysexThrottleLineCount = 256,
    kSysexPayloadRememberBytes = 32,
    kDigi003DisplayPayloadOffset = 7,
    kDigi003DisplayPayloadLength = 7,
    kDigi003DisplayClassUnknown = 0,
    kDigi003DisplayClassValue = 1,
    kDigi003DisplayClassName = 2,
};

static const char *kDefaultPortName = "Avid 003 Port 3 (Control)";

static volatile sig_atomic_t g_should_stop = 0;
static io_service_t g_probe_service = IO_OBJECT_NULL;
static io_connect_t g_debug_connect = IO_OBJECT_NULL;
static bool g_verbose = false;
static bool g_forward_feedback_to_driver = true;
static FILE *g_feedback_log = NULL;
static uint64_t g_fader_feedback_last_forward_ns[kFaderFeedbackChannelCount] = {0};
static uint8_t g_fader_feedback_last_value[kFaderFeedbackChannelCount] = {0};
static bool g_fader_feedback_last_valid[kFaderFeedbackChannelCount] = {false};
static uint64_t g_sysex_line_last_forward_ns[kSysexThrottleLineCount] = {0};
static UInt16 g_sysex_line_last_length[kSysexThrottleLineCount] = {0};
static uint8_t g_sysex_line_last_payload[kSysexThrottleLineCount][kSysexPayloadRememberBytes] = {{0}};
static uint8_t g_sysex_line_last_class[kSysexThrottleLineCount] = {0};

static void handle_signal(int signo)
{
    (void)signo;
    g_should_stop = 1;
}

static void close_debug_connection(void)
{
    if (g_debug_connect != IO_OBJECT_NULL) {
        IOServiceClose(g_debug_connect);
        g_debug_connect = IO_OBJECT_NULL;
    }
}

static int parse_u32(const char *text, uint32_t min_value, uint32_t max_value, uint32_t *value)
{
    if (text == NULL || value == NULL) {
        return 0;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed < min_value || parsed > max_value) {
        return 0;
    }

    *value = (uint32_t)parsed;
    return 1;
}

static io_service_t find_probe_service(void)
{
    io_service_t service =
        IOServiceGetMatchingService(kIOMainPortDefault, IOServiceNameMatching("FireWireOHCIProbe"));
    if (service != IO_OBJECT_NULL) {
        return service;
    }

    CFMutableDictionaryRef matching = IOServiceMatching("IOUserService");
    if (matching == NULL) {
        return IO_OBJECT_NULL;
    }
    CFDictionarySetValue(matching, CFSTR("IOUserClass"), CFSTR("FireWireOHCIProbe"));
    return IOServiceGetMatchingService(kIOMainPortDefault, matching);
}

static bool read_u64_property(io_service_t service, CFStringRef key, uint64_t *value)
{
    CFTypeRef object = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (object == NULL) {
        return false;
    }

    bool ok = false;
    if (CFGetTypeID(object) == CFNumberGetTypeID()) {
        ok = CFNumberGetValue((CFNumberRef)object, kCFNumberSInt64Type, value);
    }

    CFRelease(object);
    return ok;
}

static bool read_u32_property(io_service_t service, CFStringRef key, uint32_t *value)
{
    uint64_t raw = 0;
    if (!read_u64_property(service, key, &raw)) {
        return false;
    }
    *value = (uint32_t)raw;
    return true;
}

static size_t read_recent_messages(io_service_t service, uint32_t *messages, size_t capacity)
{
    CFTypeRef object =
        IORegistryEntryCreateCFProperty(service, CFSTR("ProbeControlDecodedRecentMessages"), kCFAllocatorDefault, 0);
    if (object == NULL) {
        return 0;
    }

    size_t count = 0;
    if (CFGetTypeID(object) == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)object;
        CFIndex length = CFDataGetLength(data);
        const UInt8 *bytes = CFDataGetBytePtr(data);
        if (bytes != NULL) {
            size_t available = (size_t)length / sizeof(uint32_t);
            if (available > capacity) {
                available = capacity;
            }
            for (size_t i = 0; i < available; ++i) {
                messages[i] =
                    ((uint32_t)bytes[i * 4 + 0]) |
                    ((uint32_t)bytes[i * 4 + 1] << 8) |
                    ((uint32_t)bytes[i * 4 + 2] << 16) |
                    ((uint32_t)bytes[i * 4 + 3] << 24);
            }
            count = available;
        }
    }

    CFRelease(object);
    return count;
}

static bool driver_feedback_ready(void)
{
    if (!g_forward_feedback_to_driver || g_probe_service == IO_OBJECT_NULL) {
        return false;
    }

    uint32_t running = 0;
    return read_u32_property(g_probe_service, CFSTR("ProbeDigiLiveRunning"), &running) &&
           running != 0;
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static bool is_digi003_display_sysex(const uint8_t *bytes, UInt16 length, uint8_t *line)
{
    if (bytes == NULL || length < 6 || line == NULL) {
        return false;
    }
    if (bytes[0] != 0xf0u ||
        bytes[1] != 0x13u ||
        bytes[2] != 0x01u ||
        bytes[3] != 0x40u) {
        return false;
    }
    *line = bytes[4];
    return true;
}

static bool ascii_digit(uint8_t value)
{
    return value >= '0' && value <= '9';
}

static bool ascii_alpha(uint8_t value)
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static uint8_t classify_digi003_display_sysex(const uint8_t *bytes, UInt16 length)
{
    if (bytes == NULL || length < (kDigi003DisplayPayloadOffset + kDigi003DisplayPayloadLength)) {
        return kDigi003DisplayClassUnknown;
    }

    bool has_alpha = false;
    bool has_digit = false;
    for (UInt16 i = 0; i < kDigi003DisplayPayloadLength; ++i) {
        uint8_t value = bytes[kDigi003DisplayPayloadOffset + i];
        has_alpha = has_alpha || ascii_alpha(value);
        has_digit = has_digit || ascii_digit(value);
    }

    if (has_alpha) {
        return kDigi003DisplayClassName;
    }
    if (has_digit) {
        return kDigi003DisplayClassValue;
    }
    return kDigi003DisplayClassUnknown;
}

static bool is_digi003_transport_counter_sysex(const uint8_t *bytes, UInt16 length, uint8_t line)
{
    if (bytes == NULL || length < (kDigi003DisplayPayloadOffset + kDigi003DisplayPayloadLength + 1)) {
        return false;
    }

    const uint8_t *payload = bytes + kDigi003DisplayPayloadOffset;
    if (line == 0x06u) {
        return payload[0] == ' ' &&
               payload[1] == ' ' &&
               payload[2] == ' ' &&
               payload[3] == ' ' &&
               ascii_digit(payload[4]) &&
               ascii_digit(payload[5]) &&
               ascii_digit(payload[6]);
    }

    if (line == 0x07u) {
        return payload[0] == '|' &&
               ascii_digit(payload[1]) &&
               payload[2] == '|' &&
               ascii_digit(payload[3]) &&
               ascii_digit(payload[4]) &&
               ascii_digit(payload[5]) &&
               payload[6] == ' ';
    }

    return false;
}

static bool should_forward_sysex_to_driver(const uint8_t *bytes, UInt16 length)
{
    uint8_t line = 0;
    if (!is_digi003_display_sysex(bytes, length, &line)) {
        return true;
    }

    if (is_digi003_transport_counter_sysex(bytes, length, line)) {
        return false;
    }

    UInt16 remembered = length < kSysexPayloadRememberBytes ? length : kSysexPayloadRememberBytes;
    bool duplicate =
        g_sysex_line_last_length[line] == remembered &&
        memcmp(g_sysex_line_last_payload[line], bytes, remembered) == 0;

    uint64_t now = monotonic_ns();
    uint64_t last = g_sysex_line_last_forward_ns[line];
    uint8_t payload_class = classify_digi003_display_sysex(bytes, length);
    bool class_changed =
        payload_class != kDigi003DisplayClassUnknown &&
        g_sysex_line_last_class[line] != kDigi003DisplayClassUnknown &&
        payload_class != g_sysex_line_last_class[line];
    uint64_t min_interval_ns = 50000000ull;
    if (class_changed) {
        min_interval_ns = 10000000ull;
    }

    if (now != 0 && last != 0) {
        uint64_t age = now - last;
        if (age < min_interval_ns || (duplicate && age < 250000000ull)) {
            return false;
        }
    }

    g_sysex_line_last_forward_ns[line] = now;
    g_sysex_line_last_length[line] = remembered;
    g_sysex_line_last_class[line] = payload_class;
    memcpy(g_sysex_line_last_payload[line], bytes, remembered);
    return true;
}

static bool should_forward_short_message_to_driver(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t command = status & 0xf0u;
    if (command == 0x80u || command == 0x90u) {
        return true;
    }

    if (command != 0xb0u) {
        return false;
    }

    if (data1 > 0x3fu) {
        return false;
    }

    uint8_t channel = data1 & 0x07u;
    uint64_t now = monotonic_ns();
    uint8_t last = g_fader_feedback_last_value[channel];
    uint8_t delta = data2 > last ? data2 - last : last - data2;
    uint64_t age = now - g_fader_feedback_last_forward_ns[channel];
    bool should_forward =
        !g_fader_feedback_last_valid[channel] ||
        delta >= 4u ||
        (data2 != last && age >= 33333333ull);

    if (!should_forward) {
        return false;
    }

    g_fader_feedback_last_valid[channel] = true;
    g_fader_feedback_last_value[channel] = data2;
    g_fader_feedback_last_forward_ns[channel] = now;
    return true;
}

static void send_coremidi_message(MIDIEndpointRef source, uint8_t status, uint8_t data1, uint8_t data2)
{
    Byte buffer[64];
    MIDIPacketList *packet_list = (MIDIPacketList *)buffer;
    MIDIPacket *packet = MIDIPacketListInit(packet_list);
    const Byte bytes[3] = {status, data1, data2};
    packet = MIDIPacketListAdd(packet_list, sizeof(buffer), packet, 0, 3, bytes);
    if (packet == NULL) {
        return;
    }
    MIDIReceived(source, packet_list);
    if (g_verbose) {
        printf("to CoreMIDI: %02x %02x %02x\n", status, data1, data2);
        fflush(stdout);
    }
}

static void print_midi_bytes(FILE *stream, const char *prefix, const uint8_t *bytes, UInt16 length)
{
    if (stream == NULL || bytes == NULL) {
        return;
    }

    fprintf(stream, "%s len=%u:", prefix, length);
    UInt16 limit = length;
    if (limit > 256) {
        limit = 256;
    }
    for (UInt16 i = 0; i < limit; ++i) {
        fprintf(stream, " %02x", bytes[i]);
    }
    if (limit < length) {
        fprintf(stream, " ...");
    }

    bool has_printable = false;
    for (UInt16 i = 0; i < limit; ++i) {
        if (bytes[i] >= 0x20u && bytes[i] <= 0x7eu) {
            has_printable = true;
            break;
        }
    }
    if (has_printable) {
        fprintf(stream, " |");
        for (UInt16 i = 0; i < limit; ++i) {
            uint8_t byte = bytes[i];
            fputc((byte >= 0x20u && byte <= 0x7eu) ? byte : '.', stream);
        }
        if (limit < length) {
            fprintf(stream, "...");
        }
        fprintf(stream, "|");
    }
    fprintf(stream, "\n");
    fflush(stream);
}

static kern_return_t ensure_debug_connection(void)
{
    if (!driver_feedback_ready()) {
        return kIOReturnNotReady;
    }
    if (g_debug_connect != IO_OBJECT_NULL) {
        return KERN_SUCCESS;
    }

    return IOServiceOpen(g_probe_service, mach_task_self(), kDebugUserClientType, &g_debug_connect);
}

static kern_return_t send_debug_midi(uint32_t port, uint32_t status, uint32_t data1, uint32_t data2)
{
    kern_return_t ret = ensure_debug_connection();
    if (ret != KERN_SUCCESS) {
        return ret;
    }

    uint64_t scalar_input[4] = {port, status, data1, data2};
    ret = IOConnectCallScalarMethod(g_debug_connect,
                                    kDebugSelectorMidiMessage,
                                    scalar_input,
                                    4,
                                    NULL,
                                    NULL);
    if (ret != KERN_SUCCESS) {
        close_debug_connection();
    }
    return ret;
}

static kern_return_t send_debug_midi_bytes(uint32_t port, const uint8_t *bytes, UInt16 length)
{
    if (bytes == NULL || length == 0) {
        return kIOReturnBadArgument;
    }

    UInt16 offset = 0;
    while (offset < length) {
        UInt16 chunk = length - offset;
        if (chunk > kMaxRawMidiBytesPerCall) {
            chunk = kMaxRawMidiBytesPerCall;
        }

        kern_return_t ret = ensure_debug_connection();
        if (ret != KERN_SUCCESS) {
            return ret;
        }

        uint64_t scalar_input[1] = {port};
        ret = IOConnectCallMethod(g_debug_connect,
                                  kDebugSelectorMidiBytes,
                                  scalar_input,
                                  1,
                                  bytes + offset,
                                  chunk,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL);
        if (ret != KERN_SUCCESS) {
            close_debug_connection();
            return ret;
        }

        offset += chunk;
    }

    return KERN_SUCCESS;
}

static int midi_message_length(uint8_t status)
{
    uint8_t command = status & 0xf0u;
    if (command == 0xc0u || command == 0xd0u) {
        return 2;
    }
    if (command >= 0x80u && command <= 0xe0u) {
        return 3;
    }
    return 0;
}

static void midi_read_proc(const MIDIPacketList *packet_list, void *read_proc_refcon, void *src_conn_refcon)
{
    (void)read_proc_refcon;
    (void)src_conn_refcon;

    const MIDIPacket *packet = &packet_list->packet[0];
    for (UInt32 packet_index = 0; packet_index < packet_list->numPackets; ++packet_index) {
        if (g_verbose) {
            print_midi_bytes(stdout, "from CoreMIDI raw", packet->data, packet->length);
        }
        if (g_feedback_log != NULL) {
            print_midi_bytes(g_feedback_log, "from CoreMIDI raw", packet->data, packet->length);
        }

        if (packet->length > 0 && packet->data[0] == 0xf0u) {
            if (should_forward_sysex_to_driver(packet->data, packet->length)) {
                kern_return_t ret = send_debug_midi_bytes(kControlPort, packet->data, packet->length);
                if (g_verbose) {
                    printf("from CoreMIDI sysex len=%u -> driver ret=0x%08x%s\n",
                           packet->length,
                           ret,
                           g_forward_feedback_to_driver ? "" : " (disabled)");
                    fflush(stdout);
                }
                if (ret != KERN_SUCCESS &&
                    ret != kIOReturnNotReady &&
                    g_forward_feedback_to_driver) {
                    fprintf(stderr,
                            "bridge: failed to send MIDI sysex feedback to driver: 0x%08x (%s)\n",
                            ret,
                            mach_error_string(ret));
                }
            } else if (g_verbose) {
                printf("from CoreMIDI sysex len=%u -> driver throttled\n", packet->length);
                fflush(stdout);
            }
        }

        uint8_t running_status = 0;
        UInt16 i = 0;
        while (i < packet->length) {
            uint8_t status = packet->data[i];
            if ((status & 0x80u) != 0) {
                ++i;
                if (status < 0xf0u) {
                    running_status = status;
                } else {
                    running_status = 0;
                }
            } else if (running_status != 0) {
                status = running_status;
            } else {
                ++i;
                continue;
            }

            int length = midi_message_length(status);
            if (length == 0) {
                continue;
            }

            uint8_t data1 = 0;
            uint8_t data2 = 0;
            if (length >= 2) {
                if (i >= packet->length) {
                    break;
                }
                data1 = packet->data[i++];
            }
            if (length >= 3) {
                if (i >= packet->length) {
                    break;
                }
                data2 = packet->data[i++];
            }

            if (!should_forward_short_message_to_driver(status, data1, data2)) {
                continue;
            }

            kern_return_t ret = send_debug_midi(kControlPort, status, data1, data2);
            if (g_verbose) {
                printf("from CoreMIDI msg: %02x %02x %02x -> driver ret=0x%08x%s\n",
                       status,
                       data1,
                       data2,
                       ret,
                       g_forward_feedback_to_driver ? "" : " (disabled)");
                fflush(stdout);
            }
            if (ret != KERN_SUCCESS &&
                ret != kIOReturnNotReady &&
                g_forward_feedback_to_driver) {
                fprintf(stderr,
                        "bridge: failed to send MIDI feedback to driver: 0x%08x (%s)\n",
                        ret,
                        mach_error_string(ret));
            }
        }
        packet = MIDIPacketNext(packet);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--name \"Avid 003 Port 3 (Control)\"] [--poll-ms 5]\n"
            "          [--include-existing] [--verbose] [--feedback-log path]\n"
            "          [--no-driver-feedback]\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *port_name = kDefaultPortName;
    uint32_t poll_ms = 5;
    bool include_existing = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            port_name = argv[++i];
        } else if (strcmp(argv[i], "--poll-ms") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], 1, 1000, &poll_ms)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--include-existing") == 0) {
            include_existing = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "--feedback-log") == 0 && i + 1 < argc) {
            g_feedback_log = fopen(argv[++i], "a");
            if (g_feedback_log == NULL) {
                perror("bridge: failed to open feedback log");
                return 1;
            }
        } else if (strcmp(argv[i], "--no-driver-feedback") == 0) {
            g_forward_feedback_to_driver = false;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    g_probe_service = find_probe_service();
    if (g_probe_service == IO_OBJECT_NULL) {
        fprintf(stderr, "bridge: FireWireOHCIProbe service not found\n");
        return 1;
    }

    MIDIClientRef client = 0;
    MIDIEndpointRef source = 0;
    MIDIEndpointRef destination = 0;

    CFStringRef client_name = CFSTR("Digi 003 Bridge");
    CFStringRef endpoint_name = CFStringCreateWithCString(kCFAllocatorDefault, port_name, kCFStringEncodingUTF8);
    if (endpoint_name == NULL) {
        IOObjectRelease(g_probe_service);
        return 1;
    }

    OSStatus err = MIDIClientCreate(client_name, NULL, NULL, &client);
    if (err == noErr) {
        err = MIDISourceCreate(client, endpoint_name, &source);
    }
    if (err == noErr) {
        err = MIDIDestinationCreate(client, endpoint_name, midi_read_proc, NULL, &destination);
    }
    CFRelease(endpoint_name);

    if (err != noErr) {
        fprintf(stderr, "bridge: CoreMIDI endpoint creation failed: %d\n", (int)err);
        if (source != 0) {
            MIDIEndpointDispose(source);
        }
        if (destination != 0) {
            MIDIEndpointDispose(destination);
        }
        if (client != 0) {
            MIDIClientDispose(client);
        }
        IOObjectRelease(g_probe_service);
        return 1;
    }

    uint64_t last_count = 0;
    uint64_t decoded_count = 0;
    uint32_t recent_index = 0;
    uint32_t recent_count = 0;
    uint32_t messages[kDecodedRecentCapacity] = {0};
    uint32_t missed_polls = 0;
    uint32_t max_missed_polls = 5000u / poll_ms;
    if (max_missed_polls < 10u) {
        max_missed_polls = 10u;
    }

    if (read_u64_property(g_probe_service, CFSTR("ProbeControlDecodedMessageCount"), &decoded_count)) {
        last_count = include_existing ? 0 : decoded_count;
    }

    printf("bridge: created CoreMIDI source/destination \"%s\"\n", port_name);
    printf("bridge: forwarding Digi 003 port-E control messages to V-Control/Pro Tools\n");
    fflush(stdout);

    while (!g_should_stop) {
        decoded_count = 0;
        if (!read_u64_property(g_probe_service, CFSTR("ProbeControlDecodedMessageCount"), &decoded_count) ||
            !read_u32_property(g_probe_service, CFSTR("ProbeControlDecodedRecentIndex"), &recent_index) ||
            !read_u32_property(g_probe_service, CFSTR("ProbeControlDecodedRecentCount"), &recent_count)) {
            missed_polls++;
            if (missed_polls >= max_missed_polls) {
                fprintf(stderr, "bridge: FireWireOHCIProbe service stopped responding; exiting for restart\n");
                break;
            }
            usleep(poll_ms * 1000u);
            continue;
        }
        missed_polls = 0;

        if (decoded_count < last_count) {
            last_count = decoded_count;
        }

        uint64_t delta = decoded_count - last_count;
        if (delta > 0) {
            size_t capacity = read_recent_messages(g_probe_service, messages, kDecodedRecentCapacity);
            size_t count = recent_count;
            if (count > capacity) {
                count = capacity;
            }
            if (delta > count) {
                delta = count;
            }

            size_t ordered[kDecodedRecentCapacity] = {0};
            if (count == capacity && capacity > 0) {
                size_t pos = 0;
                for (size_t i = recent_index; i < capacity; ++i) {
                    ordered[pos++] = i;
                }
                for (size_t i = 0; i < recent_index && pos < count; ++i) {
                    ordered[pos++] = i;
                }
            } else {
                for (size_t i = 0; i < count; ++i) {
                    ordered[i] = i;
                }
            }

            size_t start = count - (size_t)delta;
            for (size_t i = start; i < count; ++i) {
                uint32_t message = messages[ordered[i]];
                if (message == 0) {
                    continue;
                }
                uint8_t port = (message >> 24) & 0x0fu;
                uint8_t status = (message >> 16) & 0xffu;
                uint8_t data1 = (message >> 8) & 0xffu;
                uint8_t data2 = message & 0xffu;
                if (port == kControlPort) {
                    send_coremidi_message(source, status, data1, data2);
                }
            }
            last_count = decoded_count;
        }

        usleep(poll_ms * 1000u);
    }

    MIDIEndpointDispose(source);
    MIDIEndpointDispose(destination);
    MIDIClientDispose(client);
    close_debug_connection();
    if (g_feedback_log != NULL) {
        fclose(g_feedback_log);
    }
    IOObjectRelease(g_probe_service);
    printf("bridge: stopped\n");
    return 0;
}
