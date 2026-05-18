#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    kDebugCommandMidiMessage = 1,
    kDebugCommandFaderTarget = 2,
    kDebugUserClientType = 0x44494749,
    kDebugSelectorMidiMessage = 0,
    kDebugSelectorFaderTarget = 1,
};

static int
parse_u32(const char * text, uint32_t min_value, uint32_t max_value, uint32_t * value)
{
    if (text == NULL || value == NULL) {
        return 0;
    }

    char * end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed < min_value || parsed > max_value) {
        return 0;
    }

    *value = (uint32_t)parsed;
    return 1;
}

static void
set_u32(CFMutableDictionaryRef dict, CFStringRef key, uint32_t value)
{
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    if (number == NULL) {
        return;
    }

    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static kern_return_t
set_u32_property(io_service_t service, CFStringRef key, uint32_t value)
{
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    if (number == NULL) {
        return kIOReturnNoMemory;
    }

    kern_return_t ret = IORegistryEntrySetCFProperty(service, key, number);
    CFRelease(number);
    return ret;
}

static void
usage(const char * prog)
{
    fprintf(stderr,
            "usage:\n"
            "  %s fader <1-8> <0-1023>\n"
            "  %s note <group> <note> <on|off>\n"
            "  %s raw <status> <data1> <data2> [port]\n",
            prog,
            prog,
            prog);
}

static io_service_t
find_probe_service(void)
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

int
main(int argc, char ** argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    CFMutableDictionaryRef dict =
        CFDictionaryCreateMutable(kCFAllocatorDefault,
                                  8,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    if (dict == NULL) {
        fprintf(stderr, "failed to allocate property dictionary\n");
        return 1;
    }
    uint64_t scalar_input[4] = {0, 0, 0, 0};
    uint32_t scalar_input_count = 0;
    uint32_t selector = 0;
    set_u32(dict, CFSTR("ProbeControlDebugSequence"), (uint32_t)time(NULL));

    if (strcmp(argv[1], "fader") == 0) {
        uint32_t channel = 0;
        uint32_t target = 0;
        if (argc != 4 ||
            !parse_u32(argv[2], 1, 8, &channel) ||
            !parse_u32(argv[3], 0, 1023, &target)) {
            usage(argv[0]);
            CFRelease(dict);
            return 2;
        }

        set_u32(dict, CFSTR("ProbeControlDebugCommand"), kDebugCommandFaderTarget);
        set_u32(dict, CFSTR("ProbeControlDebugFaderChannel"), channel);
        set_u32(dict, CFSTR("ProbeControlDebugFaderTarget10"), target);
        selector = kDebugSelectorFaderTarget;
        scalar_input[0] = channel;
        scalar_input[1] = target;
        scalar_input_count = 2;
        printf("queue fader channel=%u target10=%u\n", channel, target);
    } else if (strcmp(argv[1], "note") == 0) {
        uint32_t group = 0;
        uint32_t note = 0;
        if (argc != 5 ||
            !parse_u32(argv[2], 0, 15, &group) ||
            !parse_u32(argv[3], 0, 127, &note) ||
            (strcmp(argv[4], "on") != 0 && strcmp(argv[4], "off") != 0)) {
            usage(argv[0]);
            CFRelease(dict);
            return 2;
        }

        uint32_t velocity = group | (strcmp(argv[4], "on") == 0 ? 0x40u : 0x00u);
        set_u32(dict, CFSTR("ProbeControlDebugCommand"), kDebugCommandMidiMessage);
        set_u32(dict, CFSTR("ProbeControlDebugPort"), 0x0e);
        set_u32(dict, CFSTR("ProbeControlDebugStatus"), 0x90);
        set_u32(dict, CFSTR("ProbeControlDebugData1"), note);
        set_u32(dict, CFSTR("ProbeControlDebugData2"), velocity);
        selector = kDebugSelectorMidiMessage;
        scalar_input[0] = 0x0e;
        scalar_input[1] = 0x90;
        scalar_input[2] = note;
        scalar_input[3] = velocity;
        scalar_input_count = 4;
        printf("queue note group=0x%02x note=0x%02x %s\n", group, note, argv[4]);
    } else if (strcmp(argv[1], "raw") == 0) {
        uint32_t status = 0;
        uint32_t data1 = 0;
        uint32_t data2 = 0;
        uint32_t port = 0x0e;
        if ((argc != 5 && argc != 6) ||
            !parse_u32(argv[2], 0, 255, &status) ||
            !parse_u32(argv[3], 0, 255, &data1) ||
            !parse_u32(argv[4], 0, 255, &data2) ||
            (argc == 6 && !parse_u32(argv[5], 0, 15, &port))) {
            usage(argv[0]);
            CFRelease(dict);
            return 2;
        }

        set_u32(dict, CFSTR("ProbeControlDebugCommand"), kDebugCommandMidiMessage);
        set_u32(dict, CFSTR("ProbeControlDebugPort"), port);
        set_u32(dict, CFSTR("ProbeControlDebugStatus"), status);
        set_u32(dict, CFSTR("ProbeControlDebugData1"), data1);
        set_u32(dict, CFSTR("ProbeControlDebugData2"), data2);
        selector = kDebugSelectorMidiMessage;
        scalar_input[0] = port;
        scalar_input[1] = status;
        scalar_input[2] = data1;
        scalar_input[3] = data2;
        scalar_input_count = 4;
        printf("queue raw port=0x%x status=0x%02x data1=0x%02x data2=0x%02x\n",
               port,
               status,
               data1,
               data2);
    } else {
        usage(argv[0]);
        CFRelease(dict);
        return 2;
    }

    io_service_t service = find_probe_service();
    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "FireWireOHCIProbe service not found\n");
        CFRelease(dict);
        return 1;
    }

    io_connect_t connect = IO_OBJECT_NULL;
    kern_return_t ret = IOServiceOpen(service, mach_task_self(), kDebugUserClientType, &connect);
    if (ret == KERN_SUCCESS) {
        ret = IOConnectCallScalarMethod(connect,
                                        selector,
                                        scalar_input,
                                        scalar_input_count,
                                        NULL,
                                        NULL);
        IOServiceClose(connect);
    }
    if (ret != KERN_SUCCESS) {
        ret = IORegistryEntrySetCFProperties(service, dict);
    }
    if (ret == KERN_SUCCESS) {
        if (selector == kDebugSelectorFaderTarget) {
            ret = set_u32_property(service, CFSTR("ProbeControlDebugCommand"), kDebugCommandFaderTarget);
            if (ret == KERN_SUCCESS) {
                ret = set_u32_property(service, CFSTR("ProbeControlDebugFaderChannel"), (uint32_t)scalar_input[0]);
            }
            if (ret == KERN_SUCCESS) {
                ret = set_u32_property(service, CFSTR("ProbeControlDebugFaderTarget10"), (uint32_t)scalar_input[1]);
            }
        } else {
            ret = set_u32_property(service, CFSTR("ProbeControlDebugCommand"), kDebugCommandMidiMessage);
            if (ret == KERN_SUCCESS) {
                ret = set_u32_property(service, CFSTR("ProbeControlDebugPort"), (uint32_t)scalar_input[0]);
            }
            if (ret == KERN_SUCCESS) {
                ret = set_u32_property(service, CFSTR("ProbeControlDebugStatus"), (uint32_t)scalar_input[1]);
            }
            if (ret == KERN_SUCCESS) {
                ret = set_u32_property(service, CFSTR("ProbeControlDebugData1"), (uint32_t)scalar_input[2]);
            }
            if (ret == KERN_SUCCESS) {
                ret = set_u32_property(service, CFSTR("ProbeControlDebugData2"), (uint32_t)scalar_input[3]);
            }
        }
    }
    IOObjectRelease(service);
    CFRelease(dict);

    if (ret != KERN_SUCCESS) {
        fprintf(stderr, "debug send failed: 0x%08x (%s)\n",
                ret,
                mach_error_string(ret));
        return 1;
    }

    return 0;
}
