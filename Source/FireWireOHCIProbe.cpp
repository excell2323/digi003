#include <stdio.h>
#include <os/log.h>

#include <AudioDriverKit/AudioDriverKit.h>
#include <AudioDriverKit/IOUserAudioDevice.h>
#include <AudioDriverKit/IOUserAudioDriver.h>
#include <AudioDriverKit/IOUserAudioStream.h>
#include <DriverKit/IODispatchQueue.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/IOInterruptDispatchSource.h>
#include <DriverKit/IODMACommand.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IOUserServer.h>
#include <DriverKit/OSAction.h>
#include <DriverKit/OSData.h>
#include <DriverKit/OSDictionary.h>
#include <DriverKit/OSNumber.h>
#include <DriverKit/OSSharedPtr.h>
#include <DriverKit/OSString.h>
#include <PCIDriverKit/IOPCIFamilyDefinitions.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include "FireWireOHCIProbe.h"

namespace
{
using namespace AudioDriverKit;

constexpr uint8_t kBAR0 = 0;
constexpr uint64_t kOhciVersionOffset = 0x00;
constexpr uint64_t kOhciGuidRomOffset = 0x04;
constexpr uint64_t kOhciAtRetriesOffset = 0x08;
constexpr uint64_t kOhciConfigRomHeaderOffset = 0x18;
constexpr uint64_t kOhciBusIdOffset = 0x1c;
constexpr uint64_t kOhciBusOptionsOffset = 0x20;
constexpr uint64_t kOhciGuidHiOffset = 0x24;
constexpr uint64_t kOhciGuidLoOffset = 0x28;
constexpr uint64_t kOhciConfigRomMapOffset = 0x34;
constexpr uint64_t kOhciVendorIdOffset = 0x40;
constexpr uint64_t kOhciHcControlSetOffset = 0x50;
constexpr uint64_t kOhciHcControlClearOffset = 0x54;
constexpr uint64_t kOhciSelfIdBufferOffset = 0x64;
constexpr uint64_t kOhciSelfIdCountOffset = 0x68;
constexpr uint64_t kOhciIntEventSetOffset = 0x80;
constexpr uint64_t kOhciIntEventClearOffset = 0x84;
constexpr uint64_t kOhciIntMaskSetOffset = 0x88;
constexpr uint64_t kOhciIntMaskClearOffset = 0x8c;
constexpr uint64_t kOhciInitialBandwidthAvailableOffset = 0xb0;
constexpr uint64_t kOhciInitialChannelsAvailableHiOffset = 0xb4;
constexpr uint64_t kOhciInitialChannelsAvailableLoOffset = 0xb8;
constexpr uint64_t kOhciFairnessControlOffset = 0xdc;
constexpr uint64_t kOhciLinkControlSetOffset = 0xe0;
constexpr uint64_t kOhciLinkControlClearOffset = 0xe4;
constexpr uint64_t kOhciNodeIdOffset = 0xe8;
constexpr uint64_t kOhciPhyControlOffset = 0xec;
constexpr uint64_t kOhciIsochronousCycleTimerOffset = 0xf0;
constexpr uint64_t kOhciAsReqFilterHiSetOffset = 0x100;
constexpr uint64_t kOhciAsReqFilterLoSetOffset = 0x108;
constexpr uint64_t kOhciPhyReqFilterHiSetOffset = 0x110;
constexpr uint64_t kOhciPhyReqFilterLoSetOffset = 0x118;
constexpr uint64_t kOhciPhyUpperBoundOffset = 0x120;
constexpr uint64_t kOhciAsReqTrContextControlSetOffset = 0x180;
constexpr uint64_t kOhciAsReqTrContextControlClearOffset = 0x184;
constexpr uint64_t kOhciAsReqTrCommandPtrOffset = 0x18c;
constexpr uint64_t kOhciAsReqRcvContextControlSetOffset = 0x1c0;
constexpr uint64_t kOhciAsReqRcvContextControlClearOffset = 0x1c4;
constexpr uint64_t kOhciAsReqRcvCommandPtrOffset = 0x1cc;
constexpr uint64_t kOhciAsRspRcvContextControlSetOffset = 0x1e0;
constexpr uint64_t kOhciAsRspRcvContextControlClearOffset = 0x1e4;
constexpr uint64_t kOhciAsRspRcvCommandPtrOffset = 0x1ec;
constexpr uint64_t kOhciIsoXmitIntEventSetOffset = 0x090;
constexpr uint64_t kOhciIsoXmitIntEventClearOffset = 0x094;
constexpr uint64_t kOhciIsoXmitIntMaskSetOffset = 0x098;
constexpr uint64_t kOhciIsoXmitIntMaskClearOffset = 0x09c;
constexpr uint64_t kOhciIsoRecvIntEventSetOffset = 0x0a0;
constexpr uint64_t kOhciIsoRecvIntEventClearOffset = 0x0a4;
constexpr uint64_t kOhciIsoRecvIntMaskSetOffset = 0x0a8;
constexpr uint64_t kOhciIsoRecvIntMaskClearOffset = 0x0ac;

constexpr uint32_t kOhciHcControlBIBImageValid = 0x80000000;
constexpr uint32_t kOhciHcControlNoByteSwapData = 0x40000000;
constexpr uint32_t kOhciHcControlProgramPhyEnable = 0x00800000;
constexpr uint32_t kOhciHcControlAPhyEnhanceEnable = 0x00400000;
constexpr uint32_t kOhciHcControlLPS = 0x00080000;
constexpr uint32_t kOhciHcControlPostedWriteEnable = 0x00040000;
constexpr uint32_t kOhciHcControlLinkEnable = 0x00020000;
constexpr uint32_t kOhciHcControlSoftReset = 0x00010000;

constexpr uint32_t kOhciLinkControlReceiveSelfID = 1u << 9;
constexpr uint32_t kOhciLinkControlReceivePhyPacket = 1u << 10;
constexpr uint32_t kOhciLinkControlCycleTimerEnable = 1u << 20;
constexpr uint32_t kOhciLinkControlCycleMaster = 1u << 21;

constexpr uint32_t kOhciEventReqTxComplete = 0x00000001;
constexpr uint32_t kOhciEventRespTxComplete = 0x00000002;
constexpr uint32_t kOhciEventAsyncReqRcv = 0x00000004;
constexpr uint32_t kOhciEventAsyncRspRcv = 0x00000008;
constexpr uint32_t kOhciEventReqPacket = 0x00000010;
constexpr uint32_t kOhciEventRspPacket = 0x00000020;
constexpr uint32_t kOhciEventIsochTx = 0x00000040;
constexpr uint32_t kOhciEventIsochRx = 0x00000080;
constexpr uint32_t kOhciEventSelfIDComplete = 0x00010000;
constexpr uint32_t kOhciEventBusReset = 0x00020000;
constexpr uint32_t kOhciEventRegisterAccessFail = 0x00040000;
constexpr uint32_t kOhciEventPhy = 0x00080000;
constexpr uint32_t kOhciEventCycleSynch = 0x00100000;
constexpr uint32_t kOhciEventCycleLost = 0x00400000;
constexpr uint32_t kOhciEventCycleInconsistent = 0x00800000;
constexpr uint32_t kOhciEventUnrecoverableError = 0x01000000;
constexpr uint32_t kOhciEventCycleTooLong = 0x02000000;
constexpr uint32_t kOhciEventPhyRegReceived = 0x04000000;
constexpr uint32_t kOhciEventMasterIntEnable = 0x80000000;
constexpr uint32_t kOhciAsyncInterruptMask = kOhciEventReqTxComplete |
                                             kOhciEventRespTxComplete |
                                             kOhciEventAsyncReqRcv |
                                             kOhciEventAsyncRspRcv |
                                             kOhciEventReqPacket |
                                             kOhciEventRspPacket;
constexpr uint32_t kOhciIsochInterruptMask = kOhciEventIsochTx | kOhciEventIsochRx;
constexpr uint32_t kOhciDiagnosticInterruptMask = kOhciAsyncInterruptMask |
                                                 kOhciEventSelfIDComplete |
                                                 kOhciEventBusReset |
                                                 kOhciEventRegisterAccessFail |
                                                 kOhciEventPhy |
                                                 kOhciEventCycleSynch |
                                                 kOhciEventCycleLost |
                                                 kOhciEventCycleInconsistent |
                                                 kOhciEventUnrecoverableError |
                                                 kOhciEventCycleTooLong |
                                                 kOhciEventPhyRegReceived |
                                                 kOhciEventMasterIntEnable;

constexpr uint32_t kOhciPhyControlReadDone = 0x80000000;
constexpr uint32_t kOhciPhyControlWritePending = 0x00004000;
constexpr uint32_t kPhyLinkActive = 0x80;
constexpr uint32_t kPhyContender = 0x40;
constexpr uint32_t kPhyBusReset = 0x40;
constexpr uint32_t kPhyExtendedRegisters = 0xe0;
constexpr uint32_t kPhyBusShortReset = 0x40;
constexpr uint32_t kPhyInterruptStatusBits = 0x3c;
constexpr uint32_t kPhyEnableAccelerated = 0x02;
constexpr uint32_t kPhyEnableMulti = 0x01;
constexpr uint32_t kPhyPageSelect = 0xe0;
constexpr uint8_t kPhyPage1394A = 1;

constexpr uint16_t kDescriptorOutputLast = 1u << 12;
constexpr uint16_t kDescriptorInputMore = 2u << 12;
constexpr uint16_t kDescriptorInputLast = 3u << 12;
constexpr uint16_t kDescriptorStatus = 1u << 11;
constexpr uint16_t kDescriptorKeyImmediate = 2u << 8;
constexpr uint16_t kDescriptorIrqAlways = 3u << 4;
constexpr uint16_t kDescriptorBranchAlways = 3u << 2;
constexpr uint32_t kContextRun = 0x00008000;
constexpr uint32_t kContextWake = 0x00001000;
constexpr uint32_t kContextActive = 0x00000400;
constexpr uint32_t kIrContextIsochHeader = 0x40000000;
constexpr uint32_t kDescriptorEventMask = 0x1f;
constexpr uint32_t kDescriptorEventAckPending = 0x12;

constexpr uint32_t kTCodeReadQuadletRequest = 0x4;
constexpr uint32_t kTCodeReadQuadletResponse = 0x6;
constexpr uint32_t kTCodeWriteQuadletRequest = 0x0;
constexpr uint32_t kTCodeWriteResponse = 0x2;
constexpr uint32_t kTCodeStreamData = 0xa;
constexpr uint32_t kRetryX = 0x1;
constexpr uint32_t kSCode400 = 0x2;
constexpr uint32_t kOhciCycleCountPerSecond = 8000;
constexpr uint32_t kOhciCycleCountModulus = 8 * kOhciCycleCountPerSecond;
constexpr uint32_t kOhciContextTimestampMask = 0xffff;
constexpr uint64_t kCSRRegisterBase = 0xfffff0000000ull;
constexpr uint64_t kCSRConfigROM = 0x400ull;
constexpr uint64_t kDigi00xRegisterBase = 0xffffe0000000ull;
constexpr uint64_t kDigi00xOffsetStreamingState = 0x0000ull;
constexpr uint64_t kDigi00xOffsetStreamingSet = 0x0004ull;
constexpr uint64_t kDigi00xOffsetMessageAddress = 0x0014ull;
constexpr uint64_t kDigi00xOffsetIsocChannels = 0x0100ull;
constexpr uint64_t kDigi00xOffsetLocalRate = 0x0110ull;
constexpr uint64_t kDigi00xOffsetExternalRate = 0x0114ull;
constexpr uint64_t kDigi00xOffsetClockSource = 0x0118ull;
constexpr uint64_t kDigi00xOffsetOpticalInterfaceMode = 0x011cull;
constexpr uint64_t kDigi00xOffsetMonitorEnable = 0x0124ull;
constexpr uint64_t kDigi00xOffsetDetectExternal = 0x012cull;
constexpr uint32_t kAsyncReadTLabel = 1;
constexpr uint32_t kAsyncRxDescriptorCount = 2;
constexpr uint32_t kAsyncRxDataSize = 4096;
constexpr uint32_t kAsyncRxDataOffset0 = 4096;
constexpr uint32_t kAsyncRxDataOffset1 = 8192;
constexpr uint32_t kAsyncRxBufferSize = 12288;
constexpr uint32_t kAsyncTxBufferSize = 4096;
constexpr uint32_t kAsyncReadAttemptCount = 6;
constexpr uint32_t kAsyncReadWaitLoopsPerAttempt = 250;
constexpr uint32_t kAsyncReadWaitMilliseconds = 10;
constexpr uint32_t kAsyncReadRetrySettleMilliseconds = 1000;
constexpr uint32_t kDigiLiveAsyncAttemptCount = 2;
constexpr uint32_t kDigiLiveAsyncWaitLoopsPerAttempt = 25;
constexpr uint32_t kDigiLiveAsyncRetrySettleMilliseconds = 100;
constexpr size_t kConfigROMProbeReadCount = 32;
constexpr size_t kConfigROMProbeDetailedCount = 16;
constexpr size_t kDigi00xRegisterProbeCount = 10;
constexpr size_t kDigi00xWritePlanCount = 5;
constexpr size_t kDigi00xStateSequenceStepCount = 8;
constexpr size_t kDigi00xDuplexStepCount = 9;
constexpr uint32_t kIsoTestEnabled = 1;
constexpr uint32_t kIsoTestContextIndex = 0;
constexpr uint32_t kIsoTestChannel = 63;
constexpr uint64_t kIsoTestBufferSize = 4096;
constexpr uint32_t kIsoTestITDescriptorOffset = 0;
constexpr uint32_t kIsoTestIRDescriptorOffset = 64;
constexpr uint32_t kIsoTestIRDataOffset = 256;
constexpr uint32_t kIsoTestIRDataSize = 512;
constexpr uint32_t kIsoTestRunReceiveEnabled = 1;
constexpr uint32_t kIsoTestRunReceiveMilliseconds = 250;
constexpr uint32_t kIsoTestRunTransmitEnabled = 1;
constexpr uint32_t kIsoTestRunTransmitMilliseconds = 100;
constexpr uint32_t kDigi00xDuplexProbeEnabled = 1;
constexpr uint32_t kDigi00xDuplexDeviceTransmitChannel = 2;
constexpr uint32_t kDigi00xDuplexDeviceReceiveChannel = 0;
constexpr uint32_t kDigi00xDuplexContextIndex = 0;
constexpr uint64_t kDigi00xDuplexBufferSize = 0xc00000;
constexpr uint32_t kDigi00xDuplexDataBlockQuadlets = 19;
constexpr uint32_t kDigi00xDuplexDataBlockBytes =
    kDigi00xDuplexDataBlockQuadlets * sizeof(uint32_t);
constexpr uint32_t kDigi00xDuplexPCMAudioChannels = 18;
constexpr uint32_t kDigiLiveMidiRecentMessageCount = 256;
constexpr uint32_t kDigiLiveMidiRecentIndexedPublishCount = 16;
constexpr uint32_t kDigiLiveMidiDecodedRecentMessageCount = 128;
constexpr uint32_t kDigiLiveMidiDecodedRecentIndexedPublishCount = 16;
constexpr uint32_t kDigiLiveMidiPortCount = 16;
constexpr uint32_t kDigiLiveMidiBytesPerMessage = 3;
constexpr uint32_t kDigiLiveMidiControlPortNibble = 0x0e;
constexpr uint32_t kDigiLiveMidiRawFragmentEchoEnabled = 0;
constexpr uint32_t kDigiLiveMidiDecodedFeedbackEnabled = 1;
constexpr uint32_t kDigiLiveMidiEchoToOutputEnabled = 1;
constexpr uint32_t kDigiLiveMidiEchoConsolePortOnlyEnabled = 1;
constexpr uint32_t kDigiLiveMidiEchoQueueSize = 4096;
constexpr uint32_t kDigiLiveMidiEchoFaderMoveFeedbackEnabled = 1;
constexpr uint32_t kDigiLiveMidiEchoFaderMoveFeedbackStride = 4;
constexpr uint32_t kDigiLiveMidiEchoFaderMoveFeedbackMinDelta = 16;
constexpr uint32_t kDigiLiveMidiEchoFaderMoveFeedbackFlushOnTouchRelease = 1;
constexpr uint32_t kDigiLiveControlChannelStripCount = 8;
constexpr uint32_t kDigiLiveControlKindUnknown = 0;
constexpr uint32_t kDigiLiveControlKindChannelSelect = 1;
constexpr uint32_t kDigiLiveControlKindChannelSolo = 2;
constexpr uint32_t kDigiLiveControlKindChannelMute = 3;
constexpr uint32_t kDigiLiveControlKindChannelFaderTouch = 4;
constexpr uint32_t kDigiLiveControlKindChannelFaderMove = 5;
constexpr uint32_t kDigiLiveControlKindStop = 6;
constexpr uint32_t kDigiLiveControlKindPlay = 7;
constexpr uint32_t kDigiLiveControlKindTransportRTZ = 8;
constexpr uint32_t kDigiLiveControlKindTransportRewind = 9;
constexpr uint32_t kDigiLiveControlKindTransportFastForward = 10;
constexpr uint32_t kDigiLiveControlKindTransportRecord = 11;
constexpr uint32_t kDigiLiveControlKindArrowLeft = 12;
constexpr uint32_t kDigiLiveControlKindArrowRight = 13;
constexpr uint32_t kDigiLiveControlKindArrowUp = 14;
constexpr uint32_t kDigiLiveControlKindArrowDown = 15;
constexpr uint32_t kDigiLiveControlKindJogWheel = 16;
constexpr uint32_t kDigiLiveControlKindShuttle = 17;
constexpr uint32_t kDigiLiveControlKindModeViewButton = 18;
constexpr uint32_t kDigiLiveControlKindEncoderAssignButton = 19;
constexpr uint32_t kDigiLiveControlKindRotaryEncoder = 20;
constexpr uint32_t kDigiLiveControlKindAboveTransportButton = 21;
constexpr uint32_t kDigiLiveControlKindTransportSectionButton = 22;
constexpr uint32_t kDigiLiveControlKindHardwareMonitorButton = 23;
constexpr uint32_t kDigiLiveControlKindDisplayModeButton = 24;
constexpr uint32_t kDigiLiveControlNoteChannelSelect = 0x00;
constexpr uint32_t kDigiLiveControlNoteChannelSolo = 0x01;
constexpr uint32_t kDigiLiveControlNoteChannelMute = 0x02;
constexpr uint32_t kDigiLiveControlNoteChannelFaderTouch = 0x03;
constexpr uint32_t kDigiLiveControlNoteArrowLeft = 0x05;
constexpr uint32_t kDigiLiveControlNoteArrowRight = 0x06;
constexpr uint32_t kDigiLiveControlNoteArrowUp = 0x07;
constexpr uint32_t kDigiLiveControlNoteArrowDown = 0x08;
constexpr uint32_t kDigiLiveControlNoteNavModeBank = 0x02;
constexpr uint32_t kDigiLiveControlNoteNavModeNudge = 0x03;
constexpr uint32_t kDigiLiveControlNoteNavModeZoom = 0x04;
constexpr uint32_t kDigiLiveControlNoteTransportRTZ = 0x06;
constexpr uint32_t kDigiLiveControlNoteTransportRewind = 0x07;
constexpr uint32_t kDigiLiveControlNoteTransportFastForward = 0x08;
constexpr uint32_t kDigiLiveControlNoteStop = 0x09;
constexpr uint32_t kDigiLiveControlNotePlay = 0x0a;
constexpr uint32_t kDigiLiveControlNoteTransportRecord = 0x0b;
constexpr uint32_t kDigiLiveControlGroupNavigation = 0x0d;
constexpr uint32_t kDigiLiveControlGroupTransport = 0x0e;
constexpr uint32_t kDigiLiveControlGroupModeView = 0x0a;
constexpr uint32_t kDigiLiveControlGroupEncoderAssign = 0x0b;
constexpr uint32_t kDigiLiveControlGroupAboveTransport = 0x0c;
constexpr uint32_t kDigiLiveControlGroupHardwareMonitor = 0x0f;
constexpr uint32_t kDigiLiveControlDisplayModeGroup = 0x0b;
constexpr uint32_t kDigiLiveControlDisplayModeNote = 0x08;
constexpr uint32_t kDigiLiveControlModeViewButtonCount = 20;
constexpr uint32_t kDigiLiveControlModeViewFirstNote = 0x00;
constexpr uint32_t kDigiLiveControlEncoderAssignButtonCount = 8;
constexpr uint32_t kDigiLiveControlAboveTransportButtonCount = 32;
constexpr uint32_t kDigiLiveControlTransportSectionGroupCount = 2;
constexpr uint32_t kDigiLiveControlTransportSectionSlotsPerGroup = 16;
constexpr uint32_t kDigiLiveControlTransportSectionButtonCount =
    kDigiLiveControlTransportSectionGroupCount *
    kDigiLiveControlTransportSectionSlotsPerGroup;
constexpr uint32_t kDigiLiveControlHardwareMonitorButtonCount = 16;
constexpr uint32_t kDigiLiveControlRotaryEncoderCount = 8;
constexpr uint32_t kDigiLiveControlCCRotaryEncoderFirst = 0x40;
constexpr uint32_t kDigiLiveControlCCJogWheel = 0x4e;
constexpr uint32_t kDigiLiveControlCCShuttle = 0x5e;
constexpr uint32_t kDigiLiveControlMotorTestEnabled = 1;
constexpr uint32_t kDigiLiveControlMotorTestLowTarget10 = 256;
constexpr uint32_t kDigiLiveControlMotorTestHighTarget10 = 768;
constexpr uint32_t kDigiLiveControlDebugCommandMidiMessage = 1;
constexpr uint32_t kDigiLiveControlDebugCommandFaderTarget = 2;
constexpr uint32_t kDigiLiveControlDebugCommandMidiBytes = 3;
constexpr uint32_t kFireWireOHCIProbeDebugUserClientType = 0x44494749;
constexpr uint64_t kDigiLiveControlDebugSelectorMidiMessage = 0;
constexpr uint64_t kDigiLiveControlDebugSelectorFaderTarget = 1;
constexpr uint64_t kDigiLiveControlDebugSelectorMidiBytes = 2;
constexpr uint32_t kDigiLiveControlDebugMaxByteMessageLength = 512;
constexpr uint32_t kDigi00xDuplexSampleRate44100 = 44100;
constexpr uint32_t kDigi00xDuplexSampleRate48000 = 48000;
constexpr uint32_t kDigi00xDuplexDefaultSampleRate = kDigi00xDuplexSampleRate48000;
constexpr uint32_t kDigi00xDuplexLocalRateIndex44100 = 0;
constexpr uint32_t kDigi00xDuplexLocalRateIndex48000 = 1;
constexpr uint32_t kDigi00xDuplexCIPSFC44100 = 1;
constexpr uint32_t kDigi00xDuplexCIPSFC48000 = 2;
constexpr uint32_t kDigi00xCIPDBCMask = 0x000000ff;
constexpr uint32_t kDigi00xCIPSYTMask = 0x0000ffff;
constexpr uint32_t kDigi00xCIPFMTAM824 = 0x10;
constexpr uint32_t kDigi00xDuplexITDescriptorOffset = 0x160000;
// 20480 packets are 256 full 44.1 kHz cadence periods: phase and DBC both wrap cleanly.
constexpr uint32_t kDigi00xDuplexITPacketCount = 20480;
constexpr uint32_t kDigi00xDuplexITDescriptorsPerPacket = 4;
constexpr uint32_t kDigiLiveITMaxDataBlocksPerPacket = 6;
constexpr uint32_t kDigiLiveITPayloadStrideBytes =
    kDigiLiveITMaxDataBlocksPerPacket * kDigi00xDuplexDataBlockBytes;
constexpr uint32_t kDigi00xDuplexIRDescriptorOffset = 0;
constexpr uint32_t kDigi00xDuplexIRDescriptorCount = 2048;
constexpr uint32_t kDigi00xDuplexIRDescriptorsPerPacketStorage = 3;
constexpr uint32_t kDigi00xDuplexIRDescriptorBranchCount = 2;
constexpr uint32_t kDigi00xDuplexIRHeaderSize = 16;
constexpr uint32_t kDigi00xDuplexIRDescriptorDataSize =
    8 * kDigi00xDuplexDataBlockBytes;
constexpr uint32_t kDigiLiveSingleDescriptorReceiveEnabled = 0;
constexpr uint32_t kDigiLiveReceiveIRQInterval = 8;
constexpr uint32_t kOHCIInterruptDispatchEnabled = 0;
constexpr uint32_t kDigiLiveRequireIREventBeforeSync = 1;
constexpr uint32_t kDigiLiveIREventGateMissBypassCount = 4;
constexpr uint32_t kDigiLiveIRCommandPtrCatchUpEnabled = 1;
constexpr uint32_t kDigiLiveIRCommandPtrCatchUpMinPackets = 8;
constexpr uint32_t kDigiLiveIRCommandPtrCatchUpScanEnabled = 1;
constexpr uint32_t kDigiLiveIRCommandPtrCatchUpScanMaxPackets = 256;
constexpr uint32_t kDigiLiveIRSegmentCatchUpEnabled = 0;
constexpr uint32_t kDigiLiveIRSegmentCatchUpPacketCount = 8;
constexpr uint32_t kDigiLiveIRSegmentCatchUpFallbackToSinglePacket = 1;
constexpr uint32_t kDigiLiveReceiveFullBufferSyncEnabled = 1;
constexpr uint32_t kDigiLiveReceivePayloadRangeSyncEnabled = 0;
constexpr uint32_t kDigiLiveSingleIRDescriptorDataSize =
    kDigi00xDuplexIRHeaderSize + kDigi00xDuplexIRDescriptorDataSize;
constexpr uint32_t kDigiLiveIRDescriptorDataStride =
    kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? kDigiLiveSingleIRDescriptorDataSize
        : kDigi00xDuplexIRDescriptorDataSize;
constexpr uint32_t kDigi00xDuplexITHeaderStorageOffset = 0x2a0000;
constexpr uint32_t kDigi00xDuplexITPayloadOffset = 0x2d0000;
constexpr uint32_t kDigi00xDuplexIRDataOffset = 0x20000;
constexpr uint32_t kDigi00xDuplexIRDataSize =
    kDigi00xDuplexIRDescriptorCount * kDigiLiveIRDescriptorDataStride;
constexpr uint32_t kDigiLiveReceiveSyncSize = kDigi00xDuplexIRDataOffset + kDigi00xDuplexIRDataSize;
constexpr uint32_t kDigi00xDuplexRunMilliseconds = 500;
constexpr size_t kDigi00xDuplexIRSamplePacketCount = 8;
constexpr size_t kDigi00xDuplexIRSampleHeaderWordCount = 4;
constexpr size_t kDigi00xDuplexIRSamplePayloadWordCount = 8;
constexpr size_t kDigi00xDuplexIRChannelSampleMaxDataBlocks = 6;
constexpr size_t kDigi00xDuplexIRChannelSampleChannelCount = kDigi00xDuplexPCMAudioChannels;
constexpr size_t kDigi00xDuplexIRCaptureSummaryChannelCount = kDigi00xDuplexPCMAudioChannels;
constexpr size_t kDigi00xDuplexIRCapturePCMFrameLimit = 8192;
constexpr size_t kDigi00xDuplexIRCapturePCMChannelCount = 8;
constexpr uint32_t kDigi00xDuplexAM824AudioLabel = 0x40;
constexpr uint32_t kAudioDeviceZeroTimestampPeriod = 1024;
constexpr uint32_t kAudioInputBufferFrameCount = 8192;
constexpr uint32_t kAudioRingBufferFrameCount = 65536;
constexpr uint32_t kAudioOutputStreamEnabled = 1;
constexpr uint32_t kAudioOutputBufferFrameCount = 1024;
constexpr uint32_t kAudioOutputRingBufferFrameCount = 65536;
constexpr uint32_t kAudioOutputChannelCount = 8;
constexpr uint32_t kAudioOutputBufferOffsetMode = 1;
constexpr uint32_t kAudioOutputRingPrebufferFrames = 3072;
constexpr uint32_t kAudioOutputRingKeepFrames = 0;
constexpr uint32_t kAudioRuntimeCallbackRestartEnabled = 1;
constexpr uint32_t kAudioRuntimeRestartReasonInputCallback = 1;
constexpr uint32_t kAudioRuntimeRestartReasonOutputCallback = 2;
constexpr uint32_t kAudioRuntimeRestartReasonPowerOn = 3;
constexpr uint32_t kAudioRefreshStopWaitLoopLimit = 1000;
constexpr uint32_t kAudioDirectInputBufferEnabled = 0;
constexpr uint32_t kAudioCallbackHarvestEnabled = 1;
constexpr uint32_t kAudioCallbackHarvestLowWaterFrames = 8192;
constexpr uint32_t kAudioCallbackHarvestMaxAttempts = 4;
constexpr uint32_t kDigiLiveIREventLowWaterBypassEnabled = 0;
constexpr uint32_t kDigiLiveIREventLowWaterBypassFrames = 16384;
constexpr uint32_t kDigiLiveHarvestMaxDescriptorsPerPass = 512;
constexpr uint32_t kDigiLiveHarvestSleepMilliseconds = 1;
constexpr uint32_t kDigiLiveIdleSleepMilliseconds = 1;
constexpr uint32_t kDigiLiveWorkerPublishInterval = 512;
constexpr uint32_t kDigiLiveIREventPollingEnabled = 1;
constexpr uint32_t kDigiLiveWorkerLowWaterFrames = 8192;
constexpr uint32_t kDigiLivePrebufferTargetFrames = 8192;
constexpr uint32_t kDigiLivePrebufferAttemptCount = 2000;
constexpr uint32_t kDigiLiveSequenceReplayEnabled = 0;
constexpr uint32_t kDigiLiveSequenceReplayPeriodPackets = 80;
constexpr uint32_t kDigiLiveSequenceReplayRequireContinuity = 0;
constexpr uint32_t kDigiLiveSequenceReplayMovingEnabled = 1;
constexpr uint32_t kDigiLiveSequenceReplayMovingDryRunEnabled = 1;
constexpr uint32_t kDigiLiveSequenceReplayMovingRequireContinuity = 0;
constexpr uint32_t kDigiLiveSequenceReplayMovingUseCadencePhase = 1;
constexpr uint32_t kDigiLiveSequenceReplayMovingLearnCadenceFromQueue = 1;
constexpr uint32_t kDigiLiveSequenceReplayMovingAllowCachedCadencePhase = 1;
constexpr uint32_t kDigiLiveSequenceReplayMovingRequireCadenceMismatchZero = 0;
constexpr uint32_t kDigiLiveSequenceReplayMovingLiveWriteGuardEnabled = 1;
constexpr uint32_t kDigiLiveSequenceReplayMovingGuardMinStartDistancePackets = 4096;
constexpr uint32_t kDigiLiveSequenceReplayMovingQueuePackets = 512;
constexpr uint32_t kDigiLiveSequenceReplayMovingUpdatePackets = 80;
constexpr uint32_t kDigiLiveSequenceReplayMovingLeadPackets = 4096;
constexpr uint32_t kDigiLiveOutputPayloadUpdateEnabled = 1;
constexpr uint32_t kDigiLiveOutputLeadPackets = 1024;
constexpr uint32_t kDigiLiveOutputServiceAheadPackets = 256;
constexpr uint32_t kDigiLiveOutputSilenceAheadPackets = 1024;
constexpr uint32_t kDigiLiveOutputMaxPacketsPerPush = 512;
constexpr uint32_t kDigiLiveOutputStopSilencePushCount = 2;
constexpr uint32_t kDigiLiveRxCadencePeriodPackets = 80;
constexpr uint32_t kDigiLiveStateStopped = 0;
constexpr uint32_t kDigiLiveStateStarting = 1;
constexpr uint32_t kDigiLiveStateRunning = 2;
constexpr uint32_t kDigiLiveStateStopping = 3;
constexpr uint32_t kAudioInputBytesPerSample = sizeof(int32_t);
constexpr uint32_t kAudioInputBytesPerFrame =
    kDigi00xDuplexIRCapturePCMChannelCount * kAudioInputBytesPerSample;
constexpr uint64_t kAudioInputBufferBytes =
    static_cast<uint64_t>(kAudioInputBufferFrameCount) * kAudioInputBytesPerFrame;
constexpr uint32_t kAudioOutputBytesPerSample = sizeof(int32_t);
constexpr uint32_t kAudioOutputBytesPerFrame =
    kAudioOutputChannelCount * kAudioOutputBytesPerSample;
constexpr uint64_t kAudioOutputBufferBytes =
    static_cast<uint64_t>(kAudioOutputBufferFrameCount) * kAudioOutputBytesPerFrame;
constexpr uint32_t kDigi00xWritePlanEnabled = 0;
constexpr uint32_t kDigi00xNoopWriteEnabled = 0;
constexpr uint32_t kDigi00xStateWriteEnabled = 0;
constexpr uint32_t kDigi00xStateSequenceEnabled = 0;
constexpr uint32_t kFastKnownDigi003InitEnabled = 1;

constexpr uint32_t kOhciMaxATRequestRetries = 0xf;
constexpr uint32_t kOhciMaxATResponseRetries = 0x2;
constexpr uint32_t kOhciMaxPhysicalResponseRetries = 0x8;
constexpr uint32_t kOhciATRetries =
    kOhciMaxATRequestRetries |
    (kOhciMaxATResponseRetries << 4) |
    (kOhciMaxPhysicalResponseRetries << 8) |
    (200u << 16);

constexpr uint64_t kSelfIDBufferSize = 4096;
constexpr uint64_t kConfigROMBufferSize = 4096;
constexpr uint32_t kConfigROMQuadletCount = 7;
constexpr uint32_t kConfigROMGeneration = 2;
constexpr uint32_t kConfigROMMaxROM = 2;
constexpr uint32_t kConfigROMNodeCapabilities = 0x0c0083c0;
constexpr uint32_t kConfigROMBusInfoLength = 4;
constexpr uint32_t kConfigROMCRCLength = 4;
constexpr size_t kSelfIDWordCount = 16;
constexpr size_t kPhyRegisterCount = 16;
constexpr size_t kPhyPortCount = 3;
constexpr size_t kShortResetAttemptCount = 4;
constexpr uint32_t kInitialAdapterSettleMilliseconds = 2500;
constexpr uint32_t kPostSelfIDSettleMilliseconds = 3000;
constexpr uint32_t kShortResetWaitAttempts = 120;
constexpr uint32_t kShortResetWaitMilliseconds = 50;
constexpr uint32_t kShortResetSettleMilliseconds = 250;

struct RegisterSnapshot
{
    const char * propertyName;
    uint64_t offset;
    uint32_t value;
};

struct DMABuffer
{
    IOBufferMemoryDescriptor * memory;
    IODMACommand * command;
    IOMemoryMap * cpuMap;
    IOAddressSegment cpuRange;
    IOAddressSegment dmaSegment;
    uint32_t segmentCount;
    uint64_t flags;
    kern_return_t result;
    kern_return_t createRet;
    kern_return_t setLengthRet;
    kern_return_t rangeRet;
    kern_return_t mappingRet;
    kern_return_t dmaCreateRet;
    kern_return_t prepareRet;
    kern_return_t completeRet;
    kern_return_t syncForDeviceRet;
    kern_return_t syncForCPURet;
    uint32_t completed;
    uint32_t maxAddressBits;
    uint32_t cacheInhibitMapping;
    uint32_t lowAddressAttempted;
    kern_return_t lowAddressResult;
    uint32_t lowAddressSegmentCount;
    uint64_t lowAddressDMAAddress;
    uint64_t lowAddressDMALength;
};

struct OHCIAsyncDescriptor
{
    uint16_t reqCount;
    uint16_t control;
    uint32_t dataAddress;
    uint32_t branchAddress;
    uint16_t resCount;
    uint16_t transferStatus;
} __attribute__((aligned(16)));

struct DigiLiveReceivePacket
{
    volatile OHCIAsyncDescriptor * descriptor;
    volatile uint32_t * header;
    volatile uint32_t * payload;
};

struct DigiLiveCIPHeader
{
    uint32_t sid;
    uint32_t dbs;
    uint32_t sph;
    uint32_t dbc;
    uint32_t fmt;
    uint32_t fdf;
    uint32_t syt;
};

volatile OHCIAsyncDescriptor *
DigiLiveReceiveDescriptorAt(volatile OHCIAsyncDescriptor * ring, uint32_t index)
{
    return ring + index * kDigi00xDuplexIRDescriptorsPerPacketStorage;
}

volatile uint32_t *
DigiLiveReceiveHeaderFor(volatile OHCIAsyncDescriptor * descriptor)
{
    return reinterpret_cast<volatile uint32_t *>(&descriptor[kDigi00xDuplexIRDescriptorBranchCount]);
}

volatile uint32_t *
DigiLiveReceiveRawPayloadAt(uint64_t bufferAddress, uint32_t index)
{
    return reinterpret_cast<volatile uint32_t *>(bufferAddress +
                                                  kDigi00xDuplexIRDataOffset +
                                                  index * kDigiLiveIRDescriptorDataStride);
}

uint32_t
DigiLiveReceiveDescriptorBranchCount()
{
    return kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? 1
        : kDigi00xDuplexIRDescriptorBranchCount;
}

uint32_t
DigiLiveReceiveDescriptorDataSize()
{
    return kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? kDigiLiveSingleIRDescriptorDataSize
        : kDigi00xDuplexIRDescriptorDataSize;
}

DigiLiveReceivePacket
DigiLiveReceivePacketAt(volatile OHCIAsyncDescriptor * ring, uint64_t bufferAddress, uint32_t index)
{
    volatile OHCIAsyncDescriptor * descriptor = DigiLiveReceiveDescriptorAt(ring, index);
    volatile uint32_t * rawPayload = DigiLiveReceiveRawPayloadAt(bufferAddress, index);
    DigiLiveReceivePacket packet = {
        descriptor,
        kDigiLiveSingleDescriptorReceiveEnabled != 0
            ? rawPayload
            : DigiLiveReceiveHeaderFor(descriptor),
        kDigiLiveSingleDescriptorReceiveEnabled != 0
            ? rawPayload + kDigi00xDuplexIRSampleHeaderWordCount
            : rawPayload,
    };
    return packet;
}

uint32_t
DigiLiveReceiveDescriptorBytes(uint32_t payloadResCount)
{
    uint32_t descriptorDataSize = DigiLiveReceiveDescriptorDataSize();
    return payloadResCount <= descriptorDataSize
        ? descriptorDataSize - payloadResCount
        : 0;
}

uint32_t
DigiLiveReceivePayloadBytes(uint32_t descriptorBytes)
{
    if (kDigiLiveSingleDescriptorReceiveEnabled != 0) {
        return descriptorBytes > kDigi00xDuplexIRHeaderSize
            ? descriptorBytes - kDigi00xDuplexIRHeaderSize
            : 0;
    }
    return descriptorBytes;
}

uint32_t
DigiLiveReceiveDataBlocks(uint32_t payloadBytes)
{
    uint32_t dataBlocks = payloadBytes / kDigi00xDuplexDataBlockBytes;
    return dataBlocks > 8 ? 8 : dataBlocks;
}

bool
DigiLiveReceiveDescriptorIsEmpty(uint32_t descriptorBytes, uint32_t headerStatus, uint32_t payloadStatus)
{
    return descriptorBytes == 0 && headerStatus == 0 && payloadStatus == 0;
}

constexpr uint64_t
OhciIsoXmitContextControlSetOffset(uint32_t index)
{
    return 0x200ull + (16ull * index);
}

constexpr uint64_t
OhciIsoXmitContextControlClearOffset(uint32_t index)
{
    return 0x204ull + (16ull * index);
}

constexpr uint64_t
OhciIsoXmitCommandPtrOffset(uint32_t index)
{
    return 0x20cull + (16ull * index);
}

constexpr uint64_t
OhciIsoRcvContextControlSetOffset(uint32_t index)
{
    return 0x400ull + (32ull * index);
}

constexpr uint64_t
OhciIsoRcvContextControlClearOffset(uint32_t index)
{
    return 0x404ull + (32ull * index);
}

constexpr uint64_t
OhciIsoRcvCommandPtrOffset(uint32_t index)
{
    return 0x40cull + (32ull * index);
}

constexpr uint64_t
OhciIsoRcvContextMatchOffset(uint32_t index)
{
    return 0x410ull + (32ull * index);
}

struct AsyncReadDiagnostics
{
    uint32_t attempted;
    uint32_t ready;
    uint32_t localNodeID;
    uint32_t destinationID;
    uint32_t offsetHi;
    uint32_t offsetLo;
    uint32_t txCommandPtr;
    uint32_t rxCommandPtr;
    uint32_t intEventBefore;
    uint32_t intEventAfter;
    uint32_t intMaskAfter;
    uint32_t txContextBefore;
    uint32_t rxContextBefore;
    uint32_t txContextControl;
    uint32_t rxContextControl;
    uint32_t txContextAfterClear;
    uint32_t rxContextAfterClear;
    uint32_t txContextStopLoops;
    uint32_t rxContextStopLoops;
    uint32_t txContextFinalStopLoops;
    uint32_t rxContextFinalStopLoops;
    uint32_t txSyncForDeviceRet;
    uint32_t rxSyncForDeviceRet;
    uint32_t txSyncForCPURet;
    uint32_t rxSyncForCPURet;
    uint32_t txCompleteRet;
    uint32_t rxCompleteRet;
    uint32_t txDescriptorControl;
    uint32_t txDescriptorReqCount;
    uint32_t txDescriptorResCount;
    uint32_t txDescriptorStatus;
    uint32_t txHeader0;
    uint32_t txHeader1;
    uint32_t txHeader2;
    uint32_t txHeader3;
    uint32_t rxDescriptor0Control;
    uint32_t rxDescriptor0DataAddress;
    uint32_t rxDescriptor0BranchAddress;
    uint32_t rxDescriptor0ResCount;
    uint32_t rxDescriptor0Status;
    uint32_t rxDescriptor1Control;
    uint32_t rxDescriptor1DataAddress;
    uint32_t rxDescriptor1BranchAddress;
    uint32_t rxDescriptor1ResCount;
    uint32_t rxDescriptor1Status;
    uint32_t rxBytes0;
    uint32_t rxBytes1;
    uint32_t rxHeader0;
    uint32_t rxHeader1;
    uint32_t rxHeader2;
    uint32_t rxHeader3;
    uint32_t responseTCode;
    uint32_t responseTLabel;
    uint32_t responseSource;
    uint32_t responseRCode;
    uint32_t responseData;
    uint32_t waitLoops;
    uint32_t configuredAttempts;
    uint32_t completedAttempts;
    uint32_t lastAttempt;
    uint32_t responseAttempt;
    uint32_t ackPendingAttempts;
    uint32_t waitLoopsPerAttempt;
    uint32_t retrySettleMilliseconds;
    uint32_t configROMReadCount;
    uint32_t configROMReadSuccessCount;
    uint32_t configROMLastIndex;
    uint32_t configROMData[kConfigROMProbeReadCount];
    uint32_t configROMRCode[kConfigROMProbeReadCount];
    uint32_t configROMTCode[kConfigROMProbeReadCount];
    uint32_t configROMTLabel[kConfigROMProbeReadCount];
    uint32_t configROMRxBytes[kConfigROMProbeReadCount];
    uint32_t configROMTxStatus[kConfigROMProbeReadCount];
    uint32_t digiRegisterReadCount;
    uint32_t digiRegisterReadSuccessCount;
    uint32_t digiRegisterLastIndex;
    uint32_t digiRegisterOffsetLo[kDigi00xRegisterProbeCount];
    uint32_t digiRegisterData[kDigi00xRegisterProbeCount];
    uint32_t digiRegisterRCode[kDigi00xRegisterProbeCount];
    uint32_t digiRegisterTCode[kDigi00xRegisterProbeCount];
    uint32_t digiRegisterTLabel[kDigi00xRegisterProbeCount];
    uint32_t digiRegisterRxBytes[kDigi00xRegisterProbeCount];
    uint32_t digiRegisterTxStatus[kDigi00xRegisterProbeCount];
    uint32_t digiWritePlanEnabled;
    uint32_t digiWritePlanExecuted;
    uint32_t digiWritePlanCount;
    uint32_t digiWritePlanOffsetLo[kDigi00xWritePlanCount];
    uint32_t digiWritePlanBusValue[kDigi00xWritePlanCount];
    uint32_t digiWritePlanTxData[kDigi00xWritePlanCount];
    uint32_t digiWritePlanReqCount[kDigi00xWritePlanCount];
    uint32_t digiWritePlanHeader0[kDigi00xWritePlanCount];
    uint32_t digiWritePlanHeader1[kDigi00xWritePlanCount];
    uint32_t digiWritePlanHeader2[kDigi00xWritePlanCount];
    uint32_t digiWritePlanHeader3[kDigi00xWritePlanCount];
    uint32_t digiNoopWriteEnabled;
    uint32_t digiNoopWriteAttempted;
    uint32_t digiNoopWriteExecuted;
    uint32_t digiNoopWriteSuccess;
    uint32_t digiNoopWriteCompletedAttempts;
    uint32_t digiNoopWriteWaitLoops;
    uint32_t digiNoopWriteOffsetLo;
    uint32_t digiNoopWriteBusValue;
    uint32_t digiNoopWriteTxData;
    uint32_t digiNoopWriteReqCount;
    uint32_t digiNoopWriteHeader0;
    uint32_t digiNoopWriteHeader1;
    uint32_t digiNoopWriteHeader2;
    uint32_t digiNoopWriteHeader3;
    uint32_t digiNoopWriteTxStatus;
    uint32_t digiNoopWriteRxBytes;
    uint32_t digiNoopWriteResponseTCode;
    uint32_t digiNoopWriteResponseTLabel;
    uint32_t digiNoopWriteResponseSource;
    uint32_t digiNoopWriteResponseRCode;
    uint32_t digiStateWriteEnabled;
    uint32_t digiStateWriteAttempted;
    uint32_t digiStateWritePrereqNoopSuccess;
    uint32_t digiStateWriteExecuted;
    uint32_t digiStateWriteSuccess;
    uint32_t digiStateWriteCompletedAttempts;
    uint32_t digiStateWriteWaitLoops;
    uint32_t digiStateWriteOffsetLo;
    uint32_t digiStateWriteBusValue;
    uint32_t digiStateWriteTxData;
    uint32_t digiStateWriteReqCount;
    uint32_t digiStateWriteHeader0;
    uint32_t digiStateWriteHeader1;
    uint32_t digiStateWriteHeader2;
    uint32_t digiStateWriteHeader3;
    uint32_t digiStateWriteTxStatus;
    uint32_t digiStateWriteRxBytes;
    uint32_t digiStateWriteResponseTCode;
    uint32_t digiStateWriteResponseTLabel;
    uint32_t digiStateWriteResponseSource;
    uint32_t digiStateWriteResponseRCode;
    uint32_t digiStateSequenceEnabled;
    uint32_t digiStateSequenceAttempted;
    uint32_t digiStateSequencePrereqStateWriteSuccess;
    uint32_t digiStateSequenceStepCount;
    uint32_t digiStateSequenceCompletedSteps;
    uint32_t digiStateSequenceSuccess;
    uint32_t digiStateSequenceOp[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceOffsetLo[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceBusValue[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceTxData[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceReqCount[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceCompletedAttempts[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceWaitLoops[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceSuccessByStep[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceHeader0[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceHeader1[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceHeader2[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceHeader3[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceTxStatus[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceRxBytes[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceResponseTCode[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceResponseTLabel[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceResponseSource[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceResponseRCode[kDigi00xStateSequenceStepCount];
    uint32_t digiStateSequenceResponseData[kDigi00xStateSequenceStepCount];
    uint32_t reqRxCommandPtr;
    uint32_t reqRxContextBefore;
    uint32_t reqRxContextControl;
    uint32_t reqRxContextAfterClear;
    uint32_t reqRxContextStopLoops;
    uint32_t reqRxContextFinalStopLoops;
    uint32_t reqRxSyncForDeviceRet;
    uint32_t reqRxSyncForCPURet;
    uint32_t reqRxCompleteRet;
    uint32_t reqRxDescriptor0ResCount;
    uint32_t reqRxDescriptor0Status;
    uint32_t reqRxDescriptor1ResCount;
    uint32_t reqRxDescriptor1Status;
    uint32_t reqRxBytes0;
    uint32_t reqRxBytes1;
    uint32_t reqRxHeader0;
    uint32_t reqRxHeader1;
    uint32_t reqRxHeader2;
    uint32_t reqRxHeader3;
};

struct IsoTestDiagnostics
{
    uint32_t enabled;
    uint32_t attempted;
    uint32_t ready;
    uint32_t contextIndex;
    uint32_t channel;
    uint32_t xmitMaskSupport;
    uint32_t recvMaskSupport;
    uint32_t xmitContextSupported;
    uint32_t recvContextSupported;
    uint32_t xmitEventBefore;
    uint32_t recvEventBefore;
    uint32_t xmitEventAfter;
    uint32_t recvEventAfter;
    uint32_t xmitMaskAfterClear;
    uint32_t recvMaskAfterClear;
    uint32_t itControlBefore;
    uint32_t itControlAfterStop;
    uint32_t itControlAfterCommandPtr;
    uint32_t itStopLoops;
    uint32_t itCommandPtr;
    uint32_t itCommandPtrReadBack;
    uint32_t irControlBefore;
    uint32_t irControlAfterStop;
    uint32_t irControlAfterCommandPtr;
    uint32_t irStopLoops;
    uint32_t irCommandPtr;
    uint32_t irCommandPtrReadBack;
    uint32_t irContextMatch;
    uint32_t irContextMatchReadBack;
    uint32_t runReceiveEnabled;
    uint32_t runReceiveAttempted;
    uint32_t runReceiveMilliseconds;
    uint32_t irRunControl;
    uint32_t irControlAfterRun;
    uint32_t irControlAfterWait;
    uint32_t irControlAfterFinalStop;
    uint32_t irRunStopLoops;
    uint32_t irEventAfterRun;
    uint32_t irMaskAfterRun;
    uint32_t irMaskAfterFinalClear;
    uint32_t irDescriptorResCountAfterRun;
    uint32_t irDescriptorStatusAfterRun;
    uint32_t runTransmitEnabled;
    uint32_t runTransmitAttempted;
    uint32_t runTransmitMilliseconds;
    uint32_t itRunControl;
    uint32_t itControlAfterRun;
    uint32_t itControlAfterWait;
    uint32_t itControlAfterFinalStop;
    uint32_t itRunStopLoops;
    uint32_t itEventAfterRun;
    uint32_t itMaskAfterRun;
    uint32_t itMaskAfterFinalClear;
    uint32_t itDescriptorResCountAfterRun;
    uint32_t itDescriptorStatusAfterRun;
    uint32_t itImmediateHeader0;
    uint32_t itImmediateHeader1;
    uint32_t syncForDeviceRet;
    uint32_t syncForCPURet;
    uint32_t completeRet;
    uint32_t itDescriptorControl;
    uint32_t itDescriptorReqCount;
    uint32_t itDescriptorDataAddress;
    uint32_t itDescriptorBranchAddress;
    uint32_t itDescriptorResCount;
    uint32_t itDescriptorStatus;
    uint32_t irDescriptorControl;
    uint32_t irDescriptorReqCount;
    uint32_t irDescriptorDataAddress;
    uint32_t irDescriptorBranchAddress;
    uint32_t irDescriptorResCount;
    uint32_t irDescriptorStatus;
};

struct DigiDuplexDiagnostics
{
    uint32_t enabled;
    uint32_t attempted;
    uint32_t destinationID;
    uint32_t deviceTransmitChannel;
    uint32_t deviceReceiveChannel;
    uint32_t sessionChannelsBusValue;
    uint32_t initialStreamingStateBusValue;
    uint32_t beginSetFirstBusValue;
    uint32_t beginSetSecondBusValue;
    uint32_t finalStreamingStateBusValue;
    uint32_t finalIsocChannelsBusValue;
    uint32_t stepCount;
    uint32_t completedSteps;
    uint32_t success;
    uint32_t finishAttempted;
    uint32_t stepOp[kDigi00xDuplexStepCount];
    uint32_t stepOffsetLo[kDigi00xDuplexStepCount];
    uint32_t stepBusValue[kDigi00xDuplexStepCount];
    uint32_t stepTxData[kDigi00xDuplexStepCount];
    uint32_t stepSuccess[kDigi00xDuplexStepCount];
    uint32_t stepCompletedAttempts[kDigi00xDuplexStepCount];
    uint32_t stepWaitLoops[kDigi00xDuplexStepCount];
    uint32_t stepTxStatus[kDigi00xDuplexStepCount];
    uint32_t stepRxBytes[kDigi00xDuplexStepCount];
    uint32_t stepResponseTCode[kDigi00xDuplexStepCount];
    uint32_t stepResponseTLabel[kDigi00xDuplexStepCount];
    uint32_t stepResponseSource[kDigi00xDuplexStepCount];
    uint32_t stepResponseRCode[kDigi00xDuplexStepCount];
    uint32_t stepResponseData[kDigi00xDuplexStepCount];
    uint32_t isoAttempted;
    uint32_t isoReady;
    uint32_t xmitMaskSupport;
    uint32_t recvMaskSupport;
    uint32_t xmitContextSupported;
    uint32_t recvContextSupported;
    uint32_t itCommandPtr;
    uint32_t irCommandPtr;
    uint32_t irContextMatch;
    uint32_t itImmediateHeader0;
    uint32_t itImmediateHeader1;
    uint32_t itPacketCount;
    uint32_t itDataBlockQuadlets;
    uint32_t itTotalDataBlocks;
    uint32_t itFirstDataBlocks;
    uint32_t itLastDataBlocks;
    uint32_t itFirstPayloadBytes;
    uint32_t itLastPayloadBytes;
    uint32_t itFirstCIPHeader0;
    uint32_t itFirstCIPHeader1;
    uint32_t itLastCIPHeader0;
    uint32_t itLastCIPHeader1;
    uint32_t itLastDescriptorResCount;
    uint32_t itLastDescriptorStatus;
    uint32_t sourceNodeIDField;
    uint32_t itControlAfterRun;
    uint32_t itControlAfterWait;
    uint32_t itControlAfterStop;
    uint32_t itStopLoops;
    uint32_t itEventAfterRun;
    uint32_t itMaskAfterRun;
    uint32_t itMaskAfterClear;
    uint32_t irControlAfterRun;
    uint32_t irControlAfterWait;
    uint32_t irControlAfterStop;
    uint32_t irStopLoops;
    uint32_t irEventAfterRun;
    uint32_t irMaskAfterRun;
    uint32_t irMaskAfterClear;
    uint32_t itDescriptorResCount;
    uint32_t itDescriptorStatus;
    uint32_t irDescriptorResCount;
    uint32_t irDescriptorStatus;
    uint32_t irDescriptorCount;
    uint32_t irDescriptorDataSize;
    uint32_t irActiveDescriptorCount;
    uint32_t irDescriptor1ResCount;
    uint32_t irDescriptor1Status;
    uint32_t irDescriptor2ResCount;
    uint32_t irDescriptor2Status;
    uint32_t irDescriptor3ResCount;
    uint32_t irDescriptor3Status;
    uint32_t irDescriptorLastTouched;
    uint32_t irRxBytes;
    uint32_t irHeader[8];
    uint32_t irSamplePacketCount;
    uint32_t irSamplePayloadWordCount;
    uint32_t irSampleDataBlockBytes;
    uint32_t irSamplePacketIndex[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleBytes[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleDataBlocks[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleRemainderBytes[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleHeaderStatus[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSamplePayloadStatus[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleHeader[kDigi00xDuplexIRSamplePacketCount][kDigi00xDuplexIRSampleHeaderWordCount];
    uint32_t irSampleHeaderBE[kDigi00xDuplexIRSamplePacketCount][kDigi00xDuplexIRSampleHeaderWordCount];
    uint32_t irSamplePayload[kDigi00xDuplexIRSamplePacketCount][kDigi00xDuplexIRSamplePayloadWordCount];
    uint32_t irSamplePayloadBE[kDigi00xDuplexIRSamplePacketCount][kDigi00xDuplexIRSamplePayloadWordCount];
    uint32_t irSampleFirstWordTag[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleFirstAudioTag[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irSampleFirstAudioValue24[kDigi00xDuplexIRSamplePacketCount];
    uint32_t irChannelSamplePacketIndex;
    uint32_t irChannelSampleBytes;
    uint32_t irChannelSampleDataBlocks;
    uint32_t irChannelSampleCapturedBlocks;
    uint32_t irChannelSampleChannelCount;
    uint32_t irChannelSampleHeaderBE[kDigi00xDuplexIRSampleHeaderWordCount];
    uint32_t irChannelSampleBlockTag[kDigi00xDuplexIRChannelSampleMaxDataBlocks];
    uint32_t irChannelSampleWordBE[kDigi00xDuplexIRChannelSampleMaxDataBlocks]
                                  [kDigi00xDuplexIRChannelSampleChannelCount];
    uint32_t irChannelSampleLabel[kDigi00xDuplexIRChannelSampleMaxDataBlocks]
                                 [kDigi00xDuplexIRChannelSampleChannelCount];
    uint32_t irChannelSampleValue24[kDigi00xDuplexIRChannelSampleMaxDataBlocks]
                                   [kDigi00xDuplexIRChannelSampleChannelCount];
    uint32_t irCaptureSummaryFrameCount;
    uint32_t irCaptureSummaryPacketCount;
    uint32_t irCaptureSummaryChannelCount;
    uint32_t irCaptureSummaryLabelMismatchCount;
    uint32_t irCaptureChannelNonzeroCount[kDigi00xDuplexIRCaptureSummaryChannelCount];
    uint32_t irCaptureChannelFirstNonzeroFrame[kDigi00xDuplexIRCaptureSummaryChannelCount];
    uint32_t irCaptureChannelMinValue[kDigi00xDuplexIRCaptureSummaryChannelCount];
    uint32_t irCaptureChannelMaxValue[kDigi00xDuplexIRCaptureSummaryChannelCount];
    uint32_t irCaptureChannelPeakAbs[kDigi00xDuplexIRCaptureSummaryChannelCount];
    uint32_t irCaptureChannelLastValue[kDigi00xDuplexIRCaptureSummaryChannelCount];
    uint32_t irCapturePCMFrameLimit;
    uint32_t irCapturePCMFrameCount;
    uint32_t irCapturePCMChannelCount;
    uint32_t irCapturePCMSampleRate;
    uint32_t irCapturePCMBytes;
    uint32_t irCapturePCMPeakAbs;
    int32_t irCapturePCMS24[kDigi00xDuplexIRCapturePCMFrameLimit]
                           [kDigi00xDuplexIRCapturePCMChannelCount];
    uint32_t syncForDeviceRet;
    uint32_t syncForCPURet;
    uint32_t completeRet;
};

struct DigiDotState {
    uint8_t carry;
    uint8_t idx;
    uint32_t off;
};

DMABuffer gSelfIDBuffer = {};
DMABuffer gConfigROMBuffer = {};
DMABuffer gAsyncTxBuffer = {};
DMABuffer gAsyncRxBuffer = {};
DMABuffer gAsyncReqRxBuffer = {};
DMABuffer gIsoTestBuffer = {};
DMABuffer gDigiDuplexBuffer = {};
DMABuffer gDigiLiveBuffer = {};
IOBufferMemoryDescriptor * gAudioInputBuffer = nullptr;
IOBufferMemoryDescriptor * gAudioOutputBuffer = nullptr;
IOAddressSegment gAudioInputCPUAddress = {};
IOAddressSegment gAudioOutputCPUAddress = {};
OSSharedPtr<IOUserAudioDevice> gAudioDevice;
OSSharedPtr<IOUserAudioStream> gAudioInputStream;
OSSharedPtr<IOUserAudioStream> gAudioOutputStream;
FireWireOHCIProbe * gDriverInstance = nullptr;
IOPCIDevice * gPCIDevice = nullptr;
uint8_t gPCIMemoryIndex = 0xff;
uint32_t gDigiDestinationID = 0xffffffff;
IODispatchQueue * gAudioRefreshQueue = nullptr;
IOInterruptDispatchSource * gOHCIInterruptSource = nullptr;
OSAction * gOHCIInterruptAction = nullptr;
uint32_t gOHCIInterruptConfigureRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gOHCIInterruptActionCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gOHCIInterruptSourceCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gOHCIInterruptSetHandlerRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gOHCIInterruptReady = 0;
uint32_t gOHCIInterruptEnabled = 0;
uint32_t gOHCIInterruptUseMSIX = 0;
uint64_t gOHCIInterruptCount = 0;
uint64_t gOHCIInterruptIsoRecvCount = 0;
uint64_t gOHCIInterruptIsoXmitCount = 0;
uint64_t gOHCIInterruptOtherCount = 0;
uint64_t gOHCIInterruptDigiHarvestAttemptCount = 0;
uint64_t gOHCIInterruptDigiHarvestSuccessCount = 0;
uint64_t gOHCIInterruptDigiHarvestBusyCount = 0;
uint64_t gOHCIInterruptDigiHarvestEmptyCount = 0;
uint32_t gOHCIInterruptLastHarvestRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gOHCIInterruptLastIntEvent = 0;
uint32_t gOHCIInterruptLastIsoRecvEvent = 0;
uint32_t gOHCIInterruptLastIsoXmitEvent = 0;
uint32_t gOHCIInterruptLastClearedIntEvent = 0;
uint32_t gOHCIInterruptLastClearedIsoRecvEvent = 0;
uint32_t gOHCIInterruptLastClearedIsoXmitEvent = 0;
uint64_t gOHCIInterruptLastCount = 0;
uint64_t gOHCIInterruptLastTime = 0;
uint32_t gAudioDeviceCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioStreamCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioAddStreamRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputStreamCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputAddStreamRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioAddObjectRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioIOHandlerRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioStreamActiveRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputStreamActiveRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRegisterIOThreadRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioStartIOThreadRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioStartDeviceCount = 0;
uint32_t gAudioStopDeviceCount = 0;
uint32_t gAudioStartDeviceRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioStopDeviceRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioStartDeviceObjectID = 0;
uint32_t gAudioStopDeviceObjectID = 0;
uint32_t gAudioStartDeviceStage = 0;
uint32_t gAudioStopDeviceStage = 0;
uint32_t gAudioRuntimeDeviceStarted = 0;
uint32_t gPowerStateLastFlags = 0xffffffff;
uint32_t gPowerStateLastRet = static_cast<uint32_t>(kIOReturnNotReady);
uint64_t gPowerStateChangeCount = 0;
uint64_t gPowerStateOnCount = 0;
uint64_t gPowerStateOffCount = 0;
uint64_t gPowerStateLowCount = 0;
uint64_t gPowerStateLiveStopCount = 0;
uint64_t gPowerStateWakeRestartRequestCount = 0;
uint32_t gAudioDeviceStartIOCount = 0;
uint32_t gAudioDeviceStopIOCount = 0;
uint32_t gAudioDeviceStartIORet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioDeviceStopIORet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioBufferCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioBufferSetLengthRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioBufferRangeRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputBufferCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputBufferSetLengthRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputBufferRangeRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRefreshCaptureAttemptCount = 0;
uint32_t gAudioRefreshCaptureSuccessCount = 0;
uint32_t gAudioRefreshCaptureInProgress = 0;
uint32_t gAudioRefreshCaptureRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRefreshCaptureFrameCount = 0;
uint32_t gAudioRefreshCapturePeakAbs = 0;
uint32_t gAudioRefreshCaptureRxBytes = 0;
uint32_t gAudioRefreshQueueCreateRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRefreshWorkerDispatchCount = 0;
uint32_t gAudioRefreshWorkerIterationCount = 0;
uint32_t gAudioRefreshWorkerExitCount = 0;
uint32_t gAudioRefreshWorkerRunning = 0;
uint32_t gAudioRefreshWorkerStopWaitLoops = 0;
uint32_t gAudioRefreshWorkerLastRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRefreshWorkerLastGeneration = 0;
uint64_t gAudioRefreshWorkerLivePublishCount = 0;
uint64_t gAudioRefreshWorkerLivePublishSkipCount = 0;
uint64_t gAudioRefreshWorkerBacklogNoSleepCount = 0;
uint64_t gAudioRefreshWorkerLowWaterNoSleepCount = 0;
uint32_t gAudioInputCallbackHarvestAttemptCount = 0;
uint32_t gAudioInputCallbackHarvestSuccessCount = 0;
uint32_t gAudioInputCallbackHarvestRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioInputCallbackHarvestLastFillFrames = 0;
uint32_t gAudioOutputCallbackCount = 0;
uint32_t gAudioRuntimeRestartInProgress = 0;
uint32_t gAudioRuntimeRestartLastReason = 0;
uint32_t gAudioRuntimeRestartLastRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRuntimeRestartLastLiveRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioRuntimeRestartLastDigiRunning = 0;
uint32_t gAudioRuntimeRestartLastDigiReady = 0;
uint32_t gAudioRuntimeRestartLastWorkerRunning = 0;
uint64_t gAudioRuntimeRestartRequestCount = 0;
uint64_t gAudioRuntimeRestartDispatchCount = 0;
uint64_t gAudioRuntimeRestartSuccessCount = 0;
uint64_t gAudioRuntimeRestartSkippedCount = 0;
uint64_t gAudioRuntimeRestartBusyCount = 0;
uint32_t gDigi00xCurrentSampleRate = kDigi00xDuplexDefaultSampleRate;
uint32_t gDigi00xCurrentLocalRateIndex = kDigi00xDuplexLocalRateIndex48000;
uint32_t gDigi00xCurrentCIPSFC = kDigi00xDuplexCIPSFC48000;
uint64_t gAudioRuntimeSampleRateChangeCount = 0;
uint32_t gAudioRuntimeRequestedSampleRate = kDigi00xDuplexDefaultSampleRate;
uint32_t gAudioRuntimeSampleRateChangeStage = 0;
uint32_t gAudioRuntimeSampleRateChangeRestarted = 0;
uint32_t gAudioRuntimeSampleRateChangeRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioOutputLastBufferFrameSize = 0;
uint64_t gAudioOutputLastSampleTime = 0;
uint32_t gAudioCaptureFrameCount = 0;
uint32_t gAudioCapturePeakAbs = 0;
uint32_t gAudioCaptureGeneration = 0;
uint64_t gAudioRingWriteFrame = 0;
uint64_t gAudioRingReadFrame = 0;
uint64_t gAudioRingProducedFrames = 0;
uint64_t gAudioRingConsumedFrames = 0;
uint64_t gAudioRingRepeatedFrames = 0;
uint64_t gAudioRingUnderrunFrames = 0;
uint64_t gAudioRingOverrunFrames = 0;
uint32_t gAudioRingCurrentFillFrames = 0;
uint32_t gAudioRingMaxFillFrames = 0;
uint32_t gAudioRingAppendCount = 0;
uint32_t gAudioRingLastAppendFrames = 0;
uint32_t gAudioRingLastAppendDroppedFrames = 0;
uint32_t gAudioRingLastAppendGeneration = 0;
uint32_t gAudioRingLastConsumeFrames = 0;
uint32_t gAudioRingLastConsumeUnderrunFrames = 0;
uint32_t gAudioRingLastConsumeRepeatedFrames = 0;
uint64_t gAudioOutputRingWriteFrame = 0;
uint64_t gAudioOutputRingReadFrame = 0;
uint64_t gAudioOutputRingProducedFrames = 0;
uint64_t gAudioOutputRingConsumedFrames = 0;
uint64_t gAudioOutputRingUnderrunFrames = 0;
uint64_t gAudioOutputRingOverrunFrames = 0;
uint32_t gAudioOutputRingCurrentFillFrames = 0;
uint32_t gAudioOutputRingMaxFillFrames = 0;
uint32_t gAudioOutputRingAppendCount = 0;
uint32_t gAudioOutputRingLastAppendFrames = 0;
uint32_t gAudioOutputRingLastAppendDroppedFrames = 0;
uint32_t gAudioOutputRingLastConsumeFrames = 0;
uint32_t gAudioOutputRingLastConsumeUnderrunFrames = 0;
uint32_t gAudioOutputRingPrebuffered = 0;
uint64_t gAudioOutputRingPrebufferReadyCount = 0;
uint64_t gAudioOutputRingPrebufferHoldCount = 0;
uint32_t gDigiLiveStartAttemptCount = 0;
uint32_t gDigiLiveStartSuccessCount = 0;
uint32_t gDigiLiveStopAttemptCount = 0;
uint32_t gDigiLiveStopSuccessCount = 0;
uint32_t gDigiLiveStopDrainWaitLoops = 0;
uint32_t gDigiLiveRunning = 0;
uint32_t gDigiLiveStarting = 0;
uint32_t gDigiLiveStopping = 0;
uint32_t gDigiLiveReady = 0;
uint32_t gDigiLiveState = kDigiLiveStateStopped;
uint32_t gDigiLiveDrainBusy = 0;
uint32_t gDigiLiveStartRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveStopRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveStartStage = 0;
uint32_t gDigiLiveBeginTransactionStage = 0;
uint32_t gDigiLiveBeginTransactionRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveIsoStartStage = 0;
uint32_t gDigiLiveIsoStartRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveLastHarvestRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveIRReadIndex = 0;
uint32_t gDigiLiveIRCommandPtrPacketIndex = 0xffffffff;
uint32_t gDigiLiveIRHardwareCursorValid = 0;
uint64_t gDigiLiveIRHardwarePacketCursor = 0;
uint64_t gDigiLiveIRSoftwarePacketCursor = 0;
uint32_t gDigiLiveIRBacklogPackets = 0;
uint64_t gDigiLiveIREmptyCatchUpCount = 0;
uint64_t gDigiLiveIREmptyCatchUpSkippedPackets = 0;
uint64_t gDigiLiveIREmptyCatchUpScanCount = 0;
uint64_t gDigiLiveIREmptyCatchUpScanFoundCount = 0;
uint64_t gDigiLiveIREmptyCatchUpScanPackets = 0;
uint64_t gDigiLiveIRSegmentCatchUpAttemptCount = 0;
uint64_t gDigiLiveIRSegmentCatchUpSuccessCount = 0;
uint64_t gDigiLiveIRSegmentCatchUpFailureCount = 0;
uint64_t gDigiLiveIRSegmentCatchUpScannedSegments = 0;
uint32_t gDigiLiveIREmptyCatchUpLastFromIndex = 0xffffffff;
uint32_t gDigiLiveIREmptyCatchUpLastToIndex = 0xffffffff;
uint32_t gDigiLiveIREmptyCatchUpLastHardwareIndex = 0xffffffff;
uint32_t gDigiLiveIREmptyCatchUpLastSkippedPackets = 0;
uint32_t gDigiLiveIREmptyCatchUpLastScannedPackets = 0;
uint32_t gDigiLiveIRSegmentCatchUpLastFromIndex = 0xffffffff;
uint32_t gDigiLiveIRSegmentCatchUpLastToIndex = 0xffffffff;
uint32_t gDigiLiveIRSegmentCatchUpLastHardwareIndex = 0xffffffff;
uint32_t gDigiLiveIRSegmentCatchUpLastSkippedPackets = 0;
uint32_t gDigiLiveIRSegmentCatchUpLastScannedSegments = 0;
uint32_t gDigiLiveLastDescriptorIndex = 0xffffffff;
uint32_t gDigiLiveLastDescriptorBytes = 0;
uint32_t gDigiLiveLastDescriptorDataBlocks = 0;
uint32_t gDigiLiveLastDescriptorHeaderStatus = 0;
uint32_t gDigiLiveLastDescriptorPayloadStatus = 0;
uint32_t gDigiLiveLastDescriptorHeaderResCount = 0;
uint32_t gDigiLiveLastDescriptorPayloadResCount = 0;
uint64_t gDigiLiveHarvestAttemptCount = 0;
uint64_t gDigiLiveHarvestSuccessCount = 0;
uint64_t gDigiLiveHarvestPacketCount = 0;
uint64_t gDigiLiveHarvestFrameCount = 0;
uint64_t gDigiLiveHarvestRxBytes = 0;
uint64_t gDigiLiveDrainAttemptCount = 0;
uint64_t gDigiLiveDrainBusyCount = 0;
uint32_t gDigiLiveLastHarvestPackets = 0;
uint32_t gDigiLiveLastHarvestFrames = 0;
uint32_t gDigiLiveLastHarvestBytes = 0;
uint32_t gDigiLiveLastHarvestPeakAbs = 0;
uint32_t gDigiLiveLastHarvestLabelMismatchCount = 0;
uint32_t gDigiLiveHarvestPeakAbs = 0;
uint32_t gDigiLiveDescriptorResetCount = 0;
uint32_t gDigiLiveEmptyPollCount = 0;
uint32_t gDigiLiveSlot0LastLabel = 0;
uint32_t gDigiLiveSlot0LastValue24 = 0;
uint64_t gDigiLiveSlot0NonzeroCount = 0;
uint64_t gDigiLiveMidiMessageCount = 0;
uint64_t gDigiLiveMidiPhysicalMessageCount = 0;
uint64_t gDigiLiveMidiConsoleMessageCount = 0;
uint64_t gDigiLiveMidiInvalidLengthCount = 0;
uint32_t gDigiLiveMidiLastRawWordBE = 0;
uint32_t gDigiLiveMidiLastMarker = 0;
uint32_t gDigiLiveMidiLastData0 = 0;
uint32_t gDigiLiveMidiLastData1 = 0;
uint32_t gDigiLiveMidiLastControl = 0;
uint32_t gDigiLiveMidiLastPortNibble = 0;
uint32_t gDigiLiveMidiLastLength = 0;
uint32_t gDigiLiveMidiLastMessageRawWordBE = 0;
uint32_t gDigiLiveMidiLastMessageMarker = 0;
uint32_t gDigiLiveMidiLastMessageData0 = 0;
uint32_t gDigiLiveMidiLastMessageData1 = 0;
uint32_t gDigiLiveMidiLastMessageControl = 0;
uint32_t gDigiLiveMidiLastMessagePortNibble = 0;
uint32_t gDigiLiveMidiLastMessageLength = 0;
uint32_t gDigiLiveMidiRecentIndex = 0;
uint32_t gDigiLiveMidiRecentCount = 0;
uint32_t gDigiLiveMidiRecentRawWordBE[kDigiLiveMidiRecentMessageCount] = {};
uint8_t gDigiLiveMidiPendingBytes[kDigiLiveMidiPortCount][kDigiLiveMidiBytesPerMessage] = {};
uint32_t gDigiLiveMidiPendingByteCount[kDigiLiveMidiPortCount] = {};
uint64_t gDigiLiveMidiDecodedMessageCount = 0;
uint64_t gDigiLiveMidiNoteMessageCount = 0;
uint64_t gDigiLiveMidiControlChangeMessageCount = 0;
uint32_t gDigiLiveMidiLastDecodedMessage = 0;
uint32_t gDigiLiveMidiLastDecodedPort = 0;
uint32_t gDigiLiveMidiLastDecodedStatus = 0;
uint32_t gDigiLiveMidiLastDecodedData1 = 0;
uint32_t gDigiLiveMidiLastDecodedData2 = 0;
uint32_t gDigiLiveMidiLastNoteNumber = 0;
uint32_t gDigiLiveMidiLastNoteVelocity = 0;
uint32_t gDigiLiveMidiLastControlNumber = 0;
uint32_t gDigiLiveMidiLastControlValue = 0;
uint32_t gDigiLiveMidiDecodedRecentIndex = 0;
uint32_t gDigiLiveMidiDecodedRecentCount = 0;
uint32_t gDigiLiveMidiDecodedRecentMessages[kDigiLiveMidiDecodedRecentMessageCount] = {};
uint64_t gDigiLiveControlMappedMessageCount = 0;
uint64_t gDigiLiveControlUnknownMessageCount = 0;
uint32_t gDigiLiveControlLastMappedKind = kDigiLiveControlKindUnknown;
uint32_t gDigiLiveControlLastMappedChannel = 0xffffffff;
uint32_t gDigiLiveControlChannelSelectPressed[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlChannelSoloPressed[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlChannelMutePressed[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlChannelFaderTouched[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlChannelFaderControlNumber[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlChannelFaderValue[kDigiLiveControlChannelStripCount] = {};
uint64_t gDigiLiveControlChannelFaderUpdateCount[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlMotorTestToggle[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlSelect1Pressed = 0;
uint32_t gDigiLiveControlFader1Touched = 0;
uint32_t gDigiLiveControlFader1ControlNumber = 0;
uint32_t gDigiLiveControlFader1Value = 0;
uint64_t gDigiLiveControlFader1UpdateCount = 0;
uint32_t gDigiLiveControlTransportRTZPressed = 0;
uint32_t gDigiLiveControlTransportRewindPressed = 0;
uint32_t gDigiLiveControlTransportFastForwardPressed = 0;
uint32_t gDigiLiveControlStopPressed = 0;
uint32_t gDigiLiveControlPlayPressed = 0;
uint32_t gDigiLiveControlTransportRecordPressed = 0;
uint32_t gDigiLiveControlArrowLeftPressed = 0;
uint32_t gDigiLiveControlArrowRightPressed = 0;
uint32_t gDigiLiveControlArrowUpPressed = 0;
uint32_t gDigiLiveControlArrowDownPressed = 0;
uint32_t gDigiLiveControlJogWheelValue = 0;
uint32_t gDigiLiveControlJogWheelDirection = 0;
uint32_t gDigiLiveControlJogWheelStep = 0;
uint64_t gDigiLiveControlJogWheelUpdateCount = 0;
uint32_t gDigiLiveControlShuttleValue = 0;
uint64_t gDigiLiveControlShuttleUpdateCount = 0;
uint32_t gDigiLiveControlModeViewButtonPressed[kDigiLiveControlModeViewButtonCount] = {};
uint32_t gDigiLiveControlModeViewLastNote = 0xffffffff;
uint32_t gDigiLiveControlModeViewLastIndex = 0xffffffff;
uint64_t gDigiLiveControlModeViewUpdateCount = 0;
uint32_t gDigiLiveControlAboveTransportButtonPressed[kDigiLiveControlAboveTransportButtonCount] = {};
uint32_t gDigiLiveControlAboveTransportLastNote = 0xffffffff;
uint32_t gDigiLiveControlAboveTransportLastIndex = 0xffffffff;
uint64_t gDigiLiveControlAboveTransportUpdateCount = 0;
uint32_t gDigiLiveControlTransportSectionButtonPressed[kDigiLiveControlTransportSectionButtonCount] = {};
uint32_t gDigiLiveControlTransportSectionLastGroup = 0xffffffff;
uint32_t gDigiLiveControlTransportSectionLastNote = 0xffffffff;
uint32_t gDigiLiveControlTransportSectionLastIndex = 0xffffffff;
uint64_t gDigiLiveControlTransportSectionUpdateCount = 0;
uint32_t gDigiLiveControlNavigationModeLedNote = 0xffffffff;
uint32_t gDigiLiveControlHardwareMonitorButtonPressed[kDigiLiveControlHardwareMonitorButtonCount] = {};
uint32_t gDigiLiveControlHardwareMonitorLastNote = 0xffffffff;
uint32_t gDigiLiveControlHardwareMonitorLastIndex = 0xffffffff;
uint64_t gDigiLiveControlHardwareMonitorUpdateCount = 0;
uint32_t gDigiLiveControlDisplayModePressed = 0;
uint64_t gDigiLiveControlDisplayModeUpdateCount = 0;
uint32_t gDigiLiveControlEncoderAssignButtonPressed[kDigiLiveControlEncoderAssignButtonCount] = {};
uint32_t gDigiLiveControlEncoderAssignLastNote = 0xffffffff;
uint32_t gDigiLiveControlEncoderAssignLastIndex = 0xffffffff;
uint64_t gDigiLiveControlEncoderAssignUpdateCount = 0;
uint32_t gDigiLiveControlRotaryEncoderValue[kDigiLiveControlRotaryEncoderCount] = {};
uint32_t gDigiLiveControlRotaryEncoderDirection[kDigiLiveControlRotaryEncoderCount] = {};
uint32_t gDigiLiveControlRotaryEncoderStep[kDigiLiveControlRotaryEncoderCount] = {};
uint64_t gDigiLiveControlRotaryEncoderUpdateCount[kDigiLiveControlRotaryEncoderCount] = {};
uint32_t gDigiLiveControlRotaryEncoderLastIndex = 0xffffffff;
uint64_t gDigiLiveControlMotorTestTriggerCount = 0;
uint64_t gDigiLiveControlMotorTestMessageCount = 0;
uint64_t gDigiLiveControlMotorTestSkippedCount = 0;
uint32_t gDigiLiveControlMotorTestLastChannel = 0xffffffff;
uint32_t gDigiLiveControlMotorTestLastTarget10 = 0;
uint32_t gDigiLiveControlMotorTestLastCC = 0;
uint32_t gDigiLiveControlMotorTestLastValue = 0;
uint32_t gDigiLiveControlFaderFeedbackMovesSinceSend[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlFaderFeedbackLastSentValue[kDigiLiveControlChannelStripCount] = {};
uint32_t gDigiLiveControlFaderFeedbackLastSentValid[kDigiLiveControlChannelStripCount] = {};
uint64_t gDigiLiveControlFaderFeedbackSentCount = 0;
uint64_t gDigiLiveControlFaderFeedbackSkippedCount = 0;
uint64_t gDigiLiveControlFaderFeedbackFlushCount = 0;
uint32_t gDigiLiveControlFaderFeedbackLastChannel = 0xffffffff;
uint32_t gDigiLiveControlFaderFeedbackLastCC = 0;
uint32_t gDigiLiveControlFaderFeedbackLastValue = 0;
uint64_t gDigiLiveControlDebugCommandCount = 0;
uint64_t gDigiLiveControlDebugMessageCount = 0;
uint64_t gDigiLiveControlDebugSkippedCount = 0;
uint32_t gDigiLiveControlDebugLastCommand = 0;
uint32_t gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveControlDebugLastPort = 0xffffffff;
uint32_t gDigiLiveControlDebugLastStatus = 0;
uint32_t gDigiLiveControlDebugLastData1 = 0;
uint32_t gDigiLiveControlDebugLastData2 = 0;
uint32_t gDigiLiveControlDebugLastFaderChannel = 0xffffffff;
uint32_t gDigiLiveControlDebugLastFaderTarget10 = 0;
uint64_t gDigiLiveMidiLoggedMessageCount = 0;
uint32_t gDigiLiveMidiEchoQueueBusy = 0;
uint32_t gDigiLiveMidiEchoReadIndex = 0;
uint32_t gDigiLiveMidiEchoWriteIndex = 0;
uint32_t gDigiLiveMidiEchoQueueCount = 0;
uint32_t gDigiLiveMidiEchoLastQueuedRawWordBE = 0;
uint32_t gDigiLiveMidiEchoLastTransmitRawWordBE = 0;
uint32_t gDigiLiveMidiEchoQueue[kDigiLiveMidiEchoQueueSize] = {};
uint64_t gDigiLiveMidiFeedbackMessageCount = 0;
uint64_t gDigiLiveMidiFeedbackSkippedCount = 0;
uint64_t gDigiLiveMidiEchoAppendCount = 0;
uint64_t gDigiLiveMidiEchoDropCount = 0;
uint64_t gDigiLiveMidiEchoTransmitCount = 0;
uint64_t gDigiLiveMidiEchoBusyCount = 0;
uint32_t gDigiLiveSyncForDeviceRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveSyncForCPURet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveCompleteRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveRxDescriptorSyncForCPURet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveRxPayloadSyncForCPURet = static_cast<uint32_t>(kIOReturnNotReady);
uint64_t gDigiLiveRxDescriptorSyncCount = 0;
uint64_t gDigiLiveRxDescriptorSyncBytes = 0;
uint64_t gDigiLiveRxPayloadSyncCount = 0;
uint64_t gDigiLiveRxPayloadSyncBytes = 0;
uint32_t gDigiLiveXmitMaskSupport = 0;
uint32_t gDigiLiveRecvMaskSupport = 0;
uint32_t gDigiLiveXmitContextSupported = 0;
uint32_t gDigiLiveRecvContextSupported = 0;
uint32_t gDigiLiveITCommandPtr = 0;
uint32_t gDigiLiveIRCommandPtr = 0;
uint32_t gDigiLiveIRContextMatch = 0;
uint32_t gDigiLiveITControlAfterRun = 0;
uint32_t gDigiLiveIRControlAfterRun = 0;
uint32_t gDigiLiveITControlAfterStop = 0;
uint32_t gDigiLiveIRControlAfterStop = 0;
uint32_t gDigiLiveITStopLoops = 0;
uint32_t gDigiLiveIRStopLoops = 0;
uint32_t gDigiLiveITMaskAfterRun = 0;
uint32_t gDigiLiveIRMaskAfterRun = 0;
uint32_t gDigiLiveITMaskAfterClear = 0;
uint32_t gDigiLiveIRMaskAfterClear = 0;
uint32_t gDigiLiveITEventAfterRun = 0;
uint32_t gDigiLiveIREventAfterRun = 0;
uint32_t gDigiLiveITCommandPtrLastRead = 0;
uint32_t gDigiLiveIRCommandPtrLastRead = 0;
uint32_t gDigiLiveITFirstPayloadResCount = 0;
uint32_t gDigiLiveITFirstPayloadStatus = 0;
uint32_t gDigiLiveITLastPayloadResCount = 0;
uint32_t gDigiLiveITLastPayloadStatus = 0;
uint64_t gDigiLiveITEventPollCount = 0;
uint64_t gDigiLiveITEventHitCount = 0;
uint64_t gDigiLiveITEventMissCount = 0;
uint64_t gDigiLiveITEventClearCount = 0;
uint32_t gDigiLiveITEventLastBeforeHarvest = 0;
uint32_t gDigiLiveITEventLastAfterClear = 0;
uint64_t gDigiLiveIREventPollCount = 0;
uint64_t gDigiLiveIREventHitCount = 0;
uint64_t gDigiLiveIREventMissCount = 0;
uint64_t gDigiLiveIREventClearCount = 0;
uint64_t gDigiLiveIREventGateSkipCount = 0;
uint64_t gDigiLiveIREventGateBypassCount = 0;
uint32_t gDigiLiveIREventConsecutiveMissCount = 0;
uint32_t gDigiLiveIREventLastBeforeHarvest = 0;
uint32_t gDigiLiveIREventLastAfterClear = 0;
uint32_t gDigiLiveRxHeader0Raw = 0;
uint32_t gDigiLiveRxHeader1Raw = 0;
uint32_t gDigiLiveRxHeader2Raw = 0;
uint32_t gDigiLiveRxHeader3Raw = 0;
uint32_t gDigiLiveRxIsoHeader = 0;
uint32_t gDigiLiveRxTimestamp = 0;
uint32_t gDigiLiveRxCycle = 0xffffffff;
uint32_t gDigiLiveRxExpectedCycle = 0xffffffff;
uint32_t gDigiLiveRxCycleDelta = 0;
uint32_t gDigiLiveRxMaxCycleDelta = 0;
uint64_t gDigiLiveRxCycleLostCount = 0;
uint64_t gDigiLiveRxCyclePacketCount = 0;
uint32_t gDigiLiveRxCIPHeader0 = 0;
uint32_t gDigiLiveRxCIPHeader1 = 0;
uint32_t gDigiLiveRxDBC = 0xffffffff;
uint32_t gDigiLiveRxExpectedDBC = 0xffffffff;
uint32_t gDigiLiveRxDBCDelta = 0;
uint32_t gDigiLiveRxMaxDBCDelta = 0;
uint64_t gDigiLiveRxDBCPacketCount = 0;
uint64_t gDigiLiveRxDBCLostCount = 0;
uint64_t gDigiLiveRxDBCInitCount = 0;
uint32_t gDigiLiveRxSYT = 0xffffffff;
uint64_t gDigiLiveRxSYTNoInfoCount = 0;
uint64_t gDigiLiveRxSYTZeroCount = 0;
uint32_t gDigiLiveRxSID = 0xffffffff;
uint32_t gDigiLiveRxExpectedSID = 0xffffffff;
uint32_t gDigiLiveRxDBS = 0xffffffff;
uint32_t gDigiLiveRxSPH = 0xffffffff;
uint32_t gDigiLiveRxFMT = 0xffffffff;
uint32_t gDigiLiveRxFDF = 0xffffffff;
uint32_t gDigiLiveRxEventCount = 0;
uint32_t gDigiLiveRxMinEventCount = 0xffffffff;
uint32_t gDigiLiveRxMaxEventCount = 0;
uint32_t gDigiLiveRxPayloadRemainderBytes = 0;
uint64_t gDigiLiveRxStreamProcessorPacketCount = 0;
uint64_t gDigiLiveRxStreamProcessorValidPacketCount = 0;
uint64_t gDigiLiveRxPayloadRemainderCount = 0;
uint64_t gDigiLiveRxUnexpectedSIDCount = 0;
uint64_t gDigiLiveRxUnexpectedDBSCount = 0;
uint64_t gDigiLiveRxUnexpectedSPHCount = 0;
uint64_t gDigiLiveRxUnexpectedFMTCount = 0;
uint64_t gDigiLiveRxUnexpectedFDFCount = 0;
uint64_t gDigiLiveRxDataBlockHistogram[9] = {};
uint64_t gDigiLiveRxUnexpectedDataBlockCount = 0;
uint8_t gDigiLiveRxCadencePeriod[kDigiLiveRxCadencePeriodPackets] = {};
uint32_t gDigiLiveRxCadencePeriodCount = 0;
uint32_t gDigiLiveRxCadenceReady = 0;
uint32_t gDigiLiveRxCadenceResetCount = 0;
uint32_t gDigiLiveRxCadenceInvalidCount = 0;
uint32_t gDigiLiveRxCadenceDiscontinuityCount = 0;
uint32_t gDigiLiveRxCadenceObservedTotalDataBlocks = 0;
uint32_t gDigiLiveRxCadenceBadTotalCount = 0;
uint32_t gDigiLiveRxCadenceLastBadTotalDataBlocks = 0;
uint32_t gDigiLiveRxCadenceIdealMismatchCount = 0;
uint32_t gDigiLiveRxCadenceBestPhase = 0xffffffff;
uint32_t gDigiLiveRxCadenceBestPhaseMismatchCount = 0xffffffff;
uint32_t gDigiLiveRxCadenceFirstDataBlocks = 0;
uint32_t gDigiLiveRxCadenceLastDataBlocks = 0;
uint8_t gDigiLiveSequenceReplayPeriod[kDigiLiveSequenceReplayPeriodPackets] = {};
uint32_t gDigiLiveSequenceReplayPeriodCount = 0;
uint32_t gDigiLiveSequenceReplayReady = 0;
uint32_t gDigiLiveSequenceReplayActive = 0;
uint32_t gDigiLiveSequenceReplayApplyAttemptCount = 0;
uint32_t gDigiLiveSequenceReplayApplySuccessCount = 0;
uint32_t gDigiLiveSequenceReplayApplyRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gDigiLiveSequenceReplayResetCount = 0;
uint32_t gDigiLiveSequenceReplayInvalidCount = 0;
uint32_t gDigiLiveSequenceReplayDiscontinuityCount = 0;
uint32_t gDigiLiveSequenceReplayObservedTotalDataBlocks = 0;
uint32_t gDigiLiveSequenceReplayBadTotalCount = 0;
uint32_t gDigiLiveSequenceReplayLastBadTotalDataBlocks = 0;
uint32_t gDigiLiveSequenceReplayIdealMismatchCount = 0;
uint32_t gDigiLiveSequenceReplayFirstDataBlocks = 0;
uint32_t gDigiLiveSequenceReplayLastDataBlocks = 0;
uint8_t gDigiLiveSequenceReplayMovingQueue[kDigiLiveSequenceReplayMovingQueuePackets] = {};
uint8_t gDigiLiveTxDataBlocks[kDigi00xDuplexITPacketCount] = {};
uint32_t gDigiLiveSourceNodeIDField = 0;
uint32_t gDigiLiveITHardwareCursorValid = 0;
uint64_t gDigiLiveITHardwarePacketCursor = 0;
uint32_t gDigiLiveOutputPacketCursorValid = 0;
uint64_t gDigiLiveOutputPacketCursor = 0;
uint32_t gDigiLiveOutputPushInProgress = 0;
uint64_t gDigiLiveOutputPushBusyCount = 0;
uint64_t gDigiLiveOutputPushAttemptCount = 0;
uint64_t gDigiLiveOutputPushSuccessCount = 0;
uint64_t gDigiLiveOutputWorkerPushAudioCount = 0;
uint64_t gDigiLiveOutputWorkerPushSkippedAudioCount = 0;
uint64_t gDigiLiveOutputPacketWriteCount = 0;
uint64_t gDigiLiveOutputFrameWriteCount = 0;
uint64_t gDigiLiveOutputSilentFrameWriteCount = 0;
uint64_t gDigiLiveOutputPlannedSilentFrameWriteCount = 0;
uint64_t gDigiLiveOutputAudioStartCount = 0;
uint64_t gDigiLiveOutputCursorCatchUpCount = 0;
uint32_t gDigiLiveOutputLastCurrentPacketIndex = 0xffffffff;
uint32_t gDigiLiveOutputLastStartPacketIndex = 0xffffffff;
uint32_t gDigiLiveOutputLastPacketCount = 0;
uint32_t gDigiLiveOutputLastFrameCount = 0;
uint32_t gDigiLiveOutputLastSilentFrameCount = 0;
uint32_t gDigiLiveOutputLastRingFillFrames = 0;
uint32_t gDigiLiveOutputLastStartDistancePackets = 0xffffffff;
uint32_t gDigiLiveOutputLastSyncRet = static_cast<uint32_t>(kIOReturnNotReady);
DigiDotState gDigiLiveOutputDotState = {};
uint32_t gDigiLiveOutputDotResetCount = 0;
uint32_t gDigiLiveOutputDotLastInputWordBE = 0;
uint32_t gDigiLiveOutputDotLastOutputWordBE = 0;
uint32_t gDigiLiveOutputDotLastCarry = 0;
uint32_t gDigiLiveSequenceReplayMovingQueueReadIndex = 0;
uint32_t gDigiLiveSequenceReplayMovingQueueWriteIndex = 0;
uint32_t gDigiLiveSequenceReplayMovingQueueCount = 0;
uint64_t gDigiLiveSequenceReplayMovingAppendCount = 0;
uint64_t gDigiLiveSequenceReplayMovingDropCount = 0;
uint64_t gDigiLiveSequenceReplayMovingClearCount = 0;
uint64_t gDigiLiveSequenceReplayMovingDiscontinuityCount = 0;
uint64_t gDigiLiveSequenceReplayMovingInvalidCount = 0;
uint64_t gDigiLiveSequenceReplayMovingUpdateAttemptCount = 0;
uint64_t gDigiLiveSequenceReplayMovingUpdateSuccessCount = 0;
uint64_t gDigiLiveSequenceReplayMovingUpdatePacketCount = 0;
uint64_t gDigiLiveSequenceReplayMovingDryRunSuccessCount = 0;
uint64_t gDigiLiveSequenceReplayMovingDryRunPacketCount = 0;
uint64_t gDigiLiveSequenceReplayMovingShortQueueCount = 0;
uint64_t gDigiLiveSequenceReplayMovingBadTotalCount = 0;
uint64_t gDigiLiveSequenceReplayMovingBadCommandPtrCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadencePhaseUseCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadencePhasePacketCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadenceNotReadyCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadenceMismatchRejectCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadenceLearnCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadenceLearnPacketCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadenceLearnRejectCount = 0;
uint64_t gDigiLiveSequenceReplayMovingCadenceCachedUseCount = 0;
uint64_t gDigiLiveSequenceReplayMovingGuardEligibleCount = 0;
uint64_t gDigiLiveSequenceReplayMovingGuardRejectCount = 0;
uint64_t gDigiLiveSequenceReplayMovingGuardDryRunWouldWriteCount = 0;
uint64_t gDigiLiveSequenceReplayMovingGuardDryRunWouldRejectCount = 0;
uint32_t gDigiLiveSequenceReplayMovingLastCurrentPacketIndex = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastUpdateStartIndex = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastUpdatePackets = 0;
uint32_t gDigiLiveSequenceReplayMovingLastTotalDataBlocks = 0;
uint32_t gDigiLiveSequenceReplayMovingLastRawTotalDataBlocks = 0;
uint32_t gDigiLiveSequenceReplayMovingLastCadencePhase = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastCadenceMismatchCount = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastCadenceSource = 0;
uint32_t gDigiLiveSequenceReplayMovingCachedCadencePhase = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingCachedCadenceMismatchCount = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastStartDistancePackets = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastEndDistancePackets = 0xffffffff;
uint32_t gDigiLiveSequenceReplayMovingLastWindowWrapsHardware = 0;
uint32_t gDigiLiveSequenceReplayMovingLastGuardWouldWrite = 0;
uint32_t gDigiLiveSequenceReplayMovingLastStartDBC = 0;
uint32_t gDigiLiveSequenceReplayMovingLastEndDBC = 0;
uint32_t gDigiLiveSequenceReplayMovingLastSyncRet = static_cast<uint32_t>(kIOReturnNotReady);
uint32_t gAudioInputCallbackCount = 0;
uint32_t gAudioInputLastBufferFrameSize = 0;
uint64_t gAudioInputLastSampleTime = 0;
uint64_t gAudioZeroTimestampHostTime = 0;
int32_t gAudioCapturePCM[kDigi00xDuplexIRCapturePCMFrameLimit]
                        [kDigi00xDuplexIRCapturePCMChannelCount] = {};
int32_t gAudioRingPCM[kAudioRingBufferFrameCount]
                     [kDigi00xDuplexIRCapturePCMChannelCount] = {};
int32_t gAudioLastOutputFrame[kDigi00xDuplexIRCapturePCMChannelCount] = {};
int32_t gAudioOutputRingPCM[kAudioOutputRingBufferFrameCount]
                           [kAudioOutputChannelCount] = {};

kern_return_t
HarvestDigiLiveIsoStream();

void
ResetAudioOutputRingBuffer();

void
ClearAudioOutputBuffer();

void
ResetDigiLiveOutputState();

void
AppendAudioOutputBufferToRing(uint32_t frameCount, uint64_t sampleTime);

kern_return_t
PushAudioOutputToDigiLiveTransmit();

void
RequestAudioRuntimeRestart(uint32_t reason);

bool
DigiLiveStreamMayNeedStop();

uint32_t
Digi00xDuplexDataBlocksForPacket(uint32_t packetIndex);

uint32_t
Digi00xLocalRateIndexForSampleRate(uint32_t sampleRate)
{
    return sampleRate == kDigi00xDuplexSampleRate44100
        ? kDigi00xDuplexLocalRateIndex44100
        : kDigi00xDuplexLocalRateIndex48000;
}

uint32_t
Digi00xCIPSFCForSampleRate(uint32_t sampleRate)
{
    return sampleRate == kDigi00xDuplexSampleRate44100
        ? kDigi00xDuplexCIPSFC44100
        : kDigi00xDuplexCIPSFC48000;
}

bool
Digi00xSampleRateFromDouble(double sampleRate, uint32_t * sampleRateOut)
{
    uint32_t selectedRate = 0;
    if (sampleRate > 44099.0 && sampleRate < 44101.0) {
        selectedRate = kDigi00xDuplexSampleRate44100;
    } else if (sampleRate > 47999.0 && sampleRate < 48001.0) {
        selectedRate = kDigi00xDuplexSampleRate48000;
    } else {
        return false;
    }

    if (sampleRateOut != nullptr) {
        *sampleRateOut = selectedRate;
    }
    return true;
}

void
SetDigi00xRuntimeSampleRate(uint32_t sampleRate)
{
    gDigi00xCurrentSampleRate = sampleRate;
    gDigi00xCurrentLocalRateIndex = Digi00xLocalRateIndexForSampleRate(sampleRate);
    gDigi00xCurrentCIPSFC = Digi00xCIPSFCForSampleRate(sampleRate);
}

uint32_t
DigiLiveSequenceReplayPeriodDataBlocks()
{
    return gDigi00xCurrentSampleRate / 100u;
}

bool
AddNumberProperty(OSDictionary * properties, const char * key, uint64_t value, size_t bits)
{
    OSNumber * number = OSNumber::withNumber(value, bits);
    if (number == nullptr) {
        return false;
    }

    bool ok = properties->setObject(key, number);
    number->release();
    return ok;
}

bool
AddDataProperty(OSDictionary * properties, const char * key, const void * bytes, size_t byteCount)
{
    OSData * data = OSData::withBytes(bytes, byteCount);
    if (data == nullptr) {
        return false;
    }

    bool ok = properties->setObject(key, data);
    data->release();
    return ok;
}

void
PublishStartStage(uint32_t stage, uint32_t value)
{
    if (gDriverInstance == nullptr) {
        return;
    }

    OSDictionary * properties = OSDictionary::withCapacity(4);
    if (properties == nullptr) {
        return;
    }

    AddNumberProperty(properties, "ProbeStartStage", stage, 32);
    AddNumberProperty(properties, "ProbeStartStageValue", value, 32);
    AddNumberProperty(properties, "ProbeStartStageTime", mach_absolute_time(), 64);
    gDriverInstance->SetProperties(properties);
    properties->release();
}

bool
AddIndexedNumberProperty(OSDictionary * properties,
                         const char * prefix,
                         size_t index,
                         const char * suffix,
                         uint64_t value,
                         size_t bits)
{
    char key[96];
    int written = snprintf(key, sizeof(key), "%s%zu%s", prefix, index, suffix);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(key)) {
        return false;
    }

    return AddNumberProperty(properties, key, value, bits);
}

bool
ReadNumberProperty(OSDictionary * properties, const char * key, uint32_t * value)
{
    if (properties == nullptr || key == nullptr || value == nullptr) {
        return false;
    }

    OSObject * object = properties->getObject(key);
    OSNumber * number = OSDynamicCast(OSNumber, object);
    if (number == nullptr) {
        return false;
    }

    *value = number->unsigned32BitValue();
    return true;
}

uint32_t
ReadNumberPropertyOrDefault(OSDictionary * properties, const char * key, uint32_t defaultValue)
{
    uint32_t value = defaultValue;
    (void)ReadNumberProperty(properties, key, &value);
    return value;
}

void
AddDigiLiveControlDebugProperties(OSDictionary * properties)
{
    if (properties == nullptr) {
        return;
    }

    AddNumberProperty(properties,
                      "ProbeControlDebugCommandCount",
                      gDigiLiveControlDebugCommandCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlDebugMessageCount",
                      gDigiLiveControlDebugMessageCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlDebugSkippedCount",
                      gDigiLiveControlDebugSkippedCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastCommand",
                      gDigiLiveControlDebugLastCommand,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastRet",
                      gDigiLiveControlDebugLastRet,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastPort",
                      gDigiLiveControlDebugLastPort,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastStatus",
                      gDigiLiveControlDebugLastStatus,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastData1",
                      gDigiLiveControlDebugLastData1,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastData2",
                      gDigiLiveControlDebugLastData2,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastFaderChannel",
                      gDigiLiveControlDebugLastFaderChannel,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDebugLastFaderTarget10",
                      gDigiLiveControlDebugLastFaderTarget10,
                      32);
}

void
PublishDigiLiveControlDiagnostics(uint32_t rawWordBE,
                                  uint32_t marker,
                                  uint32_t data0,
                                  uint32_t data1,
                                  uint32_t control,
                                  uint32_t portNibble,
                                  uint32_t length)
{
    if (gDriverInstance == nullptr) {
        return;
    }

    OSDictionary * properties = OSDictionary::withCapacity(160);
    if (properties == nullptr) {
        return;
    }

    AddNumberProperty(properties, "ProbeControlMessageCount", gDigiLiveMidiMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlConsoleMessageCount", gDigiLiveMidiConsoleMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlPhysicalMessageCount", gDigiLiveMidiPhysicalMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlInvalidLengthCount", gDigiLiveMidiInvalidLengthCount, 64);
    AddNumberProperty(properties, "ProbeControlLoggedMessageCount", gDigiLiveMidiLoggedMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlRawWordBE", rawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlMarker", marker, 32);
    AddNumberProperty(properties, "ProbeControlData0", data0, 32);
    AddNumberProperty(properties, "ProbeControlData1", data1, 32);
    AddNumberProperty(properties, "ProbeControlControl", control, 32);
    AddNumberProperty(properties, "ProbeControlPortNibble", portNibble, 32);
    AddNumberProperty(properties, "ProbeControlLength", length, 32);
    AddNumberProperty(properties, "ProbeControlTimestamp", mach_absolute_time(), 64);
    AddNumberProperty(properties, "ProbeControlRecentIndex", gDigiLiveMidiRecentIndex, 32);
    AddNumberProperty(properties, "ProbeControlRecentCount", gDigiLiveMidiRecentCount, 32);
    AddNumberProperty(properties, "ProbeControlDecodedMessageCount", gDigiLiveMidiDecodedMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlDecodedNoteMessageCount", gDigiLiveMidiNoteMessageCount, 64);
    AddNumberProperty(properties,
                      "ProbeControlDecodedControlChangeMessageCount",
                      gDigiLiveMidiControlChangeMessageCount,
                      64);
    AddNumberProperty(properties, "ProbeControlDecodedLastMessage", gDigiLiveMidiLastDecodedMessage, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastPort", gDigiLiveMidiLastDecodedPort, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastStatus", gDigiLiveMidiLastDecodedStatus, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastData1", gDigiLiveMidiLastDecodedData1, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastData2", gDigiLiveMidiLastDecodedData2, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastNoteNumber", gDigiLiveMidiLastNoteNumber, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastNoteVelocity", gDigiLiveMidiLastNoteVelocity, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastControlNumber", gDigiLiveMidiLastControlNumber, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastControlValue", gDigiLiveMidiLastControlValue, 32);
    AddNumberProperty(properties, "ProbeControlDecodedRecentIndex", gDigiLiveMidiDecodedRecentIndex, 32);
    AddNumberProperty(properties, "ProbeControlDecodedRecentCount", gDigiLiveMidiDecodedRecentCount, 32);
    AddNumberProperty(properties, "ProbeControlStateMappedMessageCount", gDigiLiveControlMappedMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlStateUnknownMessageCount", gDigiLiveControlUnknownMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlStateLastMappedKind", gDigiLiveControlLastMappedKind, 32);
    AddNumberProperty(properties, "ProbeControlStateLastMappedChannel", gDigiLiveControlLastMappedChannel, 32);
    AddNumberProperty(properties, "ProbeControlStateSelect1Pressed", gDigiLiveControlSelect1Pressed, 32);
    AddNumberProperty(properties, "ProbeControlStateFader1Touched", gDigiLiveControlFader1Touched, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateFader1ControlNumber",
                      gDigiLiveControlFader1ControlNumber,
                      32);
    AddNumberProperty(properties, "ProbeControlStateFader1Value", gDigiLiveControlFader1Value, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateFader1UpdateCount",
                      gDigiLiveControlFader1UpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportRTZPressed",
                      gDigiLiveControlTransportRTZPressed,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportRewindPressed",
                      gDigiLiveControlTransportRewindPressed,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportFastForwardPressed",
                      gDigiLiveControlTransportFastForwardPressed,
                      32);
    AddNumberProperty(properties, "ProbeControlStateStopPressed", gDigiLiveControlStopPressed, 32);
    AddNumberProperty(properties, "ProbeControlStatePlayPressed", gDigiLiveControlPlayPressed, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportRecordPressed",
                      gDigiLiveControlTransportRecordPressed,
                      32);
    AddNumberProperty(properties, "ProbeControlStateArrowLeftPressed", gDigiLiveControlArrowLeftPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateArrowRightPressed", gDigiLiveControlArrowRightPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateArrowUpPressed", gDigiLiveControlArrowUpPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateArrowDownPressed", gDigiLiveControlArrowDownPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateJogWheelValue", gDigiLiveControlJogWheelValue, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateJogWheelDirection",
                      gDigiLiveControlJogWheelDirection,
                      32);
    AddNumberProperty(properties, "ProbeControlStateJogWheelStep", gDigiLiveControlJogWheelStep, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateJogWheelUpdateCount",
                      gDigiLiveControlJogWheelUpdateCount,
                      64);
    AddNumberProperty(properties, "ProbeControlStateShuttleValue", gDigiLiveControlShuttleValue, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateShuttleUpdateCount",
                      gDigiLiveControlShuttleUpdateCount,
                      64);
    AddNumberProperty(properties, "ProbeControlStateModeViewLastNote", gDigiLiveControlModeViewLastNote, 32);
    AddNumberProperty(properties, "ProbeControlStateModeViewLastIndex", gDigiLiveControlModeViewLastIndex, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateModeViewUpdateCount",
                      gDigiLiveControlModeViewUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateAboveTransportLastNote",
                      gDigiLiveControlAboveTransportLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateAboveTransportLastIndex",
                      gDigiLiveControlAboveTransportLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateAboveTransportUpdateCount",
                      gDigiLiveControlAboveTransportUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionLastGroup",
                      gDigiLiveControlTransportSectionLastGroup,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionLastNote",
                      gDigiLiveControlTransportSectionLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionLastIndex",
                      gDigiLiveControlTransportSectionLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionUpdateCount",
                      gDigiLiveControlTransportSectionUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateNavigationModeLedNote",
                      gDigiLiveControlNavigationModeLedNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateHardwareMonitorLastNote",
                      gDigiLiveControlHardwareMonitorLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateHardwareMonitorLastIndex",
                      gDigiLiveControlHardwareMonitorLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateHardwareMonitorUpdateCount",
                      gDigiLiveControlHardwareMonitorUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateDisplayModePressed",
                      gDigiLiveControlDisplayModePressed,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateDisplayModeUpdateCount",
                      gDigiLiveControlDisplayModeUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateEncoderAssignLastNote",
                      gDigiLiveControlEncoderAssignLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateEncoderAssignLastIndex",
                      gDigiLiveControlEncoderAssignLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateEncoderAssignUpdateCount",
                      gDigiLiveControlEncoderAssignUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateRotaryEncoderLastIndex",
                      gDigiLiveControlRotaryEncoderLastIndex,
                      32);
    for (uint32_t i = 0; i < kDigiLiveControlModeViewButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateModeViewButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlModeViewButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlAboveTransportButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateAboveTransportButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlAboveTransportButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlTransportSectionButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateTransportSectionButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlTransportSectionButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlHardwareMonitorButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateHardwareMonitorButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlHardwareMonitorButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlEncoderAssignButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateEncoderAssignButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlEncoderAssignButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlRotaryEncoderCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "Value",
                                 gDigiLiveControlRotaryEncoderValue[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "Direction",
                                 gDigiLiveControlRotaryEncoderDirection[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "Step",
                                 gDigiLiveControlRotaryEncoderStep[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "UpdateCount",
                                 gDigiLiveControlRotaryEncoderUpdateCount[i],
                                 64);
    }
    AddNumberProperty(properties, "ProbeControlMotorTestEnabled", kDigiLiveControlMotorTestEnabled, 32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestTriggerCount",
                      gDigiLiveControlMotorTestTriggerCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestMessageCount",
                      gDigiLiveControlMotorTestMessageCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestSkippedCount",
                      gDigiLiveControlMotorTestSkippedCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastChannel",
                      gDigiLiveControlMotorTestLastChannel,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastTarget10",
                      gDigiLiveControlMotorTestLastTarget10,
                      32);
    AddNumberProperty(properties, "ProbeControlMotorTestLastCC", gDigiLiveControlMotorTestLastCC, 32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastValue",
                      gDigiLiveControlMotorTestLastValue,
                      32);
    AddDigiLiveControlDebugProperties(properties);
    for (uint32_t i = 0; i < kDigiLiveControlChannelStripCount; ++i) {
        uint32_t channel = i + 1;
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "SelectPressed",
                                 gDigiLiveControlChannelSelectPressed[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "SoloPressed",
                                 gDigiLiveControlChannelSoloPressed[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "MutePressed",
                                 gDigiLiveControlChannelMutePressed[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderTouched",
                                 gDigiLiveControlChannelFaderTouched[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderControlNumber",
                                 gDigiLiveControlChannelFaderControlNumber[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderValue",
                                 gDigiLiveControlChannelFaderValue[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderUpdateCount",
                                 gDigiLiveControlChannelFaderUpdateCount[i],
                                 64);
    }
    AddNumberProperty(properties,
                      "ProbeControlRawFragmentEchoEnabled",
                      kDigiLiveMidiRawFragmentEchoEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDecodedFeedbackEnabled",
                      kDigiLiveMidiDecodedFeedbackEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlEchoFaderMoveFeedbackEnabled",
                      kDigiLiveMidiEchoFaderMoveFeedbackEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackSentCount",
                      gDigiLiveControlFaderFeedbackSentCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackSkippedCount",
                      gDigiLiveControlFaderFeedbackSkippedCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackFlushCount",
                      gDigiLiveControlFaderFeedbackFlushCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackLastChannel",
                      gDigiLiveControlFaderFeedbackLastChannel,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackLastCC",
                      gDigiLiveControlFaderFeedbackLastCC,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackLastValue",
                      gDigiLiveControlFaderFeedbackLastValue,
                      32);
    AddNumberProperty(properties, "ProbeControlEchoEnabled", kDigiLiveMidiEchoToOutputEnabled, 32);
    AddNumberProperty(properties,
                      "ProbeControlEchoConsolePortOnlyEnabled",
                      kDigiLiveMidiEchoConsolePortOnlyEnabled,
                      32);
    AddNumberProperty(properties, "ProbeControlEchoQueueCount", gDigiLiveMidiEchoQueueCount, 32);
    AddNumberProperty(properties, "ProbeControlEchoLastQueuedRawWordBE", gDigiLiveMidiEchoLastQueuedRawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlEchoLastTransmitRawWordBE", gDigiLiveMidiEchoLastTransmitRawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlFeedbackMessageCount", gDigiLiveMidiFeedbackMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlFeedbackSkippedCount", gDigiLiveMidiFeedbackSkippedCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoAppendCount", gDigiLiveMidiEchoAppendCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoDropCount", gDigiLiveMidiEchoDropCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoTransmitCount", gDigiLiveMidiEchoTransmitCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoBusyCount", gDigiLiveMidiEchoBusyCount, 64);
    AddDataProperty(properties,
                    "ProbeControlRecentRawWordsBE",
                    gDigiLiveMidiRecentRawWordBE,
                    sizeof(gDigiLiveMidiRecentRawWordBE));
    AddDataProperty(properties,
                    "ProbeControlDecodedRecentMessages",
                    gDigiLiveMidiDecodedRecentMessages,
                    sizeof(gDigiLiveMidiDecodedRecentMessages));
    for (uint32_t i = 0; i < kDigiLiveMidiRecentIndexedPublishCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlRecentRawWord",
                                 i,
                                 "BE",
                                 gDigiLiveMidiRecentRawWordBE[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveMidiDecodedRecentIndexedPublishCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlDecodedRecentMessage",
                                 i,
                                 "",
                                 gDigiLiveMidiDecodedRecentMessages[i],
                                 32);
    }

    gDriverInstance->SetProperties(properties);
    properties->release();
}

uint32_t
ToBigEndian32(uint32_t value)
{
    return ((value & 0x000000ffu) << 24) |
           ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) |
           ((value & 0xff000000u) >> 24);
}

bool
QueueDigiLiveDecodedMidiFeedback(uint32_t portNibble,
                                 uint8_t status,
                                 uint8_t data1,
                                 uint8_t data2);

bool
QueueDigiLiveFaderMotorTarget(uint32_t channel, uint32_t target10);

bool
QueueDigiLiveStoredFaderMoveFeedback(uint32_t channel);

void
PublishDigiLiveControlDebugDiagnostics();

void
DecodeDigiLiveRelativeEncoderValue(uint8_t value,
                                   uint32_t * direction,
                                   uint32_t * step)
{
    if (direction == nullptr || step == nullptr) {
        return;
    }
    if (value > 0x40u) {
        *direction = 1;
        *step = value - 0x40u;
    } else if (value < 0x40u) {
        *direction = 2;
        *step = 0x40u - value;
    } else {
        *direction = 0;
        *step = 0;
    }
}

bool
IsDigiLiveTransportSectionButton(uint32_t noteGroup, uint32_t note)
{
    if (noteGroup == kDigiLiveControlGroupNavigation) {
        return note <= 0x04u || (note >= 0x09u && note <= 0x0du);
    }
    if (noteGroup == kDigiLiveControlGroupTransport) {
        return note <= 0x05u || note == 0x0cu;
    }
    return false;
}

bool
IsDigiLiveNavigationModeLedButton(uint32_t noteGroup, uint32_t note)
{
    return noteGroup == kDigiLiveControlGroupNavigation &&
           note >= kDigiLiveControlNoteNavModeBank &&
           note <= kDigiLiveControlNoteNavModeZoom;
}

uint32_t
DigiLiveTransportSectionButtonIndex(uint32_t noteGroup, uint32_t note)
{
    uint32_t groupOffset = noteGroup == kDigiLiveControlGroupTransport ?
        kDigiLiveControlTransportSectionSlotsPerGroup :
        0u;
    return groupOffset + (note & 0x0fu);
}

void
ObserveDigiLiveMappedControlState(uint32_t portNibble,
                                  uint8_t status,
                                  uint8_t data1,
                                  uint8_t data2)
{
    if (portNibble != kDigiLiveMidiControlPortNibble) {
        return;
    }

    uint8_t command = static_cast<uint8_t>(status & 0xf0u);
    bool mapped = false;

    if (command == 0x90u) {
        uint32_t pressed = (data2 & 0x40u) != 0 ? 1u : 0u;
        uint32_t noteGroup = data2 & 0x0fu;
        uint32_t channel = data2 & 0x07u;
        if (data1 == kDigiLiveControlNoteChannelSelect &&
            noteGroup < kDigiLiveControlChannelStripCount &&
            channel < kDigiLiveControlChannelStripCount) {
            gDigiLiveControlChannelSelectPressed[channel] = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindChannelSelect;
            gDigiLiveControlLastMappedChannel = channel;
            if (channel == 0) {
                gDigiLiveControlSelect1Pressed = pressed;
            }
            if (kDigiLiveControlMotorTestEnabled != 0 &&
                pressed != 0 &&
                gDigiLiveControlPlayPressed != 0) {
                gDigiLiveControlMotorTestTriggerCount++;
                uint32_t target10 = gDigiLiveControlMotorTestToggle[channel] == 0 ?
                    kDigiLiveControlMotorTestHighTarget10 :
                    kDigiLiveControlMotorTestLowTarget10;
                gDigiLiveControlMotorTestToggle[channel] =
                    gDigiLiveControlMotorTestToggle[channel] == 0 ? 1u : 0u;
                if (!QueueDigiLiveFaderMotorTarget(channel, target10)) {
                    gDigiLiveControlMotorTestSkippedCount++;
                }
            }
            mapped = true;
        } else if (data1 == kDigiLiveControlNoteChannelSolo &&
                   noteGroup < kDigiLiveControlChannelStripCount &&
                   channel < kDigiLiveControlChannelStripCount) {
            gDigiLiveControlChannelSoloPressed[channel] = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindChannelSolo;
            gDigiLiveControlLastMappedChannel = channel;
            mapped = true;
        } else if (data1 == kDigiLiveControlNoteChannelMute &&
                   noteGroup < kDigiLiveControlChannelStripCount &&
                   channel < kDigiLiveControlChannelStripCount) {
            gDigiLiveControlChannelMutePressed[channel] = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindChannelMute;
            gDigiLiveControlLastMappedChannel = channel;
            mapped = true;
        } else if (data1 == kDigiLiveControlNoteChannelFaderTouch &&
                   noteGroup < kDigiLiveControlChannelStripCount &&
                   channel < kDigiLiveControlChannelStripCount) {
            gDigiLiveControlChannelFaderTouched[channel] = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindChannelFaderTouch;
            gDigiLiveControlLastMappedChannel = channel;
            if (channel == 0) {
                gDigiLiveControlFader1Touched = pressed;
            }
            if (pressed == 0 &&
                kDigiLiveMidiEchoFaderMoveFeedbackFlushOnTouchRelease != 0) {
                (void)QueueDigiLiveStoredFaderMoveFeedback(channel);
            }
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupTransport &&
                   data1 == kDigiLiveControlNoteTransportRTZ) {
            gDigiLiveControlTransportRTZPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindTransportRTZ;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupTransport &&
                   data1 == kDigiLiveControlNoteTransportRewind) {
            gDigiLiveControlTransportRewindPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindTransportRewind;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupTransport &&
                   data1 == kDigiLiveControlNoteTransportFastForward) {
            gDigiLiveControlTransportFastForwardPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindTransportFastForward;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupTransport &&
                   data1 == kDigiLiveControlNoteStop) {
            gDigiLiveControlStopPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindStop;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupTransport &&
                   data1 == kDigiLiveControlNotePlay) {
            gDigiLiveControlPlayPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindPlay;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupTransport &&
                   data1 == kDigiLiveControlNoteTransportRecord) {
            gDigiLiveControlTransportRecordPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindTransportRecord;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupNavigation &&
                   data1 == kDigiLiveControlNoteArrowLeft) {
            gDigiLiveControlArrowLeftPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindArrowLeft;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupNavigation &&
                   data1 == kDigiLiveControlNoteArrowRight) {
            gDigiLiveControlArrowRightPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindArrowRight;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupNavigation &&
                   data1 == kDigiLiveControlNoteArrowUp) {
            gDigiLiveControlArrowUpPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindArrowUp;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupNavigation &&
                   data1 == kDigiLiveControlNoteArrowDown) {
            gDigiLiveControlArrowDownPressed = pressed;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindArrowDown;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupModeView &&
                   data1 >= kDigiLiveControlModeViewFirstNote &&
                   data1 < kDigiLiveControlModeViewFirstNote + kDigiLiveControlModeViewButtonCount) {
            uint32_t index = data1 - kDigiLiveControlModeViewFirstNote;
            gDigiLiveControlModeViewButtonPressed[index] = pressed;
            gDigiLiveControlModeViewLastNote = data1;
            gDigiLiveControlModeViewLastIndex = index;
            gDigiLiveControlModeViewUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindModeViewButton;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupAboveTransport &&
                   data1 < kDigiLiveControlAboveTransportButtonCount) {
            uint32_t index = data1;
            gDigiLiveControlAboveTransportButtonPressed[index] = pressed;
            gDigiLiveControlAboveTransportLastNote = data1;
            gDigiLiveControlAboveTransportLastIndex = index;
            gDigiLiveControlAboveTransportUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindAboveTransportButton;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlGroupEncoderAssign &&
                   data1 < kDigiLiveControlEncoderAssignButtonCount) {
            uint32_t index = data1;
            gDigiLiveControlEncoderAssignButtonPressed[index] = pressed;
            gDigiLiveControlEncoderAssignLastNote = data1;
            gDigiLiveControlEncoderAssignLastIndex = index;
            gDigiLiveControlEncoderAssignUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindEncoderAssignButton;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (IsDigiLiveTransportSectionButton(noteGroup, data1)) {
            uint32_t index = DigiLiveTransportSectionButtonIndex(noteGroup, data1);
            if (index < kDigiLiveControlTransportSectionButtonCount) {
                gDigiLiveControlTransportSectionButtonPressed[index] = pressed;
                gDigiLiveControlTransportSectionLastGroup = noteGroup;
                gDigiLiveControlTransportSectionLastNote = data1;
                gDigiLiveControlTransportSectionLastIndex = index;
                gDigiLiveControlTransportSectionUpdateCount++;
                gDigiLiveControlLastMappedKind = kDigiLiveControlKindTransportSectionButton;
                gDigiLiveControlLastMappedChannel = 0xffffffff;
                mapped = true;
            }
        } else if (noteGroup == kDigiLiveControlGroupHardwareMonitor &&
                   data1 < kDigiLiveControlHardwareMonitorButtonCount) {
            uint32_t index = data1;
            gDigiLiveControlHardwareMonitorButtonPressed[index] = pressed;
            gDigiLiveControlHardwareMonitorLastNote = data1;
            gDigiLiveControlHardwareMonitorLastIndex = index;
            gDigiLiveControlHardwareMonitorUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindHardwareMonitorButton;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (noteGroup == kDigiLiveControlDisplayModeGroup &&
                   data1 == kDigiLiveControlDisplayModeNote) {
            gDigiLiveControlDisplayModePressed = pressed;
            gDigiLiveControlDisplayModeUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindDisplayModeButton;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        }
    } else if (command == 0xb0u) {
        if (data1 <= 0x3fu) {
            uint32_t channel = data1 & 0x07u;
            if (channel < kDigiLiveControlChannelStripCount) {
                gDigiLiveControlChannelFaderControlNumber[channel] = data1;
                gDigiLiveControlChannelFaderValue[channel] = data2;
                gDigiLiveControlChannelFaderUpdateCount[channel]++;
                gDigiLiveControlLastMappedKind = kDigiLiveControlKindChannelFaderMove;
                gDigiLiveControlLastMappedChannel = channel;
                if (channel == 0) {
                    gDigiLiveControlFader1ControlNumber = data1;
                    gDigiLiveControlFader1Value = data2;
                    gDigiLiveControlFader1UpdateCount++;
                }
                mapped = true;
            }
        } else if (data1 >= kDigiLiveControlCCRotaryEncoderFirst &&
                   data1 < kDigiLiveControlCCRotaryEncoderFirst + kDigiLiveControlRotaryEncoderCount) {
            uint32_t index = data1 - kDigiLiveControlCCRotaryEncoderFirst;
            gDigiLiveControlRotaryEncoderValue[index] = data2;
            DecodeDigiLiveRelativeEncoderValue(data2,
                                               &gDigiLiveControlRotaryEncoderDirection[index],
                                               &gDigiLiveControlRotaryEncoderStep[index]);
            gDigiLiveControlRotaryEncoderUpdateCount[index]++;
            gDigiLiveControlRotaryEncoderLastIndex = index;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindRotaryEncoder;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (data1 == kDigiLiveControlCCJogWheel) {
            gDigiLiveControlJogWheelValue = data2;
            DecodeDigiLiveRelativeEncoderValue(data2,
                                               &gDigiLiveControlJogWheelDirection,
                                               &gDigiLiveControlJogWheelStep);
            gDigiLiveControlJogWheelUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindJogWheel;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        } else if (data1 == kDigiLiveControlCCShuttle) {
            gDigiLiveControlShuttleValue = data2;
            gDigiLiveControlShuttleUpdateCount++;
            gDigiLiveControlLastMappedKind = kDigiLiveControlKindShuttle;
            gDigiLiveControlLastMappedChannel = 0xffffffff;
            mapped = true;
        }
    }

    if (mapped) {
        gDigiLiveControlMappedMessageCount++;
    } else {
        gDigiLiveControlUnknownMessageCount++;
    }
}

void
ObserveDigiLiveDecodedMidiMessage(uint32_t portNibble,
                                  uint8_t status,
                                  uint8_t data1,
                                  uint8_t data2)
{
    uint32_t message = ((portNibble & 0x0fu) << 24) |
                       (static_cast<uint32_t>(status) << 16) |
                       (static_cast<uint32_t>(data1) << 8) |
                       static_cast<uint32_t>(data2);

    gDigiLiveMidiDecodedMessageCount++;
    gDigiLiveMidiLastDecodedMessage = message;
    gDigiLiveMidiLastDecodedPort = portNibble & 0x0fu;
    gDigiLiveMidiLastDecodedStatus = status;
    gDigiLiveMidiLastDecodedData1 = data1;
    gDigiLiveMidiLastDecodedData2 = data2;

    uint8_t command = static_cast<uint8_t>(status & 0xf0u);
    if (command == 0x80u || command == 0x90u) {
        gDigiLiveMidiNoteMessageCount++;
        gDigiLiveMidiLastNoteNumber = data1;
        gDigiLiveMidiLastNoteVelocity = data2;
    } else if (command == 0xb0u) {
        gDigiLiveMidiControlChangeMessageCount++;
        gDigiLiveMidiLastControlNumber = data1;
        gDigiLiveMidiLastControlValue = data2;
    }

    gDigiLiveMidiDecodedRecentMessages[gDigiLiveMidiDecodedRecentIndex] = message;
    gDigiLiveMidiDecodedRecentIndex =
        (gDigiLiveMidiDecodedRecentIndex + 1) % kDigiLiveMidiDecodedRecentMessageCount;
    if (gDigiLiveMidiDecodedRecentCount < kDigiLiveMidiDecodedRecentMessageCount) {
        gDigiLiveMidiDecodedRecentCount++;
    }

    ObserveDigiLiveMappedControlState(portNibble, status, data1, data2);
    (void)QueueDigiLiveDecodedMidiFeedback(portNibble, status, data1, data2);
}

void
ObserveDigiLiveMidiPayloadBytes(uint32_t portNibble,
                                uint32_t data0,
                                uint32_t data1,
                                uint32_t length)
{
    if (portNibble >= kDigiLiveMidiPortCount) {
        return;
    }

    uint8_t bytes[2] = {
        static_cast<uint8_t>(data0 & 0xffu),
        static_cast<uint8_t>(data1 & 0xffu),
    };
    uint32_t byteCount = length;
    if (byteCount > 2) {
        byteCount = 2;
    }

    for (uint32_t i = 0; i < byteCount; ++i) {
        uint8_t byte = bytes[i];
        uint32_t pendingCount = gDigiLiveMidiPendingByteCount[portNibble];

        if ((byte & 0x80u) != 0) {
            gDigiLiveMidiPendingBytes[portNibble][0] = byte;
            gDigiLiveMidiPendingByteCount[portNibble] = 1;
            continue;
        }

        if (pendingCount == 0 || pendingCount >= kDigiLiveMidiBytesPerMessage) {
            gDigiLiveMidiPendingByteCount[portNibble] = 0;
            continue;
        }

        gDigiLiveMidiPendingBytes[portNibble][pendingCount] = byte;
        pendingCount++;
        gDigiLiveMidiPendingByteCount[portNibble] = pendingCount;

        if (pendingCount == kDigiLiveMidiBytesPerMessage) {
            ObserveDigiLiveDecodedMidiMessage(
                portNibble,
                gDigiLiveMidiPendingBytes[portNibble][0],
                gDigiLiveMidiPendingBytes[portNibble][1],
                gDigiLiveMidiPendingBytes[portNibble][2]);
            gDigiLiveMidiPendingByteCount[portNibble] = 0;
        }
    }
}

bool
TryAcquireDigiLiveMidiEchoQueue()
{
    if (__sync_lock_test_and_set(&gDigiLiveMidiEchoQueueBusy, 1) != 0) {
        gDigiLiveMidiEchoBusyCount++;
        return false;
    }
    return true;
}

void
ReleaseDigiLiveMidiEchoQueue()
{
    __sync_lock_release(&gDigiLiveMidiEchoQueueBusy);
}

bool
AppendDigiLiveMidiEchoWordsBE(const uint32_t * words, uint32_t wordCount)
{
    if (kDigiLiveMidiEchoToOutputEnabled == 0) {
        return false;
    }
    if (words == nullptr || wordCount == 0) {
        return false;
    }
    if (!TryAcquireDigiLiveMidiEchoQueue()) {
        return false;
    }

    if (gDigiLiveMidiEchoQueueCount + wordCount > kDigiLiveMidiEchoQueueSize) {
        gDigiLiveMidiEchoDropCount += wordCount;
        ReleaseDigiLiveMidiEchoQueue();
        return false;
    }

    for (uint32_t i = 0; i < wordCount; ++i) {
        uint32_t wordBE = words[i];
        gDigiLiveMidiEchoQueue[gDigiLiveMidiEchoWriteIndex] = wordBE;
        gDigiLiveMidiEchoWriteIndex =
            (gDigiLiveMidiEchoWriteIndex + 1) % kDigiLiveMidiEchoQueueSize;
        gDigiLiveMidiEchoQueueCount++;
        gDigiLiveMidiEchoLastQueuedRawWordBE = wordBE;
        gDigiLiveMidiEchoAppendCount++;
    }
    ReleaseDigiLiveMidiEchoQueue();
    return true;
}

bool
AppendDigiLiveMidiEchoWordBE(uint32_t wordBE)
{
    return AppendDigiLiveMidiEchoWordsBE(&wordBE, 1);
}

bool
QueueDigiLiveMidiMessageToOutput(uint32_t portNibble,
                                 uint8_t status,
                                 uint8_t data1,
                                 uint8_t data2)
{
    if (portNibble >= kDigiLiveMidiPortCount) {
        return false;
    }

    uint32_t controlLength2 = ((portNibble & 0x0fu) << 4) | 0x02u;
    uint32_t controlLength1 = ((portNibble & 0x0fu) << 4) | 0x01u;
    uint32_t words[2] = {
        0x80000000u |
            (static_cast<uint32_t>(status) << 16) |
            (static_cast<uint32_t>(data1) << 8) |
            controlLength2,
        0x80000000u |
            (static_cast<uint32_t>(data2) << 16) |
            controlLength1,
    };

    return AppendDigiLiveMidiEchoWordsBE(words, 2);
}

bool
QueueDigiLiveNavigationModeLed(uint8_t note)
{
    if (!IsDigiLiveNavigationModeLedButton(kDigiLiveControlGroupNavigation, note)) {
        return false;
    }

    bool queued = true;
    for (uint8_t current = kDigiLiveControlNoteNavModeBank;
         current <= kDigiLiveControlNoteNavModeZoom;
         ++current) {
        uint8_t data2 = static_cast<uint8_t>(kDigiLiveControlGroupNavigation);
        if (current == note) {
            data2 |= 0x20u;
        }
        if (!QueueDigiLiveMidiMessageToOutput(kDigiLiveMidiControlPortNibble,
                                              0x90u,
                                              current,
                                              data2)) {
            queued = false;
        }
    }

    if (queued) {
        gDigiLiveControlNavigationModeLedNote = note;
    }
    return queued;
}

bool
QueueDigiLiveMidiBytesToOutput(uint32_t portNibble,
                               const uint8_t * bytes,
                               uint32_t byteCount)
{
    if (portNibble >= kDigiLiveMidiPortCount ||
        bytes == nullptr ||
        byteCount == 0 ||
        byteCount > kDigiLiveControlDebugMaxByteMessageLength) {
        return false;
    }

    uint32_t wordCount = (byteCount + 1u) / 2u;
    if (wordCount > kDigiLiveControlDebugMaxByteMessageLength / 2u) {
        return false;
    }

    uint32_t words[kDigiLiveControlDebugMaxByteMessageLength / 2u] = {};
    for (uint32_t byteIndex = 0, wordIndex = 0; byteIndex < byteCount; ++wordIndex) {
        uint32_t remaining = byteCount - byteIndex;
        uint32_t fragmentLength = remaining >= 2u ? 2u : 1u;
        uint32_t control = ((portNibble & 0x0fu) << 4) | fragmentLength;
        uint32_t word = 0x80000000u |
                        (static_cast<uint32_t>(bytes[byteIndex]) << 16) |
                        control;
        if (fragmentLength == 2u) {
            word |= static_cast<uint32_t>(bytes[byteIndex + 1u]) << 8;
        }
        words[wordIndex] = word;
        byteIndex += fragmentLength;
    }

    return AppendDigiLiveMidiEchoWordsBE(words, wordCount);
}

bool
QueueDigiLiveFaderMoveFeedback(uint32_t channel, uint8_t cc, uint8_t value, bool force)
{
    if (kDigiLiveMidiEchoFaderMoveFeedbackEnabled == 0) {
        gDigiLiveControlFaderFeedbackSkippedCount++;
        return false;
    }
    if (channel >= kDigiLiveControlChannelStripCount) {
        gDigiLiveControlFaderFeedbackSkippedCount++;
        return false;
    }

    bool shouldQueue = force;
    if (!shouldQueue) {
        gDigiLiveControlFaderFeedbackMovesSinceSend[channel]++;
        uint32_t sinceSend = gDigiLiveControlFaderFeedbackMovesSinceSend[channel];
        uint32_t lastValue = gDigiLiveControlFaderFeedbackLastSentValue[channel];
        uint32_t delta = value > lastValue ? value - lastValue : lastValue - value;
        shouldQueue = gDigiLiveControlFaderFeedbackLastSentValid[channel] == 0 ||
                      sinceSend >= kDigiLiveMidiEchoFaderMoveFeedbackStride ||
                      delta >= kDigiLiveMidiEchoFaderMoveFeedbackMinDelta;
    } else {
        gDigiLiveControlFaderFeedbackFlushCount++;
    }

    if (!shouldQueue) {
        gDigiLiveControlFaderFeedbackSkippedCount++;
        return false;
    }

    bool queued = QueueDigiLiveMidiMessageToOutput(kDigiLiveMidiControlPortNibble,
                                                   0xb0u,
                                                   cc,
                                                   value);
    if (!queued) {
        gDigiLiveControlFaderFeedbackSkippedCount++;
        return false;
    }

    gDigiLiveControlFaderFeedbackMovesSinceSend[channel] = 0;
    gDigiLiveControlFaderFeedbackLastSentValue[channel] = value;
    gDigiLiveControlFaderFeedbackLastSentValid[channel] = 1;
    gDigiLiveControlFaderFeedbackSentCount++;
    gDigiLiveControlFaderFeedbackLastChannel = channel;
    gDigiLiveControlFaderFeedbackLastCC = cc;
    gDigiLiveControlFaderFeedbackLastValue = value;
    return true;
}

bool
QueueDigiLiveStoredFaderMoveFeedback(uint32_t channel)
{
    if (channel >= kDigiLiveControlChannelStripCount ||
        gDigiLiveControlChannelFaderUpdateCount[channel] == 0) {
        gDigiLiveControlFaderFeedbackSkippedCount++;
        return false;
    }

    return QueueDigiLiveFaderMoveFeedback(
        channel,
        static_cast<uint8_t>(gDigiLiveControlChannelFaderControlNumber[channel] & 0xffu),
        static_cast<uint8_t>(gDigiLiveControlChannelFaderValue[channel] & 0xffu),
        true);
}

bool
QueueDigiLiveDecodedMidiFeedback(uint32_t portNibble,
                                 uint8_t status,
                                 uint8_t data1,
                                 uint8_t data2)
{
    if (kDigiLiveMidiDecodedFeedbackEnabled == 0) {
        return false;
    }
    if (portNibble != kDigiLiveMidiControlPortNibble) {
        gDigiLiveMidiFeedbackSkippedCount++;
        return false;
    }

    uint8_t command = static_cast<uint8_t>(status & 0xf0u);
    if (command != 0x80u && command != 0x90u && command != 0xb0u) {
        gDigiLiveMidiFeedbackSkippedCount++;
        return false;
    }
    if (command == 0xb0u && data1 > 0x3fu) {
        gDigiLiveMidiFeedbackSkippedCount++;
        return false;
    }
    if (command == 0xb0u &&
        data1 <= 0x3fu &&
        kDigiLiveMidiEchoFaderMoveFeedbackEnabled == 0) {
        gDigiLiveMidiFeedbackSkippedCount++;
        return false;
    }
    if (command == 0xb0u && data1 <= 0x3fu) {
        uint32_t channel = data1 & 0x07u;
        if (QueueDigiLiveFaderMoveFeedback(channel, data1, data2, false)) {
            gDigiLiveMidiFeedbackMessageCount++;
            return true;
        }
        gDigiLiveMidiFeedbackSkippedCount++;
        return false;
    }
    if (command == 0x80u || command == 0x90u) {
        uint8_t noteGroup = static_cast<uint8_t>(data2 & 0x0fu);
        if (IsDigiLiveNavigationModeLedButton(noteGroup, data1)) {
            bool active = (data2 & 0x60u) != 0;
            if (active) {
                if (QueueDigiLiveNavigationModeLed(data1)) {
                    gDigiLiveMidiFeedbackMessageCount++;
                    return true;
                }
            }
            gDigiLiveMidiFeedbackSkippedCount++;
            return false;
        }
        if (noteGroup == kDigiLiveControlGroupAboveTransport ||
            noteGroup == kDigiLiveControlGroupHardwareMonitor ||
            (noteGroup == kDigiLiveControlDisplayModeGroup &&
             data1 == kDigiLiveControlDisplayModeNote)) {
            gDigiLiveMidiFeedbackSkippedCount++;
            return false;
        }
    }

    if (QueueDigiLiveMidiMessageToOutput(portNibble, status, data1, data2)) {
        gDigiLiveMidiFeedbackMessageCount++;
        return true;
    } else {
        gDigiLiveMidiFeedbackSkippedCount++;
        return false;
    }
}

bool
QueueDigiLiveFaderMotorTarget(uint32_t channel, uint32_t target10)
{
    if (channel >= kDigiLiveControlChannelStripCount) {
        return false;
    }
    if (target10 > 1023u) {
        target10 = 1023u;
    }

    uint8_t coarse = static_cast<uint8_t>((target10 >> 3) & 0x7fu);
    uint8_t cc = static_cast<uint8_t>(((target10 & 0x07u) << 3) | (channel & 0x07u));
    bool queued = QueueDigiLiveMidiMessageToOutput(kDigiLiveMidiControlPortNibble,
                                                   0xb0u,
                                                   cc,
                                                   coarse);
    if (queued) {
        gDigiLiveControlMotorTestMessageCount++;
        gDigiLiveControlMotorTestLastChannel = channel;
        gDigiLiveControlMotorTestLastTarget10 = target10;
        gDigiLiveControlMotorTestLastCC = cc;
        gDigiLiveControlMotorTestLastValue = coarse;
    }
    return queued;
}

kern_return_t
QueueDigiLiveControlDebugMidiCommand(uint32_t portNibble,
                                     uint32_t status,
                                     uint32_t data1,
                                     uint32_t data2)
{
    gDigiLiveControlDebugCommandCount++;
    gDigiLiveControlDebugLastCommand = kDigiLiveControlDebugCommandMidiMessage;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnBadArgument);

    if (portNibble >= kDigiLiveMidiPortCount ||
        status > 0xffu ||
        data1 > 0xffu ||
        data2 > 0xffu) {
        gDigiLiveControlDebugSkippedCount++;
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnBadArgument;
    }

    gDigiLiveControlDebugLastPort = portNibble;
    gDigiLiveControlDebugLastStatus = status;
    gDigiLiveControlDebugLastData1 = data1;
    gDigiLiveControlDebugLastData2 = data2;
    gDigiLiveControlDebugLastFaderChannel = 0xffffffff;
    gDigiLiveControlDebugLastFaderTarget10 = 0;

    bool queued = QueueDigiLiveMidiMessageToOutput(portNibble,
                                                   static_cast<uint8_t>(status),
                                                   static_cast<uint8_t>(data1),
                                                   static_cast<uint8_t>(data2));
    if (queued) {
        gDigiLiveControlDebugMessageCount++;
        gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnSuccess);
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnSuccess;
    }

    gDigiLiveControlDebugSkippedCount++;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnNoResources);
    PublishDigiLiveControlDebugDiagnostics();
    return kIOReturnNoResources;
}

kern_return_t
QueueDigiLiveControlDebugMidiBytes(uint32_t portNibble,
                                   const uint8_t * bytes,
                                   uint32_t byteCount)
{
    gDigiLiveControlDebugCommandCount++;
    gDigiLiveControlDebugLastCommand = kDigiLiveControlDebugCommandMidiBytes;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnBadArgument);

    if (portNibble >= kDigiLiveMidiPortCount ||
        bytes == nullptr ||
        byteCount == 0 ||
        byteCount > kDigiLiveControlDebugMaxByteMessageLength) {
        gDigiLiveControlDebugSkippedCount++;
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnBadArgument;
    }

    gDigiLiveControlDebugLastPort = portNibble;
    gDigiLiveControlDebugLastStatus = bytes[0];
    gDigiLiveControlDebugLastData1 = byteCount > 1u ? bytes[1] : 0;
    gDigiLiveControlDebugLastData2 = byteCount > 2u ? bytes[2] : 0;
    gDigiLiveControlDebugLastFaderChannel = 0xffffffff;
    gDigiLiveControlDebugLastFaderTarget10 = byteCount;

    bool queued = QueueDigiLiveMidiBytesToOutput(portNibble, bytes, byteCount);
    if (queued) {
        gDigiLiveControlDebugMessageCount++;
        gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnSuccess);
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnSuccess;
    }

    gDigiLiveControlDebugSkippedCount++;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnNoResources);
    PublishDigiLiveControlDebugDiagnostics();
    return kIOReturnNoResources;
}

kern_return_t
QueueDigiLiveControlDebugFaderTarget(uint32_t channel, uint32_t target10)
{
    gDigiLiveControlDebugCommandCount++;
    gDigiLiveControlDebugLastCommand = kDigiLiveControlDebugCommandFaderTarget;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnBadArgument);

    if (channel < 1u ||
        channel > kDigiLiveControlChannelStripCount ||
        target10 > 1023u) {
        gDigiLiveControlDebugSkippedCount++;
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnBadArgument;
    }

    gDigiLiveControlDebugLastPort = kDigiLiveMidiControlPortNibble;
    gDigiLiveControlDebugLastStatus = 0xb0u;
    gDigiLiveControlDebugLastData1 = ((target10 & 0x07u) << 3) | ((channel - 1u) & 0x07u);
    gDigiLiveControlDebugLastData2 = (target10 >> 3) & 0x7fu;
    gDigiLiveControlDebugLastFaderChannel = channel;
    gDigiLiveControlDebugLastFaderTarget10 = target10;

    bool queued = QueueDigiLiveFaderMotorTarget(channel - 1u, target10);
    if (queued) {
        gDigiLiveControlDebugMessageCount++;
        gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnSuccess);
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnSuccess;
    }

    gDigiLiveControlDebugSkippedCount++;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnNoResources);
    PublishDigiLiveControlDebugDiagnostics();
    return kIOReturnNoResources;
}

void
PublishDigiLiveControlDebugDiagnostics()
{
    if (gDriverInstance == nullptr) {
        return;
    }

    OSDictionary * properties = OSDictionary::withCapacity(16);
    if (properties == nullptr) {
        return;
    }

    AddDigiLiveControlDebugProperties(properties);
    AddNumberProperty(properties, "ProbeControlEchoQueueCount", gDigiLiveMidiEchoQueueCount, 32);
    AddNumberProperty(properties, "ProbeControlEchoLastQueuedRawWordBE", gDigiLiveMidiEchoLastQueuedRawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlEchoDropCount", gDigiLiveMidiEchoDropCount, 64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastChannel",
                      gDigiLiveControlMotorTestLastChannel,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastTarget10",
                      gDigiLiveControlMotorTestLastTarget10,
                      32);
    gDriverInstance->SetProperties(properties);
    properties->release();
}

kern_return_t
ProcessDigiLiveControlDebugSetProperties(OSDictionary * properties)
{
    uint32_t command = 0;
    if (!ReadNumberProperty(properties, "ProbeControlDebugCommand", &command)) {
        return kIOReturnUnsupported;
    }

    gDigiLiveControlDebugCommandCount++;
    gDigiLiveControlDebugLastCommand = command;
    gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnBadArgument);

    if (command == kDigiLiveControlDebugCommandMidiMessage) {
        uint32_t status = 0;
        uint32_t data1 = 0;
        uint32_t data2 = 0;
        uint32_t portNibble =
            ReadNumberPropertyOrDefault(properties,
                                        "ProbeControlDebugPort",
                                        kDigiLiveMidiControlPortNibble);

        if (!ReadNumberProperty(properties, "ProbeControlDebugStatus", &status) ||
            !ReadNumberProperty(properties, "ProbeControlDebugData1", &data1) ||
            !ReadNumberProperty(properties, "ProbeControlDebugData2", &data2) ||
            portNibble >= kDigiLiveMidiPortCount ||
            status > 0xffu ||
            data1 > 0xffu ||
            data2 > 0xffu) {
            gDigiLiveControlDebugSkippedCount++;
            PublishDigiLiveControlDebugDiagnostics();
            return kIOReturnBadArgument;
        }

        gDigiLiveControlDebugLastPort = portNibble;
        gDigiLiveControlDebugLastStatus = status;
        gDigiLiveControlDebugLastData1 = data1;
        gDigiLiveControlDebugLastData2 = data2;
        gDigiLiveControlDebugLastFaderChannel = 0xffffffff;
        gDigiLiveControlDebugLastFaderTarget10 = 0;

        bool queued = QueueDigiLiveMidiMessageToOutput(portNibble,
                                                       static_cast<uint8_t>(status),
                                                       static_cast<uint8_t>(data1),
                                                       static_cast<uint8_t>(data2));
        if (queued) {
            gDigiLiveControlDebugMessageCount++;
            gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnSuccess);
            PublishDigiLiveControlDebugDiagnostics();
            return kIOReturnSuccess;
        }

        gDigiLiveControlDebugSkippedCount++;
        gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnNoResources);
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnNoResources;
    }

    if (command == kDigiLiveControlDebugCommandFaderTarget) {
        uint32_t channel = 0;
        uint32_t target10 = 0;
        if (!ReadNumberProperty(properties, "ProbeControlDebugFaderChannel", &channel) ||
            !ReadNumberProperty(properties, "ProbeControlDebugFaderTarget10", &target10) ||
            channel < 1u ||
            channel > kDigiLiveControlChannelStripCount ||
            target10 > 1023u) {
            gDigiLiveControlDebugSkippedCount++;
            PublishDigiLiveControlDebugDiagnostics();
            return kIOReturnBadArgument;
        }

        gDigiLiveControlDebugLastPort = kDigiLiveMidiControlPortNibble;
        gDigiLiveControlDebugLastStatus = 0xb0u;
        gDigiLiveControlDebugLastData1 = ((target10 & 0x07u) << 3) | ((channel - 1u) & 0x07u);
        gDigiLiveControlDebugLastData2 = (target10 >> 3) & 0x7fu;
        gDigiLiveControlDebugLastFaderChannel = channel;
        gDigiLiveControlDebugLastFaderTarget10 = target10;

        bool queued = QueueDigiLiveFaderMotorTarget(channel - 1u, target10);
        if (queued) {
            gDigiLiveControlDebugMessageCount++;
            gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnSuccess);
            PublishDigiLiveControlDebugDiagnostics();
            return kIOReturnSuccess;
        }

        gDigiLiveControlDebugSkippedCount++;
        gDigiLiveControlDebugLastRet = static_cast<uint32_t>(kIOReturnNoResources);
        PublishDigiLiveControlDebugDiagnostics();
        return kIOReturnNoResources;
    }

    gDigiLiveControlDebugSkippedCount++;
    PublishDigiLiveControlDebugDiagnostics();
    return kIOReturnUnsupported;
}

uint32_t
PopDigiLiveMidiEchoWordBE()
{
    if (kDigiLiveMidiEchoToOutputEnabled == 0) {
        return 0x80000000u;
    }
    if (!TryAcquireDigiLiveMidiEchoQueue()) {
        return 0x80000000u;
    }

    if (gDigiLiveMidiEchoQueueCount == 0) {
        ReleaseDigiLiveMidiEchoQueue();
        return 0x80000000u;
    }

    uint32_t wordBE = gDigiLiveMidiEchoQueue[gDigiLiveMidiEchoReadIndex];
    gDigiLiveMidiEchoReadIndex =
        (gDigiLiveMidiEchoReadIndex + 1) % kDigiLiveMidiEchoQueueSize;
    gDigiLiveMidiEchoQueueCount--;
    gDigiLiveMidiEchoLastTransmitRawWordBE = wordBE;
    gDigiLiveMidiEchoTransmitCount++;
    ReleaseDigiLiveMidiEchoQueue();
    return wordBE;
}

void
ObserveDigiLiveMidiSlot0(uint32_t slot0WordBE)
{
    uint32_t marker = (slot0WordBE >> 24) & 0xffu;
    uint32_t data0 = (slot0WordBE >> 16) & 0xffu;
    uint32_t data1 = (slot0WordBE >> 8) & 0xffu;
    uint32_t control = slot0WordBE & 0xffu;
    uint32_t length = control & 0x0fu;
    uint32_t portNibble = (control >> 4) & 0x0fu;

    if (length == 0) {
        return;
    }

    gDigiLiveMidiLastRawWordBE = slot0WordBE;
    gDigiLiveMidiLastMarker = marker;
    gDigiLiveMidiLastData0 = data0;
    gDigiLiveMidiLastData1 = data1;
    gDigiLiveMidiLastControl = control;
    gDigiLiveMidiLastPortNibble = portNibble;
    gDigiLiveMidiLastLength = length;

    if (length > 2) {
        gDigiLiveMidiInvalidLengthCount++;
        return;
    }

    gDigiLiveMidiMessageCount++;
    if (portNibble != 0) {
        gDigiLiveMidiConsoleMessageCount++;
    } else {
        gDigiLiveMidiPhysicalMessageCount++;
    }
    ObserveDigiLiveMidiPayloadBytes(portNibble, data0, data1, length);

    if (kDigiLiveMidiRawFragmentEchoEnabled != 0 &&
        (kDigiLiveMidiEchoConsolePortOnlyEnabled == 0 || portNibble != 0)) {
        (void)AppendDigiLiveMidiEchoWordBE(slot0WordBE);
    }

    gDigiLiveMidiLastMessageRawWordBE = slot0WordBE;
    gDigiLiveMidiLastMessageMarker = marker;
    gDigiLiveMidiLastMessageData0 = data0;
    gDigiLiveMidiLastMessageData1 = data1;
    gDigiLiveMidiLastMessageControl = control;
    gDigiLiveMidiLastMessagePortNibble = portNibble;
    gDigiLiveMidiLastMessageLength = length;

    gDigiLiveMidiRecentRawWordBE[gDigiLiveMidiRecentIndex] = slot0WordBE;
    gDigiLiveMidiRecentIndex =
        (gDigiLiveMidiRecentIndex + 1) % kDigiLiveMidiRecentMessageCount;
    if (gDigiLiveMidiRecentCount < kDigiLiveMidiRecentMessageCount) {
        gDigiLiveMidiRecentCount++;
    }

    gDigiLiveMidiLoggedMessageCount++;
    os_log(OS_LOG_DEFAULT,
           "FireWireOHCIProbe: digi003 slot0 midi/control msg=%llu rawBE=0x%08x marker=0x%02x data0=0x%02x data1=0x%02x control=0x%02x port=0x%x len=%u",
           gDigiLiveMidiLoggedMessageCount,
           slot0WordBE,
           marker,
           data0,
           data1,
           control,
           portNibble,
           length);
    PublishDigiLiveControlDiagnostics(slot0WordBE,
                                      marker,
                                      data0,
                                      data1,
                                      control,
                                      portNibble,
                                      length);
}

uint8_t
DigiDotSalt(uint8_t idx, uint32_t off)
{
    static const uint8_t len[16] = {
        0, 1, 3, 5, 7, 9, 11, 13, 14, 12, 10, 8, 6, 4, 2, 0,
    };
    static const uint8_t nib[15] = {
        0x8, 0x7, 0x9, 0x6, 0xa, 0x5, 0xb, 0x4, 0xc, 0x3, 0xd, 0x2, 0xe, 0x1, 0xf,
    };
    static const uint8_t hir[15] = {
        0x0, 0x6, 0xf, 0x8, 0x7, 0x5, 0x3, 0x4, 0xc, 0xd, 0xe, 0x1, 0x2, 0xb, 0xa,
    };
    static const uint8_t hio[16] = {
        0, 11, 12, 6, 7, 5, 1, 4, 3, 0x00, 14, 13, 8, 9, 10, 2,
    };

    uint8_t lowNibble = idx & 0x0fu;
    uint8_t highNibble = (idx >> 4) & 0x0fu;
    uint8_t highResult = highNibble == 0x9
        ? 0x9
        : hir[(hio[highNibble] + off) % 15];
    if (len[lowNibble] < off) {
        return 0;
    }
    return static_cast<uint8_t>(nib[14 + off - len[lowNibble]] | (highResult << 4));
}

void
ResetDigiDotState(DigiDotState * state)
{
    if (state == nullptr) {
        return;
    }
    state->carry = 0;
    state->idx = 0;
    state->off = 0;
}

uint32_t
DigiDotEncodeAM824Word(DigiDotState * state, uint32_t wordBE)
{
    if (state == nullptr) {
        return wordBE;
    }

    uint8_t magicByte = static_cast<uint8_t>((wordBE >> 8) & 0xffu);
    if (magicByte != 0) {
        state->off = 0;
        state->idx = magicByte ^ state->carry;
    }
    magicByte ^= state->carry;
    uint32_t encodedWordBE =
        (wordBE & 0xffff00ffu) | (static_cast<uint32_t>(magicByte) << 8);
    state->carry = DigiDotSalt(state->idx, ++state->off);

    gDigiLiveOutputDotLastInputWordBE = wordBE;
    gDigiLiveOutputDotLastOutputWordBE = encodedWordBE;
    gDigiLiveOutputDotLastCarry = state->carry;
    return encodedWordBE;
}

DigiLiveCIPHeader
ParseDigiLiveCIPHeader(uint32_t cipHeader0, uint32_t cipHeader1)
{
    DigiLiveCIPHeader header = {
        (cipHeader0 >> 24) & 0x3fu,
        (cipHeader0 >> 16) & 0xffu,
        (cipHeader0 >> 10) & 0x01u,
        cipHeader0 & 0xffu,
        (cipHeader1 >> 24) & 0x3fu,
        (cipHeader1 >> 16) & 0xffu,
        cipHeader1 & 0xffffu,
    };
    return header;
}

uint32_t
DigiLiveDBCForwardDistance(uint32_t expected, uint32_t observed)
{
    if (expected == 0xffffffff || observed == 0xffffffff) {
        return 0;
    }
    return (observed + 256u - expected) & 0xffu;
}

void
ResetDigiLiveRxCadenceState()
{
    for (uint32_t i = 0; i < kDigiLiveRxCadencePeriodPackets; ++i) {
        gDigiLiveRxCadencePeriod[i] = 0;
    }
    gDigiLiveRxCadencePeriodCount = 0;
    gDigiLiveRxCadenceReady = 0;
    gDigiLiveRxCadenceResetCount = 0;
    gDigiLiveRxCadenceInvalidCount = 0;
    gDigiLiveRxCadenceDiscontinuityCount = 0;
    gDigiLiveRxCadenceObservedTotalDataBlocks = 0;
    gDigiLiveRxCadenceBadTotalCount = 0;
    gDigiLiveRxCadenceLastBadTotalDataBlocks = 0;
    gDigiLiveRxCadenceIdealMismatchCount = 0;
    gDigiLiveRxCadenceBestPhase = 0xffffffff;
    gDigiLiveRxCadenceBestPhaseMismatchCount = 0xffffffff;
    gDigiLiveRxCadenceFirstDataBlocks = 0;
    gDigiLiveRxCadenceLastDataBlocks = 0;
}

void
ResetDigiLiveReceiveStreamDiagnostics()
{
    gDigiLiveRxHeader0Raw = 0;
    gDigiLiveRxHeader1Raw = 0;
    gDigiLiveRxHeader2Raw = 0;
    gDigiLiveRxHeader3Raw = 0;
    gDigiLiveRxIsoHeader = 0;
    gDigiLiveRxTimestamp = 0;
    gDigiLiveRxCycle = 0xffffffff;
    gDigiLiveRxExpectedCycle = 0xffffffff;
    gDigiLiveRxCycleDelta = 0;
    gDigiLiveRxMaxCycleDelta = 0;
    gDigiLiveRxCycleLostCount = 0;
    gDigiLiveRxCyclePacketCount = 0;
    gDigiLiveRxCIPHeader0 = 0;
    gDigiLiveRxCIPHeader1 = 0;
    gDigiLiveRxDBC = 0xffffffff;
    gDigiLiveRxExpectedDBC = 0xffffffff;
    gDigiLiveRxDBCDelta = 0;
    gDigiLiveRxMaxDBCDelta = 0;
    gDigiLiveRxDBCPacketCount = 0;
    gDigiLiveRxDBCLostCount = 0;
    gDigiLiveRxDBCInitCount = 0;
    gDigiLiveRxSYT = 0xffffffff;
    gDigiLiveRxSYTNoInfoCount = 0;
    gDigiLiveRxSYTZeroCount = 0;
    gDigiLiveRxSID = 0xffffffff;
    gDigiLiveRxExpectedSID = 0xffffffff;
    gDigiLiveRxDBS = 0xffffffff;
    gDigiLiveRxSPH = 0xffffffff;
    gDigiLiveRxFMT = 0xffffffff;
    gDigiLiveRxFDF = 0xffffffff;
    gDigiLiveRxEventCount = 0;
    gDigiLiveRxMinEventCount = 0xffffffff;
    gDigiLiveRxMaxEventCount = 0;
    gDigiLiveRxPayloadRemainderBytes = 0;
    gDigiLiveRxStreamProcessorPacketCount = 0;
    gDigiLiveRxStreamProcessorValidPacketCount = 0;
    gDigiLiveRxPayloadRemainderCount = 0;
    gDigiLiveRxUnexpectedSIDCount = 0;
    gDigiLiveRxUnexpectedDBSCount = 0;
    gDigiLiveRxUnexpectedSPHCount = 0;
    gDigiLiveRxUnexpectedFMTCount = 0;
    gDigiLiveRxUnexpectedFDFCount = 0;
    for (size_t i = 0;
         i < sizeof(gDigiLiveRxDataBlockHistogram) / sizeof(gDigiLiveRxDataBlockHistogram[0]);
         ++i) {
        gDigiLiveRxDataBlockHistogram[i] = 0;
    }
    gDigiLiveRxUnexpectedDataBlockCount = 0;
    ResetDigiLiveRxCadenceState();
}

void
ResetDigiLiveRxCadenceCapture()
{
    if (gDigiLiveRxCadencePeriodCount != 0) {
        gDigiLiveRxCadenceResetCount++;
    }
    for (uint32_t i = 0; i < kDigiLiveRxCadencePeriodPackets; ++i) {
        gDigiLiveRxCadencePeriod[i] = 0;
    }
    gDigiLiveRxCadencePeriodCount = 0;
    gDigiLiveRxCadenceObservedTotalDataBlocks = 0;
    gDigiLiveRxCadenceFirstDataBlocks = 0;
    gDigiLiveRxCadenceLastDataBlocks = 0;
    gDigiLiveRxCadenceBestPhase = 0xffffffff;
    gDigiLiveRxCadenceBestPhaseMismatchCount = 0xffffffff;
}

void
RecordDigiLiveRxCadencePacket(uint32_t eventCount, bool continuous)
{
    if (gDigiLiveRxCadenceReady != 0) {
        return;
    }

    if (eventCount != 5 && eventCount != 6) {
        gDigiLiveRxCadenceInvalidCount++;
        ResetDigiLiveRxCadenceCapture();
        return;
    }
    if (!continuous) {
        gDigiLiveRxCadenceDiscontinuityCount++;
        ResetDigiLiveRxCadenceCapture();
        return;
    }

    uint32_t index = gDigiLiveRxCadencePeriodCount;
    gDigiLiveRxCadencePeriod[index] = static_cast<uint8_t>(eventCount);
    gDigiLiveRxCadenceObservedTotalDataBlocks += eventCount;
    if (index == 0) {
        gDigiLiveRxCadenceFirstDataBlocks = eventCount;
    }
    gDigiLiveRxCadenceLastDataBlocks = eventCount;
    if (eventCount != Digi00xDuplexDataBlocksForPacket(index)) {
        gDigiLiveRxCadenceIdealMismatchCount++;
    }

    gDigiLiveRxCadencePeriodCount++;
    if (gDigiLiveRxCadencePeriodCount >= kDigiLiveRxCadencePeriodPackets) {
        if (gDigiLiveRxCadenceObservedTotalDataBlocks ==
            DigiLiveSequenceReplayPeriodDataBlocks()) {
            uint32_t bestMismatchCount = 0xffffffff;
            uint32_t bestPhase = 0xffffffff;
            for (uint32_t phase = 0; phase < kDigiLiveRxCadencePeriodPackets; ++phase) {
                uint32_t mismatchCount = 0;
                for (uint32_t packet = 0; packet < kDigiLiveRxCadencePeriodPackets; ++packet) {
                    uint32_t idealPacket = (packet + phase) % kDigiLiveRxCadencePeriodPackets;
                    if (gDigiLiveRxCadencePeriod[packet] !=
                        Digi00xDuplexDataBlocksForPacket(idealPacket)) {
                        mismatchCount++;
                    }
                }
                if (mismatchCount < bestMismatchCount) {
                    bestMismatchCount = mismatchCount;
                    bestPhase = phase;
                }
            }
            gDigiLiveRxCadenceBestPhase = bestPhase;
            gDigiLiveRxCadenceBestPhaseMismatchCount = bestMismatchCount;
            gDigiLiveRxCadenceReady = 1;
        } else {
            gDigiLiveRxCadenceBadTotalCount++;
            gDigiLiveRxCadenceLastBadTotalDataBlocks =
                gDigiLiveRxCadenceObservedTotalDataBlocks;
            ResetDigiLiveRxCadenceCapture();
        }
    }
}

uint32_t
DigiLiveCycleFromOHCITimestamp(uint32_t timestamp)
{
    uint32_t value = timestamp & kOhciContextTimestampMask;
    return (((value >> 13) & 0x07u) * kOhciCycleCountPerSecond) +
           (value & 0x1fffu);
}

uint32_t
DigiLiveCycleDistance(uint32_t expected, uint32_t observed)
{
    if (expected == 0xffffffff || observed == 0xffffffff) {
        return 0;
    }
    return observed >= expected
        ? observed - expected
        : (kOhciCycleCountModulus - expected) + observed;
}

uint32_t
DigiLiveNextCycle(uint32_t cycle)
{
    if (cycle == 0xffffffff) {
        return 0xffffffff;
    }
    cycle++;
    return cycle >= kOhciCycleCountModulus ? 0 : cycle;
}

void
UpdateDigiLiveReceiveTimingDiagnostics(volatile uint32_t * packetHeader,
                                       uint32_t payloadBytes,
                                       uint32_t dataBlocks)
{
    if (packetHeader == nullptr) {
        return;
    }

    uint32_t header0 = packetHeader[0];
    uint32_t header1 = packetHeader[1];
    uint32_t header2 = packetHeader[2];
    uint32_t header3 = packetHeader[3];
    uint32_t timestamp = header0 & kOhciContextTimestampMask;
    uint32_t cycle = DigiLiveCycleFromOHCITimestamp(timestamp);
    uint32_t cipHeader0 = ToBigEndian32(header2);
    uint32_t cipHeader1 = ToBigEndian32(header3);
    DigiLiveCIPHeader cip = ParseDigiLiveCIPHeader(cipHeader0, cipHeader1);
    uint32_t eventBytes = cip.dbs * sizeof(uint32_t);
    uint32_t eventCount = eventBytes != 0 ? payloadBytes / eventBytes : 0;
    uint32_t payloadRemainder = eventBytes != 0 ? payloadBytes % eventBytes : payloadBytes;

    gDigiLiveRxHeader0Raw = header0;
    gDigiLiveRxHeader1Raw = header1;
    gDigiLiveRxHeader2Raw = header2;
    gDigiLiveRxHeader3Raw = header3;
    gDigiLiveRxIsoHeader = ToBigEndian32(header1);
    gDigiLiveRxTimestamp = timestamp;
    gDigiLiveRxCycle = cycle;
    gDigiLiveRxCIPHeader0 = cipHeader0;
    gDigiLiveRxCIPHeader1 = cipHeader1;
    gDigiLiveRxDBC = cip.dbc;
    gDigiLiveRxSYT = cip.syt;
    gDigiLiveRxSID = cip.sid;
    gDigiLiveRxDBS = cip.dbs;
    gDigiLiveRxSPH = cip.sph;
    gDigiLiveRxFMT = cip.fmt;
    gDigiLiveRxFDF = cip.fdf;
    gDigiLiveRxEventCount = eventCount;
    gDigiLiveRxPayloadRemainderBytes = payloadRemainder;
    gDigiLiveRxStreamProcessorPacketCount++;

    if (payloadRemainder == 0 &&
        cip.dbs == kDigi00xDuplexDataBlockQuadlets &&
        cip.fmt == kDigi00xCIPFMTAM824 &&
        cip.fdf == gDigi00xCurrentCIPSFC &&
        cip.sph == 0) {
        gDigiLiveRxStreamProcessorValidPacketCount++;
    }
    if (payloadRemainder != 0) {
        gDigiLiveRxPayloadRemainderCount++;
    }
    if (gDigiLiveRxExpectedSID == 0xffffffff) {
        gDigiLiveRxExpectedSID = cip.sid;
    } else if (cip.sid != gDigiLiveRxExpectedSID) {
        gDigiLiveRxUnexpectedSIDCount++;
    }
    if (cip.dbs != kDigi00xDuplexDataBlockQuadlets) {
        gDigiLiveRxUnexpectedDBSCount++;
    }
    if (cip.sph != 0) {
        gDigiLiveRxUnexpectedSPHCount++;
    }
    if (cip.fmt != kDigi00xCIPFMTAM824) {
        gDigiLiveRxUnexpectedFMTCount++;
    }
    if (cip.fdf != gDigi00xCurrentCIPSFC) {
        gDigiLiveRxUnexpectedFDFCount++;
    }
    if (eventCount < gDigiLiveRxMinEventCount) {
        gDigiLiveRxMinEventCount = eventCount;
    }
    if (eventCount > gDigiLiveRxMaxEventCount) {
        gDigiLiveRxMaxEventCount = eventCount;
    }

    if (gDigiLiveRxExpectedCycle != 0xffffffff) {
        gDigiLiveRxCycleDelta = DigiLiveCycleDistance(gDigiLiveRxExpectedCycle, cycle);
        if (gDigiLiveRxCycleDelta != 0) {
            gDigiLiveRxCycleLostCount++;
            if (gDigiLiveRxCycleDelta > gDigiLiveRxMaxCycleDelta) {
                gDigiLiveRxMaxCycleDelta = gDigiLiveRxCycleDelta;
            }
        }
    } else {
        gDigiLiveRxCycleDelta = 0;
    }
    gDigiLiveRxCyclePacketCount++;
    gDigiLiveRxExpectedCycle = DigiLiveNextCycle(cycle);

    if (gDigiLiveRxExpectedDBC == 0xffffffff) {
        gDigiLiveRxDBCInitCount++;
        gDigiLiveRxDBCDelta = 0;
    } else {
        gDigiLiveRxDBCDelta = DigiLiveDBCForwardDistance(gDigiLiveRxExpectedDBC, cip.dbc);
        if (cip.dbc != gDigiLiveRxExpectedDBC) {
            gDigiLiveRxDBCLostCount++;
            if (gDigiLiveRxDBCDelta > gDigiLiveRxMaxDBCDelta) {
                gDigiLiveRxMaxDBCDelta = gDigiLiveRxDBCDelta;
            }
        }
    }
    gDigiLiveRxDBCPacketCount++;
    gDigiLiveRxExpectedDBC = (cip.dbc + eventCount) & 0xffu;

    if (cip.syt == 0xffffu) {
        gDigiLiveRxSYTNoInfoCount++;
    } else if (cip.syt == 0) {
        gDigiLiveRxSYTZeroCount++;
    }

    if (eventCount < (sizeof(gDigiLiveRxDataBlockHistogram) /
                      sizeof(gDigiLiveRxDataBlockHistogram[0]))) {
        gDigiLiveRxDataBlockHistogram[eventCount]++;
    } else {
        gDigiLiveRxUnexpectedDataBlockCount++;
    }
    if (eventCount != dataBlocks || (eventCount != 5 && eventCount != 6)) {
        gDigiLiveRxUnexpectedDataBlockCount++;
    }
}

int32_t
Signed24ToInt32(uint32_t value24)
{
    value24 &= 0x00ffffffu;
    if ((value24 & 0x00800000u) != 0) {
        value24 |= 0xff000000u;
    }
    return static_cast<int32_t>(value24);
}

uint32_t
AbsoluteInt32(int32_t value)
{
    if (value < 0) {
        return static_cast<uint32_t>(-value);
    }
    return static_cast<uint32_t>(value);
}

uint32_t
ReturnCodeToProperty(kern_return_t ret)
{
    return static_cast<uint32_t>(ret);
}

void
ResetOHCIInterruptDiagnostics()
{
    gOHCIInterruptConfigureRet = ReturnCodeToProperty(kIOReturnNotReady);
    gOHCIInterruptActionCreateRet = ReturnCodeToProperty(kIOReturnNotReady);
    gOHCIInterruptSourceCreateRet = ReturnCodeToProperty(kIOReturnNotReady);
    gOHCIInterruptSetHandlerRet = ReturnCodeToProperty(kIOReturnNotReady);
    gOHCIInterruptReady = 0;
    gOHCIInterruptEnabled = 0;
    gOHCIInterruptUseMSIX = 0;
    gOHCIInterruptCount = 0;
    gOHCIInterruptIsoRecvCount = 0;
    gOHCIInterruptIsoXmitCount = 0;
    gOHCIInterruptOtherCount = 0;
    gOHCIInterruptDigiHarvestAttemptCount = 0;
    gOHCIInterruptDigiHarvestSuccessCount = 0;
    gOHCIInterruptDigiHarvestBusyCount = 0;
    gOHCIInterruptDigiHarvestEmptyCount = 0;
    gOHCIInterruptLastHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
    gOHCIInterruptLastIntEvent = 0;
    gOHCIInterruptLastIsoRecvEvent = 0;
    gOHCIInterruptLastIsoXmitEvent = 0;
    gOHCIInterruptLastClearedIntEvent = 0;
    gOHCIInterruptLastClearedIsoRecvEvent = 0;
    gOHCIInterruptLastClearedIsoXmitEvent = 0;
    gOHCIInterruptLastCount = 0;
    gOHCIInterruptLastTime = 0;
}

kern_return_t
ConfigureOHCIInterruptDispatch(FireWireOHCIProbe * driver, IOPCIDevice * pciDevice)
{
    if (driver == nullptr || pciDevice == nullptr || gAudioRefreshQueue == nullptr) {
        return kIOReturnNotReady;
    }
    if (gOHCIInterruptSource != nullptr && gOHCIInterruptAction != nullptr) {
        gOHCIInterruptReady = 1;
        return kIOReturnSuccess;
    }

    kern_return_t configureRet =
        pciDevice->ConfigureInterrupts(kIOInterruptTypePCIMessagedX, 1, 1, 0);
    gOHCIInterruptUseMSIX = configureRet == kIOReturnSuccess ? 1 : 0;
    if (configureRet != kIOReturnSuccess) {
        configureRet = pciDevice->ConfigureInterrupts(kIOInterruptTypePCIMessaged, 1, 1, 0);
        gOHCIInterruptUseMSIX = 0;
    }
    gOHCIInterruptConfigureRet = ReturnCodeToProperty(configureRet);
    if (configureRet != kIOReturnSuccess) {
        return configureRet;
    }

    OSAction * action = nullptr;
    kern_return_t actionRet = driver->CreateActionInterruptOccurred(0, &action);
    gOHCIInterruptActionCreateRet = ReturnCodeToProperty(actionRet);
    if (actionRet != kIOReturnSuccess || action == nullptr) {
        return actionRet == kIOReturnSuccess ? kIOReturnNoResources : actionRet;
    }
    gOHCIInterruptAction = action;

    IOInterruptDispatchSource * source = nullptr;
    kern_return_t sourceRet =
        IOInterruptDispatchSource::Create(pciDevice, 0, gAudioRefreshQueue, &source);
    gOHCIInterruptSourceCreateRet = ReturnCodeToProperty(sourceRet);
    if (sourceRet != kIOReturnSuccess || source == nullptr) {
        OSSafeReleaseNULL(gOHCIInterruptAction);
        return sourceRet == kIOReturnSuccess ? kIOReturnNoResources : sourceRet;
    }
    gOHCIInterruptSource = source;

    kern_return_t handlerRet = gOHCIInterruptSource->SetHandler(gOHCIInterruptAction);
    gOHCIInterruptSetHandlerRet = ReturnCodeToProperty(handlerRet);
    if (handlerRet != kIOReturnSuccess) {
        OSSafeReleaseNULL(gOHCIInterruptSource);
        OSSafeReleaseNULL(gOHCIInterruptAction);
        return handlerRet;
    }

    gOHCIInterruptReady = 1;
    return kIOReturnSuccess;
}

void
EnableOHCIInterruptDispatch()
{
    if (gOHCIInterruptSource == nullptr || gOHCIInterruptReady == 0) {
        gOHCIInterruptEnabled = 0;
        return;
    }
    gOHCIInterruptSource->SetEnableWithCompletion(true, nullptr);
    gOHCIInterruptEnabled = 1;
}

void
DisableOHCIInterruptDispatch()
{
    if (gOHCIInterruptSource != nullptr) {
        gOHCIInterruptSource->SetEnableWithCompletion(false, nullptr);
    }
    gOHCIInterruptEnabled = 0;
}

void
ReleaseOHCIInterruptDispatch()
{
    DisableOHCIInterruptDispatch();
    OSSafeReleaseNULL(gOHCIInterruptSource);
    OSSafeReleaseNULL(gOHCIInterruptAction);
    gOHCIInterruptReady = 0;
}

void
PublishAudioRuntimeDiagnostics()
{
    if (gDriverInstance == nullptr) {
        return;
    }

    OSDictionary * properties = OSDictionary::withCapacity(640);
    if (properties == nullptr) {
        return;
    }

    AddNumberProperty(properties, "ProbeControlMessageCount", gDigiLiveMidiMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlConsoleMessageCount", gDigiLiveMidiConsoleMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlPhysicalMessageCount", gDigiLiveMidiPhysicalMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlInvalidLengthCount", gDigiLiveMidiInvalidLengthCount, 64);
    AddNumberProperty(properties, "ProbeControlLoggedMessageCount", gDigiLiveMidiLoggedMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlRawWordBE", gDigiLiveMidiLastRawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlMarker", gDigiLiveMidiLastMarker, 32);
    AddNumberProperty(properties, "ProbeControlData0", gDigiLiveMidiLastData0, 32);
    AddNumberProperty(properties, "ProbeControlData1", gDigiLiveMidiLastData1, 32);
    AddNumberProperty(properties, "ProbeControlControl", gDigiLiveMidiLastControl, 32);
    AddNumberProperty(properties, "ProbeControlPortNibble", gDigiLiveMidiLastPortNibble, 32);
    AddNumberProperty(properties, "ProbeControlLength", gDigiLiveMidiLastLength, 32);
    AddNumberProperty(properties, "ProbeControlRecentIndex", gDigiLiveMidiRecentIndex, 32);
    AddNumberProperty(properties, "ProbeControlRecentCount", gDigiLiveMidiRecentCount, 32);
    AddNumberProperty(properties, "ProbeControlDecodedMessageCount", gDigiLiveMidiDecodedMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlDecodedNoteMessageCount", gDigiLiveMidiNoteMessageCount, 64);
    AddNumberProperty(properties,
                      "ProbeControlDecodedControlChangeMessageCount",
                      gDigiLiveMidiControlChangeMessageCount,
                      64);
    AddNumberProperty(properties, "ProbeControlDecodedLastMessage", gDigiLiveMidiLastDecodedMessage, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastPort", gDigiLiveMidiLastDecodedPort, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastStatus", gDigiLiveMidiLastDecodedStatus, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastData1", gDigiLiveMidiLastDecodedData1, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastData2", gDigiLiveMidiLastDecodedData2, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastNoteNumber", gDigiLiveMidiLastNoteNumber, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastNoteVelocity", gDigiLiveMidiLastNoteVelocity, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastControlNumber", gDigiLiveMidiLastControlNumber, 32);
    AddNumberProperty(properties, "ProbeControlDecodedLastControlValue", gDigiLiveMidiLastControlValue, 32);
    AddNumberProperty(properties, "ProbeControlDecodedRecentIndex", gDigiLiveMidiDecodedRecentIndex, 32);
    AddNumberProperty(properties, "ProbeControlDecodedRecentCount", gDigiLiveMidiDecodedRecentCount, 32);
    AddNumberProperty(properties, "ProbeControlStateMappedMessageCount", gDigiLiveControlMappedMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlStateUnknownMessageCount", gDigiLiveControlUnknownMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlStateLastMappedKind", gDigiLiveControlLastMappedKind, 32);
    AddNumberProperty(properties, "ProbeControlStateLastMappedChannel", gDigiLiveControlLastMappedChannel, 32);
    AddNumberProperty(properties, "ProbeControlStateSelect1Pressed", gDigiLiveControlSelect1Pressed, 32);
    AddNumberProperty(properties, "ProbeControlStateFader1Touched", gDigiLiveControlFader1Touched, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateFader1ControlNumber",
                      gDigiLiveControlFader1ControlNumber,
                      32);
    AddNumberProperty(properties, "ProbeControlStateFader1Value", gDigiLiveControlFader1Value, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateFader1UpdateCount",
                      gDigiLiveControlFader1UpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportRTZPressed",
                      gDigiLiveControlTransportRTZPressed,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportRewindPressed",
                      gDigiLiveControlTransportRewindPressed,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportFastForwardPressed",
                      gDigiLiveControlTransportFastForwardPressed,
                      32);
    AddNumberProperty(properties, "ProbeControlStateStopPressed", gDigiLiveControlStopPressed, 32);
    AddNumberProperty(properties, "ProbeControlStatePlayPressed", gDigiLiveControlPlayPressed, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportRecordPressed",
                      gDigiLiveControlTransportRecordPressed,
                      32);
    AddNumberProperty(properties, "ProbeControlStateArrowLeftPressed", gDigiLiveControlArrowLeftPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateArrowRightPressed", gDigiLiveControlArrowRightPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateArrowUpPressed", gDigiLiveControlArrowUpPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateArrowDownPressed", gDigiLiveControlArrowDownPressed, 32);
    AddNumberProperty(properties, "ProbeControlStateJogWheelValue", gDigiLiveControlJogWheelValue, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateJogWheelDirection",
                      gDigiLiveControlJogWheelDirection,
                      32);
    AddNumberProperty(properties, "ProbeControlStateJogWheelStep", gDigiLiveControlJogWheelStep, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateJogWheelUpdateCount",
                      gDigiLiveControlJogWheelUpdateCount,
                      64);
    AddNumberProperty(properties, "ProbeControlStateShuttleValue", gDigiLiveControlShuttleValue, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateShuttleUpdateCount",
                      gDigiLiveControlShuttleUpdateCount,
                      64);
    AddNumberProperty(properties, "ProbeControlStateModeViewLastNote", gDigiLiveControlModeViewLastNote, 32);
    AddNumberProperty(properties, "ProbeControlStateModeViewLastIndex", gDigiLiveControlModeViewLastIndex, 32);
    AddNumberProperty(properties,
                      "ProbeControlStateModeViewUpdateCount",
                      gDigiLiveControlModeViewUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateAboveTransportLastNote",
                      gDigiLiveControlAboveTransportLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateAboveTransportLastIndex",
                      gDigiLiveControlAboveTransportLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateAboveTransportUpdateCount",
                      gDigiLiveControlAboveTransportUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionLastGroup",
                      gDigiLiveControlTransportSectionLastGroup,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionLastNote",
                      gDigiLiveControlTransportSectionLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionLastIndex",
                      gDigiLiveControlTransportSectionLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateTransportSectionUpdateCount",
                      gDigiLiveControlTransportSectionUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateNavigationModeLedNote",
                      gDigiLiveControlNavigationModeLedNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateHardwareMonitorLastNote",
                      gDigiLiveControlHardwareMonitorLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateHardwareMonitorLastIndex",
                      gDigiLiveControlHardwareMonitorLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateHardwareMonitorUpdateCount",
                      gDigiLiveControlHardwareMonitorUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateDisplayModePressed",
                      gDigiLiveControlDisplayModePressed,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateDisplayModeUpdateCount",
                      gDigiLiveControlDisplayModeUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateEncoderAssignLastNote",
                      gDigiLiveControlEncoderAssignLastNote,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateEncoderAssignLastIndex",
                      gDigiLiveControlEncoderAssignLastIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlStateEncoderAssignUpdateCount",
                      gDigiLiveControlEncoderAssignUpdateCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlStateRotaryEncoderLastIndex",
                      gDigiLiveControlRotaryEncoderLastIndex,
                      32);
    for (uint32_t i = 0; i < kDigiLiveControlModeViewButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateModeViewButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlModeViewButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlAboveTransportButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateAboveTransportButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlAboveTransportButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlTransportSectionButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateTransportSectionButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlTransportSectionButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlHardwareMonitorButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateHardwareMonitorButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlHardwareMonitorButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlEncoderAssignButtonCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateEncoderAssignButton",
                                 i + 1,
                                 "Pressed",
                                 gDigiLiveControlEncoderAssignButtonPressed[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveControlRotaryEncoderCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "Value",
                                 gDigiLiveControlRotaryEncoderValue[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "Direction",
                                 gDigiLiveControlRotaryEncoderDirection[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "Step",
                                 gDigiLiveControlRotaryEncoderStep[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateRotaryEncoder",
                                 i + 1,
                                 "UpdateCount",
                                 gDigiLiveControlRotaryEncoderUpdateCount[i],
                                 64);
    }
    AddNumberProperty(properties, "ProbeControlMotorTestEnabled", kDigiLiveControlMotorTestEnabled, 32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestTriggerCount",
                      gDigiLiveControlMotorTestTriggerCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestMessageCount",
                      gDigiLiveControlMotorTestMessageCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestSkippedCount",
                      gDigiLiveControlMotorTestSkippedCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastChannel",
                      gDigiLiveControlMotorTestLastChannel,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastTarget10",
                      gDigiLiveControlMotorTestLastTarget10,
                      32);
    AddNumberProperty(properties, "ProbeControlMotorTestLastCC", gDigiLiveControlMotorTestLastCC, 32);
    AddNumberProperty(properties,
                      "ProbeControlMotorTestLastValue",
                      gDigiLiveControlMotorTestLastValue,
                      32);
    AddDigiLiveControlDebugProperties(properties);
    for (uint32_t i = 0; i < kDigiLiveControlChannelStripCount; ++i) {
        uint32_t channel = i + 1;
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "SelectPressed",
                                 gDigiLiveControlChannelSelectPressed[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "SoloPressed",
                                 gDigiLiveControlChannelSoloPressed[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "MutePressed",
                                 gDigiLiveControlChannelMutePressed[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderTouched",
                                 gDigiLiveControlChannelFaderTouched[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderControlNumber",
                                 gDigiLiveControlChannelFaderControlNumber[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderValue",
                                 gDigiLiveControlChannelFaderValue[i],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeControlStateChannel",
                                 channel,
                                 "FaderUpdateCount",
                                 gDigiLiveControlChannelFaderUpdateCount[i],
                                 64);
    }
    AddNumberProperty(properties,
                      "ProbeControlRawFragmentEchoEnabled",
                      kDigiLiveMidiRawFragmentEchoEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlDecodedFeedbackEnabled",
                      kDigiLiveMidiDecodedFeedbackEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlEchoFaderMoveFeedbackEnabled",
                      kDigiLiveMidiEchoFaderMoveFeedbackEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackSentCount",
                      gDigiLiveControlFaderFeedbackSentCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackSkippedCount",
                      gDigiLiveControlFaderFeedbackSkippedCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackFlushCount",
                      gDigiLiveControlFaderFeedbackFlushCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackLastChannel",
                      gDigiLiveControlFaderFeedbackLastChannel,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackLastCC",
                      gDigiLiveControlFaderFeedbackLastCC,
                      32);
    AddNumberProperty(properties,
                      "ProbeControlFaderFeedbackLastValue",
                      gDigiLiveControlFaderFeedbackLastValue,
                      32);
    AddNumberProperty(properties, "ProbeControlEchoEnabled", kDigiLiveMidiEchoToOutputEnabled, 32);
    AddNumberProperty(properties,
                      "ProbeControlEchoConsolePortOnlyEnabled",
                      kDigiLiveMidiEchoConsolePortOnlyEnabled,
                      32);
    AddNumberProperty(properties, "ProbeControlEchoQueueCount", gDigiLiveMidiEchoQueueCount, 32);
    AddNumberProperty(properties, "ProbeControlEchoLastQueuedRawWordBE", gDigiLiveMidiEchoLastQueuedRawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlEchoLastTransmitRawWordBE", gDigiLiveMidiEchoLastTransmitRawWordBE, 32);
    AddNumberProperty(properties, "ProbeControlFeedbackMessageCount", gDigiLiveMidiFeedbackMessageCount, 64);
    AddNumberProperty(properties, "ProbeControlFeedbackSkippedCount", gDigiLiveMidiFeedbackSkippedCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoAppendCount", gDigiLiveMidiEchoAppendCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoDropCount", gDigiLiveMidiEchoDropCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoTransmitCount", gDigiLiveMidiEchoTransmitCount, 64);
    AddNumberProperty(properties, "ProbeControlEchoBusyCount", gDigiLiveMidiEchoBusyCount, 64);
    AddDataProperty(properties,
                    "ProbeControlRecentRawWordsBE",
                    gDigiLiveMidiRecentRawWordBE,
                    sizeof(gDigiLiveMidiRecentRawWordBE));
    AddDataProperty(properties,
                    "ProbeControlDecodedRecentMessages",
                    gDigiLiveMidiDecodedRecentMessages,
                    sizeof(gDigiLiveMidiDecodedRecentMessages));
    for (uint32_t i = 0; i < kDigiLiveMidiRecentIndexedPublishCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlRecentRawWord",
                                 i,
                                 "BE",
                                 gDigiLiveMidiRecentRawWordBE[i],
                                 32);
    }
    for (uint32_t i = 0; i < kDigiLiveMidiDecodedRecentIndexedPublishCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeControlDecodedRecentMessage",
                                 i,
                                 "",
                                 gDigiLiveMidiDecodedRecentMessages[i],
                                 32);
    }

    AddNumberProperty(properties, "ProbeAudioRuntimeStartDeviceCount", gAudioStartDeviceCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStopDeviceCount", gAudioStopDeviceCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStartDeviceRet", gAudioStartDeviceRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStopDeviceRet", gAudioStopDeviceRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStartDeviceObjectID", gAudioStartDeviceObjectID, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStopDeviceObjectID", gAudioStopDeviceObjectID, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStartDeviceStage", gAudioStartDeviceStage, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeStopDeviceStage", gAudioStopDeviceStage, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeDeviceStarted", gAudioRuntimeDeviceStarted, 32);
    AddNumberProperty(properties, "ProbePowerStateLastFlags", gPowerStateLastFlags, 32);
    AddNumberProperty(properties, "ProbePowerStateLastRet", gPowerStateLastRet, 32);
    AddNumberProperty(properties, "ProbePowerStateChangeCount", gPowerStateChangeCount, 64);
    AddNumberProperty(properties, "ProbePowerStateOnCount", gPowerStateOnCount, 64);
    AddNumberProperty(properties, "ProbePowerStateOffCount", gPowerStateOffCount, 64);
    AddNumberProperty(properties, "ProbePowerStateLowCount", gPowerStateLowCount, 64);
    AddNumberProperty(properties, "ProbePowerStateLiveStopCount", gPowerStateLiveStopCount, 64);
    AddNumberProperty(properties,
                      "ProbePowerStateWakeRestartRequestCount",
                      gPowerStateWakeRestartRequestCount,
                      64);
    AddNumberProperty(properties, "ProbeAudioRuntimeDeviceStartIOCount", gAudioDeviceStartIOCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeDeviceStopIOCount", gAudioDeviceStopIOCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeDeviceStartIORet", gAudioDeviceStartIORet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeDeviceStopIORet", gAudioDeviceStopIORet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeCallbackRestartEnabled", kAudioRuntimeCallbackRestartEnabled, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartInProgress", gAudioRuntimeRestartInProgress, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartLastReason", gAudioRuntimeRestartLastReason, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartLastRet", gAudioRuntimeRestartLastRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartLastLiveRet", gAudioRuntimeRestartLastLiveRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartLastDigiRunning", gAudioRuntimeRestartLastDigiRunning, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartLastDigiReady", gAudioRuntimeRestartLastDigiReady, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartLastWorkerRunning", gAudioRuntimeRestartLastWorkerRunning, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartRequestCount", gAudioRuntimeRestartRequestCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartDispatchCount", gAudioRuntimeRestartDispatchCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartSuccessCount", gAudioRuntimeRestartSuccessCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartSkippedCount", gAudioRuntimeRestartSkippedCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRestartBusyCount", gAudioRuntimeRestartBusyCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCaptureAttemptCount", gAudioRefreshCaptureAttemptCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCaptureSuccessCount", gAudioRefreshCaptureSuccessCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCaptureInProgress", gAudioRefreshCaptureInProgress, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCaptureRet", gAudioRefreshCaptureRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCaptureFrameCount", gAudioRefreshCaptureFrameCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCapturePeakAbs", gAudioRefreshCapturePeakAbs, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshCaptureRxBytes", gAudioRefreshCaptureRxBytes, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshQueueCreateRet", gAudioRefreshQueueCreateRet, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptDispatchEnabled", kOHCIInterruptDispatchEnabled, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptConfigureRet", gOHCIInterruptConfigureRet, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptActionCreateRet", gOHCIInterruptActionCreateRet, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptSourceCreateRet", gOHCIInterruptSourceCreateRet, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptSetHandlerRet", gOHCIInterruptSetHandlerRet, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptReady", gOHCIInterruptReady, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptEnabled", gOHCIInterruptEnabled, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptUseMSIX", gOHCIInterruptUseMSIX, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptCount", gOHCIInterruptCount, 64);
    AddNumberProperty(properties, "ProbeOHCIInterruptIsoRecvCount", gOHCIInterruptIsoRecvCount, 64);
    AddNumberProperty(properties, "ProbeOHCIInterruptIsoXmitCount", gOHCIInterruptIsoXmitCount, 64);
    AddNumberProperty(properties, "ProbeOHCIInterruptOtherCount", gOHCIInterruptOtherCount, 64);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptDigiHarvestAttemptCount",
                      gOHCIInterruptDigiHarvestAttemptCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptDigiHarvestSuccessCount",
                      gOHCIInterruptDigiHarvestSuccessCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptDigiHarvestBusyCount",
                      gOHCIInterruptDigiHarvestBusyCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptDigiHarvestEmptyCount",
                      gOHCIInterruptDigiHarvestEmptyCount,
                      64);
    AddNumberProperty(properties, "ProbeOHCIInterruptLastHarvestRet", gOHCIInterruptLastHarvestRet, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptLastIntEvent", gOHCIInterruptLastIntEvent, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptLastIsoRecvEvent", gOHCIInterruptLastIsoRecvEvent, 32);
    AddNumberProperty(properties, "ProbeOHCIInterruptLastIsoXmitEvent", gOHCIInterruptLastIsoXmitEvent, 32);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptLastClearedIntEvent",
                      gOHCIInterruptLastClearedIntEvent,
                      32);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptLastClearedIsoRecvEvent",
                      gOHCIInterruptLastClearedIsoRecvEvent,
                      32);
    AddNumberProperty(properties,
                      "ProbeOHCIInterruptLastClearedIsoXmitEvent",
                      gOHCIInterruptLastClearedIsoXmitEvent,
                      32);
    AddNumberProperty(properties, "ProbeOHCIInterruptLastCount", gOHCIInterruptLastCount, 64);
    AddNumberProperty(properties, "ProbeOHCIInterruptLastTime", gOHCIInterruptLastTime, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerDispatchCount", gAudioRefreshWorkerDispatchCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerIterationCount", gAudioRefreshWorkerIterationCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerExitCount", gAudioRefreshWorkerExitCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerRunning", gAudioRefreshWorkerRunning, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerStopWaitLoops", gAudioRefreshWorkerStopWaitLoops, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerLastRet", gAudioRefreshWorkerLastRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerLastGeneration", gAudioRefreshWorkerLastGeneration, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerLivePublishCount", gAudioRefreshWorkerLivePublishCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerLivePublishSkipCount", gAudioRefreshWorkerLivePublishSkipCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerBacklogNoSleepCount", gAudioRefreshWorkerBacklogNoSleepCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRefreshWorkerLowWaterNoSleepCount", gAudioRefreshWorkerLowWaterNoSleepCount, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeSampleRate", gDigi00xCurrentSampleRate, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeDigiLocalRateIndex", gDigi00xCurrentLocalRateIndex, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeCIPSFC", gDigi00xCurrentCIPSFC, 32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeSampleRateChangeCount",
                      gAudioRuntimeSampleRateChangeCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeRequestedSampleRate",
                      gAudioRuntimeRequestedSampleRate,
                      32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeSampleRateChangeStage",
                      gAudioRuntimeSampleRateChangeStage,
                      32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeSampleRateChangeRestarted",
                      gAudioRuntimeSampleRateChangeRestarted,
                      32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeSampleRateChangeRet",
                      gAudioRuntimeSampleRateChangeRet,
                      32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputCallbackHarvestEnabled", kAudioCallbackHarvestEnabled, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputCallbackHarvestAttemptCount", gAudioInputCallbackHarvestAttemptCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputCallbackHarvestSuccessCount", gAudioInputCallbackHarvestSuccessCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputCallbackHarvestRet", gAudioInputCallbackHarvestRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputCallbackHarvestLastFillFrames", gAudioInputCallbackHarvestLastFillFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeCaptureGeneration", gAudioCaptureGeneration, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeDirectInputBufferEnabled", kAudioDirectInputBufferEnabled, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingCapacityFrames", kAudioRingBufferFrameCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingWriteFrame", gAudioRingWriteFrame, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingReadFrame", gAudioRingReadFrame, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingProducedFrames", gAudioRingProducedFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingConsumedFrames", gAudioRingConsumedFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingRepeatedFrames", gAudioRingRepeatedFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingUnderrunFrames", gAudioRingUnderrunFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingOverrunFrames", gAudioRingOverrunFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingCurrentFillFrames", gAudioRingCurrentFillFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingMaxFillFrames", gAudioRingMaxFillFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingAppendCount", gAudioRingAppendCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingLastAppendFrames", gAudioRingLastAppendFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingLastAppendDroppedFrames", gAudioRingLastAppendDroppedFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingLastAppendGeneration", gAudioRingLastAppendGeneration, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingLastConsumeFrames", gAudioRingLastConsumeFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingLastConsumeUnderrunFrames", gAudioRingLastConsumeUnderrunFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeRingLastConsumeRepeatedFrames", gAudioRingLastConsumeRepeatedFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputStreamEnabled", kAudioOutputStreamEnabled, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputStreamCreateRet", gAudioOutputStreamCreateRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputAddStreamRet", gAudioOutputAddStreamRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputStreamActiveRet", gAudioOutputStreamActiveRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputBufferCreateRet", gAudioOutputBufferCreateRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputBufferSetLengthRet", gAudioOutputBufferSetLengthRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputBufferRangeRet", gAudioOutputBufferRangeRet, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputBufferFrameCount", kAudioOutputBufferFrameCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputBufferBytes", kAudioOutputBufferBytes, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputCallbackCount", gAudioOutputCallbackCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputLastBufferFrameSize", gAudioOutputLastBufferFrameSize, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputLastSampleTime", gAudioOutputLastSampleTime, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingCapacityFrames", kAudioOutputRingBufferFrameCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputBufferOffsetMode", kAudioOutputBufferOffsetMode, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingWriteFrame", gAudioOutputRingWriteFrame, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingReadFrame", gAudioOutputRingReadFrame, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingProducedFrames", gAudioOutputRingProducedFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingConsumedFrames", gAudioOutputRingConsumedFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingUnderrunFrames", gAudioOutputRingUnderrunFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingOverrunFrames", gAudioOutputRingOverrunFrames, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingCurrentFillFrames", gAudioOutputRingCurrentFillFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingMaxFillFrames", gAudioOutputRingMaxFillFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingPrebufferFrames", kAudioOutputRingPrebufferFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingKeepFrames", kAudioOutputRingKeepFrames, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingPrebuffered", gAudioOutputRingPrebuffered, 32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeOutputRingPrebufferReadyCount",
                      gAudioOutputRingPrebufferReadyCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeOutputRingPrebufferHoldCount",
                      gAudioOutputRingPrebufferHoldCount,
                      64);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingAppendCount", gAudioOutputRingAppendCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingLastAppendFrames", gAudioOutputRingLastAppendFrames, 32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeOutputRingLastAppendDroppedFrames",
                      gAudioOutputRingLastAppendDroppedFrames,
                      32);
    AddNumberProperty(properties, "ProbeAudioRuntimeOutputRingLastConsumeFrames", gAudioOutputRingLastConsumeFrames, 32);
    AddNumberProperty(properties,
                      "ProbeAudioRuntimeOutputRingLastConsumeUnderrunFrames",
                      gAudioOutputRingLastConsumeUnderrunFrames,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPayloadUpdateEnabled", kDigiLiveOutputPayloadUpdateEnabled, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLeadPackets", kDigiLiveOutputLeadPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputServiceAheadPackets", kDigiLiveOutputServiceAheadPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputSilenceAheadPackets", kDigiLiveOutputSilenceAheadPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputStopSilencePushCount", kDigiLiveOutputStopSilencePushCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputMaxPacketsPerPush", kDigiLiveOutputMaxPacketsPerPush, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputITHardwareCursorValid", gDigiLiveITHardwareCursorValid, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputITHardwarePacketCursor", gDigiLiveITHardwarePacketCursor, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPacketCursorValid", gDigiLiveOutputPacketCursorValid, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPacketCursor", gDigiLiveOutputPacketCursor, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPushInProgress", gDigiLiveOutputPushInProgress, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPushBusyCount", gDigiLiveOutputPushBusyCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPushAttemptCount", gDigiLiveOutputPushAttemptCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPushSuccessCount", gDigiLiveOutputPushSuccessCount, 64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveOutputWorkerPushAudioCount",
                      gDigiLiveOutputWorkerPushAudioCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveOutputWorkerPushSkippedAudioCount",
                      gDigiLiveOutputWorkerPushSkippedAudioCount,
                      64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputPacketWriteCount", gDigiLiveOutputPacketWriteCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputFrameWriteCount", gDigiLiveOutputFrameWriteCount, 64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveOutputSilentFrameWriteCount",
                      gDigiLiveOutputSilentFrameWriteCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveOutputPlannedSilentFrameWriteCount",
                      gDigiLiveOutputPlannedSilentFrameWriteCount,
                      64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputAudioStartCount", gDigiLiveOutputAudioStartCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputCursorCatchUpCount", gDigiLiveOutputCursorCatchUpCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastCurrentPacketIndex", gDigiLiveOutputLastCurrentPacketIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastStartPacketIndex", gDigiLiveOutputLastStartPacketIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastPacketCount", gDigiLiveOutputLastPacketCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastFrameCount", gDigiLiveOutputLastFrameCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastSilentFrameCount", gDigiLiveOutputLastSilentFrameCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastRingFillFrames", gDigiLiveOutputLastRingFillFrames, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveOutputLastStartDistancePackets",
                      gDigiLiveOutputLastStartDistancePackets,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputLastSyncRet", gDigiLiveOutputLastSyncRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotResetCount", gDigiLiveOutputDotResetCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotCarry", gDigiLiveOutputDotState.carry, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotIndex", gDigiLiveOutputDotState.idx, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotOffset", gDigiLiveOutputDotState.off, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotLastInputWordBE", gDigiLiveOutputDotLastInputWordBE, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotLastOutputWordBE", gDigiLiveOutputDotLastOutputWordBE, 32);
    AddNumberProperty(properties, "ProbeDigiLiveOutputDotLastCarry", gDigiLiveOutputDotLastCarry, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStartAttemptCount", gDigiLiveStartAttemptCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStartSuccessCount", gDigiLiveStartSuccessCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStopAttemptCount", gDigiLiveStopAttemptCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStopSuccessCount", gDigiLiveStopSuccessCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStopDrainWaitLoops", gDigiLiveStopDrainWaitLoops, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRunning", gDigiLiveRunning, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStarting", gDigiLiveStarting, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStopping", gDigiLiveStopping, 32);
    AddNumberProperty(properties, "ProbeDigiLiveReady", gDigiLiveReady, 32);
    AddNumberProperty(properties, "ProbeDigiLiveState", gDigiLiveState, 32);
    AddNumberProperty(properties, "ProbeDigiLiveDrainBusy", gDigiLiveDrainBusy, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStartRet", gDigiLiveStartRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStopRet", gDigiLiveStopRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveStartStage", gDigiLiveStartStage, 32);
    AddNumberProperty(properties, "ProbeDigiLiveBeginTransactionStage", gDigiLiveBeginTransactionStage, 32);
    AddNumberProperty(properties, "ProbeDigiLiveBeginTransactionRet", gDigiLiveBeginTransactionRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIsoStartStage", gDigiLiveIsoStartStage, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIsoStartRet", gDigiLiveIsoStartRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveAsyncAttemptCount", kDigiLiveAsyncAttemptCount, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveAsyncWaitLoopsPerAttempt",
                      kDigiLiveAsyncWaitLoopsPerAttempt,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveAsyncRetrySettleMilliseconds",
                      kDigiLiveAsyncRetrySettleMilliseconds,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveLastHarvestRet", gDigiLiveLastHarvestRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRReadIndex", gDigiLiveIRReadIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtrCatchUpEnabled", kDigiLiveIRCommandPtrCatchUpEnabled, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtrCatchUpMinPackets", kDigiLiveIRCommandPtrCatchUpMinPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtrCatchUpScanEnabled", kDigiLiveIRCommandPtrCatchUpScanEnabled, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtrCatchUpScanMaxPackets", kDigiLiveIRCommandPtrCatchUpScanMaxPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRSegmentCatchUpEnabled", kDigiLiveIRSegmentCatchUpEnabled, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpPacketCount",
                      kDigiLiveIRSegmentCatchUpPacketCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpFallbackToSinglePacket",
                      kDigiLiveIRSegmentCatchUpFallbackToSinglePacket,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtrPacketIndex", gDigiLiveIRCommandPtrPacketIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRHardwareCursorValid", gDigiLiveIRHardwareCursorValid, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRHardwarePacketCursor", gDigiLiveIRHardwarePacketCursor, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIRSoftwarePacketCursor", gDigiLiveIRSoftwarePacketCursor, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIRBacklogPackets", gDigiLiveIRBacklogPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpCount", gDigiLiveIREmptyCatchUpCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpSkippedPackets", gDigiLiveIREmptyCatchUpSkippedPackets, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpScanCount", gDigiLiveIREmptyCatchUpScanCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpScanFoundCount", gDigiLiveIREmptyCatchUpScanFoundCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpScanPackets", gDigiLiveIREmptyCatchUpScanPackets, 64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpAttemptCount",
                      gDigiLiveIRSegmentCatchUpAttemptCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpSuccessCount",
                      gDigiLiveIRSegmentCatchUpSuccessCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpFailureCount",
                      gDigiLiveIRSegmentCatchUpFailureCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpScannedSegments",
                      gDigiLiveIRSegmentCatchUpScannedSegments,
                      64);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpLastFromIndex", gDigiLiveIREmptyCatchUpLastFromIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpLastToIndex", gDigiLiveIREmptyCatchUpLastToIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpLastHardwareIndex", gDigiLiveIREmptyCatchUpLastHardwareIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpLastSkippedPackets", gDigiLiveIREmptyCatchUpLastSkippedPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREmptyCatchUpLastScannedPackets", gDigiLiveIREmptyCatchUpLastScannedPackets, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpLastFromIndex",
                      gDigiLiveIRSegmentCatchUpLastFromIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpLastToIndex",
                      gDigiLiveIRSegmentCatchUpLastToIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpLastHardwareIndex",
                      gDigiLiveIRSegmentCatchUpLastHardwareIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpLastSkippedPackets",
                      gDigiLiveIRSegmentCatchUpLastSkippedPackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIRSegmentCatchUpLastScannedSegments",
                      gDigiLiveIRSegmentCatchUpLastScannedSegments,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorIndex", gDigiLiveLastDescriptorIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorBytes", gDigiLiveLastDescriptorBytes, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorDataBlocks", gDigiLiveLastDescriptorDataBlocks, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorHeaderStatus", gDigiLiveLastDescriptorHeaderStatus, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorPayloadStatus", gDigiLiveLastDescriptorPayloadStatus, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorHeaderResCount", gDigiLiveLastDescriptorHeaderResCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastDescriptorPayloadResCount", gDigiLiveLastDescriptorPayloadResCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveSingleDescriptorReceiveEnabled", kDigiLiveSingleDescriptorReceiveEnabled, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRDescriptorDataStride", kDigiLiveIRDescriptorDataStride, 32);
    AddNumberProperty(properties, "ProbeDigiLiveReceiveSyncSize", kDigiLiveReceiveSyncSize, 32);
    AddNumberProperty(properties, "ProbeDigiLiveHarvestAttemptCount", gDigiLiveHarvestAttemptCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveHarvestSuccessCount", gDigiLiveHarvestSuccessCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveHarvestPacketCount", gDigiLiveHarvestPacketCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveHarvestFrameCount", gDigiLiveHarvestFrameCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveHarvestRxBytes", gDigiLiveHarvestRxBytes, 64);
    AddNumberProperty(properties, "ProbeDigiLiveDrainAttemptCount", gDigiLiveDrainAttemptCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveDrainBusyCount", gDigiLiveDrainBusyCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveLastHarvestPackets", gDigiLiveLastHarvestPackets, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastHarvestFrames", gDigiLiveLastHarvestFrames, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastHarvestBytes", gDigiLiveLastHarvestBytes, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastHarvestPeakAbs", gDigiLiveLastHarvestPeakAbs, 32);
    AddNumberProperty(properties, "ProbeDigiLiveLastHarvestLabelMismatchCount", gDigiLiveLastHarvestLabelMismatchCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveHarvestPeakAbs", gDigiLiveHarvestPeakAbs, 32);
    AddNumberProperty(properties, "ProbeDigiLiveDescriptorResetCount", gDigiLiveDescriptorResetCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveEmptyPollCount", gDigiLiveEmptyPollCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveSlot0LastLabel", gDigiLiveSlot0LastLabel, 32);
    AddNumberProperty(properties, "ProbeDigiLiveSlot0LastValue24", gDigiLiveSlot0LastValue24, 32);
    AddNumberProperty(properties, "ProbeDigiLiveSlot0NonzeroCount", gDigiLiveSlot0NonzeroCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveMidiMessageCount", gDigiLiveMidiMessageCount, 64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiPhysicalMessageCount",
                      gDigiLiveMidiPhysicalMessageCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiConsoleMessageCount",
                      gDigiLiveMidiConsoleMessageCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiInvalidLengthCount",
                      gDigiLiveMidiInvalidLengthCount,
                      64);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastRawWordBE", gDigiLiveMidiLastRawWordBE, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastMarker", gDigiLiveMidiLastMarker, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastData0", gDigiLiveMidiLastData0, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastData1", gDigiLiveMidiLastData1, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastControl", gDigiLiveMidiLastControl, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastPortNibble", gDigiLiveMidiLastPortNibble, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastLength", gDigiLiveMidiLastLength, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiLastMessageRawWordBE",
                      gDigiLiveMidiLastMessageRawWordBE,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastMessageMarker", gDigiLiveMidiLastMessageMarker, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastMessageData0", gDigiLiveMidiLastMessageData0, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiLastMessageData1", gDigiLiveMidiLastMessageData1, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiLastMessageControl",
                      gDigiLiveMidiLastMessageControl,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiLastMessagePortNibble",
                      gDigiLiveMidiLastMessagePortNibble,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveMidiLastMessageLength",
                      gDigiLiveMidiLastMessageLength,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiRecentIndex", gDigiLiveMidiRecentIndex, 32);
    AddNumberProperty(properties, "ProbeDigiLiveMidiRecentCount", gDigiLiveMidiRecentCount, 32);
    for (uint32_t i = 0; i < kDigiLiveMidiRecentMessageCount; ++i) {
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiLiveMidiRecentRawWord",
                                 i,
                                 "BE",
                                 gDigiLiveMidiRecentRawWordBE[i],
                                 32);
    }
    AddNumberProperty(properties, "ProbeDigiLiveDMACacheInhibitMapping", gDigiLiveBuffer.cacheInhibitMapping, 32);
    AddNumberProperty(properties, "ProbeDigiLiveDMAMappingRet", ReturnCodeToProperty(gDigiLiveBuffer.mappingRet), 32);
    AddNumberProperty(properties, "ProbeDigiLiveDMACPULength", gDigiLiveBuffer.cpuRange.length, 64);
    AddNumberProperty(properties, "ProbeDigiLiveSyncForDeviceRet", gDigiLiveSyncForDeviceRet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveSyncForCPURet", gDigiLiveSyncForCPURet, 32);
    AddNumberProperty(properties, "ProbeDigiLiveCompleteRet", gDigiLiveCompleteRet, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveReceiveFullBufferSyncEnabled",
                      kDigiLiveReceiveFullBufferSyncEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveReceivePayloadRangeSyncEnabled",
                      kDigiLiveReceivePayloadRangeSyncEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxDescriptorSyncForCPURet",
                      gDigiLiveRxDescriptorSyncForCPURet,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxPayloadSyncForCPURet",
                      gDigiLiveRxPayloadSyncForCPURet,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxDescriptorSyncCount",
                      gDigiLiveRxDescriptorSyncCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxDescriptorSyncBytes",
                      gDigiLiveRxDescriptorSyncBytes,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxPayloadSyncCount",
                      gDigiLiveRxPayloadSyncCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxPayloadSyncBytes",
                      gDigiLiveRxPayloadSyncBytes,
                      64);
    AddNumberProperty(properties, "ProbeDigiLiveXmitMaskSupport", gDigiLiveXmitMaskSupport, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRecvMaskSupport", gDigiLiveRecvMaskSupport, 32);
    AddNumberProperty(properties, "ProbeDigiLiveXmitContextSupported", gDigiLiveXmitContextSupported, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRecvContextSupported", gDigiLiveRecvContextSupported, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITCommandPtr", gDigiLiveITCommandPtr, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtr", gDigiLiveIRCommandPtr, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRContextMatch", gDigiLiveIRContextMatch, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITControlAfterRun", gDigiLiveITControlAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRControlAfterRun", gDigiLiveIRControlAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITControlAfterStop", gDigiLiveITControlAfterStop, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRControlAfterStop", gDigiLiveIRControlAfterStop, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITStopLoops", gDigiLiveITStopLoops, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRStopLoops", gDigiLiveIRStopLoops, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITMaskAfterRun", gDigiLiveITMaskAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRMaskAfterRun", gDigiLiveIRMaskAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITMaskAfterClear", gDigiLiveITMaskAfterClear, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRMaskAfterClear", gDigiLiveIRMaskAfterClear, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITEventAfterRun", gDigiLiveITEventAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREventAfterRun", gDigiLiveIREventAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITCommandPtrLastRead", gDigiLiveITCommandPtrLastRead, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIRCommandPtrLastRead", gDigiLiveIRCommandPtrLastRead, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITFirstPayloadResCount", gDigiLiveITFirstPayloadResCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITFirstPayloadStatus", gDigiLiveITFirstPayloadStatus, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITLastPayloadResCount", gDigiLiveITLastPayloadResCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITLastPayloadStatus", gDigiLiveITLastPayloadStatus, 32);
    AddNumberProperty(properties, "ProbeDigiLiveReceiveIRQInterval", kDigiLiveReceiveIRQInterval, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveHarvestMaxDescriptorsPerPass",
                      kDigiLiveHarvestMaxDescriptorsPerPass,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIREventLowWaterBypassEnabled",
                      kDigiLiveIREventLowWaterBypassEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIREventLowWaterBypassFrames",
                      kDigiLiveIREventLowWaterBypassFrames,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveHarvestSleepMilliseconds",
                      kDigiLiveHarvestSleepMilliseconds,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveIdleSleepMilliseconds",
                      kDigiLiveIdleSleepMilliseconds,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveWorkerLowWaterFrames",
                      kDigiLiveWorkerLowWaterFrames,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLivePrebufferTargetFrames",
                      kDigiLivePrebufferTargetFrames,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveSequenceReplayEnabled", kDigiLiveSequenceReplayEnabled, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayRequireContinuity",
                      kDigiLiveSequenceReplayRequireContinuity,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayPeriodPackets",
                      kDigiLiveSequenceReplayPeriodPackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayPeriodCount",
                      gDigiLiveSequenceReplayPeriodCount,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveSequenceReplayReady", gDigiLiveSequenceReplayReady, 32);
    AddNumberProperty(properties, "ProbeDigiLiveSequenceReplayActive", gDigiLiveSequenceReplayActive, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayApplyAttemptCount",
                      gDigiLiveSequenceReplayApplyAttemptCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayApplySuccessCount",
                      gDigiLiveSequenceReplayApplySuccessCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayApplyRet",
                      gDigiLiveSequenceReplayApplyRet,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayResetCount",
                      gDigiLiveSequenceReplayResetCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayInvalidCount",
                      gDigiLiveSequenceReplayInvalidCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayDiscontinuityCount",
                      gDigiLiveSequenceReplayDiscontinuityCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayObservedTotalDataBlocks",
                      gDigiLiveSequenceReplayObservedTotalDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayBadTotalCount",
                      gDigiLiveSequenceReplayBadTotalCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayLastBadTotalDataBlocks",
                      gDigiLiveSequenceReplayLastBadTotalDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayIdealMismatchCount",
                      gDigiLiveSequenceReplayIdealMismatchCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayFirstDataBlocks",
                      gDigiLiveSequenceReplayFirstDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayLastDataBlocks",
                      gDigiLiveSequenceReplayLastDataBlocks,
                      32);
    AddDataProperty(properties,
                    "ProbeDigiLiveSequenceReplayPeriod",
                    gDigiLiveSequenceReplayPeriod,
                    sizeof(gDigiLiveSequenceReplayPeriod));
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingEnabled",
                      kDigiLiveSequenceReplayMovingEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingDryRunEnabled",
                      kDigiLiveSequenceReplayMovingDryRunEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingRequireContinuity",
                      kDigiLiveSequenceReplayMovingRequireContinuity,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingUseCadencePhase",
                      kDigiLiveSequenceReplayMovingUseCadencePhase,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLearnCadenceFromQueue",
                      kDigiLiveSequenceReplayMovingLearnCadenceFromQueue,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingAllowCachedCadencePhase",
                      kDigiLiveSequenceReplayMovingAllowCachedCadencePhase,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingRequireCadenceMismatchZero",
                      kDigiLiveSequenceReplayMovingRequireCadenceMismatchZero,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLiveWriteGuardEnabled",
                      kDigiLiveSequenceReplayMovingLiveWriteGuardEnabled,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingGuardMinStartDistancePackets",
                      kDigiLiveSequenceReplayMovingGuardMinStartDistancePackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingQueuePackets",
                      kDigiLiveSequenceReplayMovingQueuePackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingUpdatePackets",
                      kDigiLiveSequenceReplayMovingUpdatePackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLeadPackets",
                      kDigiLiveSequenceReplayMovingLeadPackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayPeriodDataBlocks",
                      DigiLiveSequenceReplayPeriodDataBlocks(),
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingQueueReadIndex",
                      gDigiLiveSequenceReplayMovingQueueReadIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingQueueWriteIndex",
                      gDigiLiveSequenceReplayMovingQueueWriteIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingQueueCount",
                      gDigiLiveSequenceReplayMovingQueueCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingAppendCount",
                      gDigiLiveSequenceReplayMovingAppendCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingDropCount",
                      gDigiLiveSequenceReplayMovingDropCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingClearCount",
                      gDigiLiveSequenceReplayMovingClearCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingDiscontinuityCount",
                      gDigiLiveSequenceReplayMovingDiscontinuityCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingInvalidCount",
                      gDigiLiveSequenceReplayMovingInvalidCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingUpdateAttemptCount",
                      gDigiLiveSequenceReplayMovingUpdateAttemptCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingUpdateSuccessCount",
                      gDigiLiveSequenceReplayMovingUpdateSuccessCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingUpdatePacketCount",
                      gDigiLiveSequenceReplayMovingUpdatePacketCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount",
                      gDigiLiveSequenceReplayMovingDryRunSuccessCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingDryRunPacketCount",
                      gDigiLiveSequenceReplayMovingDryRunPacketCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingShortQueueCount",
                      gDigiLiveSequenceReplayMovingShortQueueCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingBadTotalCount",
                      gDigiLiveSequenceReplayMovingBadTotalCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount",
                      gDigiLiveSequenceReplayMovingBadCommandPtrCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadencePhaseUseCount",
                      gDigiLiveSequenceReplayMovingCadencePhaseUseCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadencePhasePacketCount",
                      gDigiLiveSequenceReplayMovingCadencePhasePacketCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadenceNotReadyCount",
                      gDigiLiveSequenceReplayMovingCadenceNotReadyCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadenceMismatchRejectCount",
                      gDigiLiveSequenceReplayMovingCadenceMismatchRejectCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadenceLearnCount",
                      gDigiLiveSequenceReplayMovingCadenceLearnCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadenceLearnPacketCount",
                      gDigiLiveSequenceReplayMovingCadenceLearnPacketCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadenceLearnRejectCount",
                      gDigiLiveSequenceReplayMovingCadenceLearnRejectCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCadenceCachedUseCount",
                      gDigiLiveSequenceReplayMovingCadenceCachedUseCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingGuardEligibleCount",
                      gDigiLiveSequenceReplayMovingGuardEligibleCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingGuardRejectCount",
                      gDigiLiveSequenceReplayMovingGuardRejectCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingGuardDryRunWouldWriteCount",
                      gDigiLiveSequenceReplayMovingGuardDryRunWouldWriteCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingGuardDryRunWouldRejectCount",
                      gDigiLiveSequenceReplayMovingGuardDryRunWouldRejectCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastCurrentPacketIndex",
                      gDigiLiveSequenceReplayMovingLastCurrentPacketIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastUpdateStartIndex",
                      gDigiLiveSequenceReplayMovingLastUpdateStartIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastUpdatePackets",
                      gDigiLiveSequenceReplayMovingLastUpdatePackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastTotalDataBlocks",
                      gDigiLiveSequenceReplayMovingLastTotalDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastRawTotalDataBlocks",
                      gDigiLiveSequenceReplayMovingLastRawTotalDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastCadencePhase",
                      gDigiLiveSequenceReplayMovingLastCadencePhase,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastCadenceMismatchCount",
                      gDigiLiveSequenceReplayMovingLastCadenceMismatchCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastCadenceSource",
                      gDigiLiveSequenceReplayMovingLastCadenceSource,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCachedCadencePhase",
                      gDigiLiveSequenceReplayMovingCachedCadencePhase,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingCachedCadenceMismatchCount",
                      gDigiLiveSequenceReplayMovingCachedCadenceMismatchCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastStartDistancePackets",
                      gDigiLiveSequenceReplayMovingLastStartDistancePackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastEndDistancePackets",
                      gDigiLiveSequenceReplayMovingLastEndDistancePackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastWindowWrapsHardware",
                      gDigiLiveSequenceReplayMovingLastWindowWrapsHardware,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastGuardWouldWrite",
                      gDigiLiveSequenceReplayMovingLastGuardWouldWrite,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastStartDBC",
                      gDigiLiveSequenceReplayMovingLastStartDBC,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastEndDBC",
                      gDigiLiveSequenceReplayMovingLastEndDBC,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveSequenceReplayMovingLastSyncRet",
                      gDigiLiveSequenceReplayMovingLastSyncRet,
                      32);
    AddNumberProperty(properties, "ProbeDigiLiveITEventPollCount", gDigiLiveITEventPollCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveITEventHitCount", gDigiLiveITEventHitCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveITEventMissCount", gDigiLiveITEventMissCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveITEventClearCount", gDigiLiveITEventClearCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveITEventLastBeforeHarvest", gDigiLiveITEventLastBeforeHarvest, 32);
    AddNumberProperty(properties, "ProbeDigiLiveITEventLastAfterClear", gDigiLiveITEventLastAfterClear, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREventPollCount", gDigiLiveIREventPollCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREventHitCount", gDigiLiveIREventHitCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREventMissCount", gDigiLiveIREventMissCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREventClearCount", gDigiLiveIREventClearCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREventGateSkipCount", gDigiLiveIREventGateSkipCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREventGateBypassCount", gDigiLiveIREventGateBypassCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveIREventConsecutiveMissCount", gDigiLiveIREventConsecutiveMissCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREventLastBeforeHarvest", gDigiLiveIREventLastBeforeHarvest, 32);
    AddNumberProperty(properties, "ProbeDigiLiveIREventLastAfterClear", gDigiLiveIREventLastAfterClear, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxHeader0Raw", gDigiLiveRxHeader0Raw, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxHeader1Raw", gDigiLiveRxHeader1Raw, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxHeader2Raw", gDigiLiveRxHeader2Raw, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxHeader3Raw", gDigiLiveRxHeader3Raw, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxIsoHeader", gDigiLiveRxIsoHeader, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxTimestamp", gDigiLiveRxTimestamp, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxCycle", gDigiLiveRxCycle, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxExpectedCycle", gDigiLiveRxExpectedCycle, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxCycleDelta", gDigiLiveRxCycleDelta, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxMaxCycleDelta", gDigiLiveRxMaxCycleDelta, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxCycleLostCount", gDigiLiveRxCycleLostCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveRxCyclePacketCount", gDigiLiveRxCyclePacketCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveRxCIPHeader0", gDigiLiveRxCIPHeader0, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxCIPHeader1", gDigiLiveRxCIPHeader1, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxSID", gDigiLiveRxSID, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxExpectedSID", gDigiLiveRxExpectedSID, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxDBS", gDigiLiveRxDBS, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxSPH", gDigiLiveRxSPH, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxFMT", gDigiLiveRxFMT, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxFDF", gDigiLiveRxFDF, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxEventCount", gDigiLiveRxEventCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxMinEventCount", gDigiLiveRxMinEventCount, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxMaxEventCount", gDigiLiveRxMaxEventCount, 32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxPayloadRemainderBytes",
                      gDigiLiveRxPayloadRemainderBytes,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxStreamProcessorPacketCount",
                      gDigiLiveRxStreamProcessorPacketCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxStreamProcessorValidPacketCount",
                      gDigiLiveRxStreamProcessorValidPacketCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxPayloadRemainderCount",
                      gDigiLiveRxPayloadRemainderCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxUnexpectedSIDCount",
                      gDigiLiveRxUnexpectedSIDCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxUnexpectedDBSCount",
                      gDigiLiveRxUnexpectedDBSCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxUnexpectedSPHCount",
                      gDigiLiveRxUnexpectedSPHCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxUnexpectedFMTCount",
                      gDigiLiveRxUnexpectedFMTCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxUnexpectedFDFCount",
                      gDigiLiveRxUnexpectedFDFCount,
                      64);
    AddNumberProperty(properties, "ProbeDigiLiveRxDBC", gDigiLiveRxDBC, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxExpectedDBC", gDigiLiveRxExpectedDBC, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxDBCDelta", gDigiLiveRxDBCDelta, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxMaxDBCDelta", gDigiLiveRxMaxDBCDelta, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxDBCPacketCount", gDigiLiveRxDBCPacketCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveRxDBCLostCount", gDigiLiveRxDBCLostCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveRxDBCInitCount", gDigiLiveRxDBCInitCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveRxSYT", gDigiLiveRxSYT, 32);
    AddNumberProperty(properties, "ProbeDigiLiveRxSYTNoInfoCount", gDigiLiveRxSYTNoInfoCount, 64);
    AddNumberProperty(properties, "ProbeDigiLiveRxSYTZeroCount", gDigiLiveRxSYTZeroCount, 64);
    for (size_t index = 0;
         index < sizeof(gDigiLiveRxDataBlockHistogram) / sizeof(gDigiLiveRxDataBlockHistogram[0]);
         ++index) {
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiLiveRxDataBlocks",
                                 index,
                                 "Count",
                                 gDigiLiveRxDataBlockHistogram[index],
                                 64);
    }
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxUnexpectedDataBlockCount",
                      gDigiLiveRxUnexpectedDataBlockCount,
                      64);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadencePeriodPackets",
                      kDigiLiveRxCadencePeriodPackets,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadencePeriodCount",
                      gDigiLiveRxCadencePeriodCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceReady",
                      gDigiLiveRxCadenceReady,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceResetCount",
                      gDigiLiveRxCadenceResetCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceInvalidCount",
                      gDigiLiveRxCadenceInvalidCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceDiscontinuityCount",
                      gDigiLiveRxCadenceDiscontinuityCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceObservedTotalDataBlocks",
                      gDigiLiveRxCadenceObservedTotalDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceBadTotalCount",
                      gDigiLiveRxCadenceBadTotalCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceLastBadTotalDataBlocks",
                      gDigiLiveRxCadenceLastBadTotalDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceIdealMismatchCount",
                      gDigiLiveRxCadenceIdealMismatchCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceBestPhase",
                      gDigiLiveRxCadenceBestPhase,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceBestPhaseMismatchCount",
                      gDigiLiveRxCadenceBestPhaseMismatchCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceFirstDataBlocks",
                      gDigiLiveRxCadenceFirstDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiLiveRxCadenceLastDataBlocks",
                      gDigiLiveRxCadenceLastDataBlocks,
                      32);
    AddDataProperty(properties,
                    "ProbeDigiLiveRxCadencePeriod",
                    gDigiLiveRxCadencePeriod,
                    sizeof(gDigiLiveRxCadencePeriod));
    AddNumberProperty(properties, "ProbeAudioRuntimeInputCallbackCount", gAudioInputCallbackCount, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputLastBufferFrameSize", gAudioInputLastBufferFrameSize, 32);
    AddNumberProperty(properties, "ProbeAudioRuntimeInputLastSampleTime", gAudioInputLastSampleTime, 64);
    AddNumberProperty(properties, "ProbeAudioRuntimeZeroTimestampHostTime", gAudioZeroTimestampHostTime, 64);

    gDriverInstance->SetProperties(properties);
    properties->release();
}

void
ResetAudioRingBuffer()
{
    gAudioRingWriteFrame = 0;
    gAudioRingReadFrame = 0;
    gAudioRingProducedFrames = 0;
    gAudioRingConsumedFrames = 0;
    gAudioRingRepeatedFrames = 0;
    gAudioRingUnderrunFrames = 0;
    gAudioRingOverrunFrames = 0;
    gAudioRingCurrentFillFrames = 0;
    gAudioRingMaxFillFrames = 0;
    gAudioRingAppendCount = 0;
    gAudioRingLastAppendFrames = 0;
    gAudioRingLastAppendDroppedFrames = 0;
    gAudioRingLastAppendGeneration = 0;
    gAudioRingLastConsumeFrames = 0;
    gAudioRingLastConsumeUnderrunFrames = 0;
    gAudioRingLastConsumeRepeatedFrames = 0;
    for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
        gAudioLastOutputFrame[channel] = 0;
    }
}

uint32_t
AudioOutputRingFillFrames()
{
    uint64_t fillFrames = gAudioOutputRingWriteFrame - gAudioOutputRingReadFrame;
    if (fillFrames > kAudioOutputRingBufferFrameCount) {
        fillFrames = kAudioOutputRingBufferFrameCount;
    }
    return static_cast<uint32_t>(fillFrames);
}

void
UpdateAudioOutputRingFill()
{
    gAudioOutputRingCurrentFillFrames = AudioOutputRingFillFrames();
    if (gAudioOutputRingCurrentFillFrames > gAudioOutputRingMaxFillFrames) {
        gAudioOutputRingMaxFillFrames = gAudioOutputRingCurrentFillFrames;
    }
}

void
ResetAudioOutputRingBuffer()
{
    gAudioOutputRingWriteFrame = 0;
    gAudioOutputRingReadFrame = 0;
    gAudioOutputRingProducedFrames = 0;
    gAudioOutputRingConsumedFrames = 0;
    gAudioOutputRingUnderrunFrames = 0;
    gAudioOutputRingOverrunFrames = 0;
    gAudioOutputRingCurrentFillFrames = 0;
    gAudioOutputRingMaxFillFrames = 0;
    gAudioOutputRingAppendCount = 0;
    gAudioOutputRingLastAppendFrames = 0;
    gAudioOutputRingLastAppendDroppedFrames = 0;
    gAudioOutputRingLastConsumeFrames = 0;
    gAudioOutputRingLastConsumeUnderrunFrames = 0;
    gAudioOutputRingPrebuffered = 0;
    gAudioOutputRingPrebufferReadyCount = 0;
    gAudioOutputRingPrebufferHoldCount = 0;
}

void
HarvestDigiLiveForAudioCallback(uint32_t requestedFrameCount)
{
    if (kAudioCallbackHarvestEnabled == 0 || gDigiLiveRunning == 0) {
        return;
    }

    uint32_t lowWaterFrames =
        requestedFrameCount + kAudioCallbackHarvestLowWaterFrames;
    for (uint32_t attempt = 0;
         attempt < kAudioCallbackHarvestMaxAttempts &&
         gDigiLiveRunning != 0 &&
         gAudioRingCurrentFillFrames < lowWaterFrames;
         ++attempt) {
        gAudioInputCallbackHarvestAttemptCount++;
        kern_return_t ret = HarvestDigiLiveIsoStream();
        gAudioInputCallbackHarvestRet = ReturnCodeToProperty(ret);
        if (ret == kIOReturnSuccess) {
            gAudioInputCallbackHarvestSuccessCount++;
        } else {
            break;
        }
    }
    gAudioInputCallbackHarvestLastFillFrames = gAudioRingCurrentFillFrames;
}

void
ClearAudioInputBuffer()
{
    if (gAudioInputCPUAddress.address == 0 ||
        gAudioInputCPUAddress.length < kAudioInputBufferBytes) {
        return;
    }

    volatile int32_t * samples =
        reinterpret_cast<volatile int32_t *>(gAudioInputCPUAddress.address);
    for (uint32_t frame = 0; frame < kAudioInputBufferFrameCount; ++frame) {
        for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
            samples[frame * kDigi00xDuplexIRCapturePCMChannelCount + channel] = 0;
        }
    }
}

void
ClearAudioOutputBuffer()
{
    if (gAudioOutputCPUAddress.address == 0 ||
        gAudioOutputCPUAddress.length < kAudioOutputBufferBytes) {
        return;
    }

    volatile int32_t * samples =
        reinterpret_cast<volatile int32_t *>(gAudioOutputCPUAddress.address);
    for (uint32_t frame = 0; frame < kAudioOutputBufferFrameCount; ++frame) {
        for (uint32_t channel = 0; channel < kAudioOutputChannelCount; ++channel) {
            samples[frame * kAudioOutputChannelCount + channel] = 0;
        }
    }
}

void
AppendPCMFrameToAudioOutputRing(const int32_t samples[kAudioOutputChannelCount])
{
    if (gAudioOutputRingWriteFrame - gAudioOutputRingReadFrame >=
        kAudioOutputRingBufferFrameCount) {
        gAudioOutputRingReadFrame++;
        gAudioOutputRingOverrunFrames++;
        gAudioOutputRingLastAppendDroppedFrames++;
    }

    uint32_t ringFrame =
        static_cast<uint32_t>(gAudioOutputRingWriteFrame %
                              kAudioOutputRingBufferFrameCount);
    for (uint32_t channel = 0; channel < kAudioOutputChannelCount; ++channel) {
        gAudioOutputRingPCM[ringFrame][channel] = samples[channel];
    }
    __sync_synchronize();
    gAudioOutputRingWriteFrame++;
    gAudioOutputRingProducedFrames++;
}

void
AppendAudioOutputBufferToRing(uint32_t frameCount, uint64_t sampleTime)
{
    if (gAudioOutputCPUAddress.address == 0 ||
        gAudioOutputCPUAddress.length < kAudioOutputBufferBytes ||
        frameCount == 0) {
        gAudioOutputRingLastAppendFrames = 0;
        gAudioOutputRingLastAppendDroppedFrames = 0;
        return;
    }

    if (frameCount > kAudioOutputBufferFrameCount) {
        frameCount = kAudioOutputBufferFrameCount;
    }

    volatile int32_t * samples =
        reinterpret_cast<volatile int32_t *>(gAudioOutputCPUAddress.address);
    gAudioOutputRingLastAppendDroppedFrames = 0;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint32_t srcFrame = frame;
        if (kAudioOutputBufferOffsetMode == 1) {
            srcFrame = static_cast<uint32_t>((sampleTime + frame) %
                                             kAudioOutputBufferFrameCount);
        } else if (kAudioOutputBufferOffsetMode == 2) {
            uint64_t blockStart =
                sampleTime + kAudioOutputBufferFrameCount - frameCount;
            srcFrame = static_cast<uint32_t>((blockStart + frame) %
                                             kAudioOutputBufferFrameCount);
        }
        int32_t frameSamples[kAudioOutputChannelCount] = {};
        for (uint32_t channel = 0; channel < kAudioOutputChannelCount; ++channel) {
            frameSamples[channel] =
                samples[srcFrame * kAudioOutputChannelCount + channel];
        }
        AppendPCMFrameToAudioOutputRing(frameSamples);
    }
    gAudioOutputRingAppendCount++;
    gAudioOutputRingLastAppendFrames = frameCount;
    UpdateAudioOutputRingFill();
}

void
WritePCMFrameToAudioInputBuffer(uint64_t sampleFrame,
                                const int32_t frameSamples[kDigi00xDuplexIRCapturePCMChannelCount])
{
    if (kAudioDirectInputBufferEnabled == 0 ||
        gAudioInputCPUAddress.address == 0 ||
        gAudioInputCPUAddress.length < kAudioInputBufferBytes) {
        return;
    }

    volatile int32_t * samples =
        reinterpret_cast<volatile int32_t *>(gAudioInputCPUAddress.address);
    uint32_t dstFrame = static_cast<uint32_t>(sampleFrame % kAudioInputBufferFrameCount);
    for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
        samples[dstFrame * kDigi00xDuplexIRCapturePCMChannelCount + channel] =
            frameSamples[channel];
        gAudioLastOutputFrame[channel] = frameSamples[channel];
    }
    __sync_synchronize();
}

void
AppendPCMFrameToAudioRing(const int32_t samples[kDigi00xDuplexIRCapturePCMChannelCount])
{
    if (gAudioRingWriteFrame - gAudioRingReadFrame >= kAudioRingBufferFrameCount) {
        gAudioRingReadFrame++;
        gAudioRingOverrunFrames++;
        gAudioRingLastAppendDroppedFrames++;
    }

    uint32_t ringFrame = static_cast<uint32_t>(gAudioRingWriteFrame % kAudioRingBufferFrameCount);
    for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
        gAudioRingPCM[ringFrame][channel] = samples[channel];
    }
    WritePCMFrameToAudioInputBuffer(gAudioRingWriteFrame, samples);
    __sync_synchronize();
    gAudioRingWriteFrame++;
    gAudioRingProducedFrames++;
}

void
UpdateAudioRingFillAfterAppend(uint32_t frames)
{
    uint64_t fillFrames = gAudioRingWriteFrame - gAudioRingReadFrame;
    if (fillFrames > kAudioRingBufferFrameCount) {
        fillFrames = kAudioRingBufferFrameCount;
    }
    gAudioRingCurrentFillFrames = static_cast<uint32_t>(fillFrames);
    if (gAudioRingCurrentFillFrames > gAudioRingMaxFillFrames) {
        gAudioRingMaxFillFrames = gAudioRingCurrentFillFrames;
    }
    gAudioRingAppendCount++;
    gAudioRingLastAppendFrames = frames;
    gAudioRingLastAppendGeneration = gAudioCaptureGeneration;
}

void
AppendDigiCaptureToAudioRing(const DigiDuplexDiagnostics * diagnostics, uint32_t frames)
{
    if (diagnostics == nullptr || frames == 0) {
        gAudioRingLastAppendFrames = 0;
        gAudioRingLastAppendDroppedFrames = 0;
        return;
    }

    gAudioRingLastAppendDroppedFrames = 0;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        AppendPCMFrameToAudioRing(diagnostics->irCapturePCMS24[frame]);
    }
    UpdateAudioRingFillAfterAppend(frames);
}

void
FillAudioInputBuffer(uint32_t frameCount, uint64_t sampleTime)
{
    if (gAudioInputCPUAddress.address == 0 ||
        gAudioInputCPUAddress.length < kAudioInputBufferBytes) {
        return;
    }

    volatile int32_t * samples =
        reinterpret_cast<volatile int32_t *>(gAudioInputCPUAddress.address);
    if (frameCount > kAudioInputBufferFrameCount) {
        frameCount = kAudioInputBufferFrameCount;
    }

    if (kAudioDirectInputBufferEnabled != 0) {
        uint64_t producedFrame = gAudioRingWriteFrame;
        uint64_t requestEndFrame = sampleTime + frameCount;
        uint32_t underrunFrames = 0;
        uint32_t repeatedFrames = 0;
        for (uint32_t frame = 0; frame < frameCount; ++frame) {
            uint64_t absoluteFrame = sampleTime + frame;
            uint32_t dstFrame = static_cast<uint32_t>(absoluteFrame % kAudioInputBufferFrameCount);
            bool frameReady =
                absoluteFrame < producedFrame &&
                producedFrame - absoluteFrame < kAudioInputBufferFrameCount;
            if (frameReady) {
                for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
                    gAudioLastOutputFrame[channel] =
                        samples[dstFrame * kDigi00xDuplexIRCapturePCMChannelCount + channel];
                }
            } else {
                underrunFrames++;
                repeatedFrames++;
                for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
                    samples[dstFrame * kDigi00xDuplexIRCapturePCMChannelCount + channel] =
                        gAudioLastOutputFrame[channel];
                }
            }
        }

        uint64_t consumedFrame = requestEndFrame;
        if (consumedFrame > producedFrame) {
            consumedFrame = producedFrame;
        }
        if (consumedFrame > gAudioRingReadFrame) {
            gAudioRingReadFrame = consumedFrame;
        }
        gAudioRingConsumedFrames = gAudioRingReadFrame;

        uint64_t fillFrames = producedFrame - gAudioRingReadFrame;
        if (fillFrames > kAudioRingBufferFrameCount) {
            fillFrames = kAudioRingBufferFrameCount;
        }
        gAudioRingCurrentFillFrames = static_cast<uint32_t>(fillFrames);
        gAudioRingLastConsumeFrames = frameCount;
        gAudioRingLastConsumeUnderrunFrames = underrunFrames;
        gAudioRingLastConsumeRepeatedFrames = repeatedFrames;
        gAudioRingUnderrunFrames += underrunFrames;
        gAudioRingRepeatedFrames += repeatedFrames;
        return;
    }

    uint32_t underrunFrames = 0;
    uint32_t repeatedFrames = 0;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint32_t dstFrame = static_cast<uint32_t>((sampleTime + frame) % kAudioInputBufferFrameCount);
        if (gAudioRingReadFrame < gAudioRingWriteFrame) {
            uint32_t ringFrame = static_cast<uint32_t>(gAudioRingReadFrame % kAudioRingBufferFrameCount);
            for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
                int32_t sample = gAudioRingPCM[ringFrame][channel];
                samples[dstFrame * kDigi00xDuplexIRCapturePCMChannelCount + channel] = sample;
                gAudioLastOutputFrame[channel] = sample;
            }
            gAudioRingReadFrame++;
            gAudioRingConsumedFrames++;
        } else {
            underrunFrames++;
            repeatedFrames++;
            for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
                samples[dstFrame * kDigi00xDuplexIRCapturePCMChannelCount + channel] =
                    gAudioLastOutputFrame[channel];
            }
        }
    }

    uint64_t fillFrames = gAudioRingWriteFrame - gAudioRingReadFrame;
    if (fillFrames > kAudioRingBufferFrameCount) {
        fillFrames = kAudioRingBufferFrameCount;
    }
    gAudioRingCurrentFillFrames = static_cast<uint32_t>(fillFrames);
    gAudioRingLastConsumeFrames = frameCount;
    gAudioRingLastConsumeUnderrunFrames = underrunFrames;
    gAudioRingLastConsumeRepeatedFrames = repeatedFrames;
    gAudioRingUnderrunFrames += underrunFrames;
    gAudioRingRepeatedFrames += repeatedFrames;
}

void
CopyDigiCaptureForAudio(const DigiDuplexDiagnostics * diagnostics)
{
    if (diagnostics == nullptr) {
        return;
    }

    uint32_t frames = diagnostics->irCapturePCMFrameCount;
    if (frames > kDigi00xDuplexIRCapturePCMFrameLimit) {
        frames = kDigi00xDuplexIRCapturePCMFrameLimit;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
            gAudioCapturePCM[frame][channel] = diagnostics->irCapturePCMS24[frame][channel];
        }
    }
    for (uint32_t frame = frames; frame < kDigi00xDuplexIRCapturePCMFrameLimit; ++frame) {
        for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
            gAudioCapturePCM[frame][channel] = 0;
        }
    }

    gAudioCaptureFrameCount = frames;
    gAudioCapturePeakAbs = diagnostics->irCapturePCMPeakAbs;
    gAudioCaptureGeneration++;
    AppendDigiCaptureToAudioRing(diagnostics, frames);
}

kern_return_t
SetAudioObjectName(IOUserAudioObject * object, const char * name)
{
    if (object == nullptr || name == nullptr) {
        return kIOReturnBadArgument;
    }
    OSString * string = OSString::withCString(name);
    if (string == nullptr) {
        return kIOReturnNoMemory;
    }
    kern_return_t ret = object->SetName(string);
    string->release();
    return ret;
}

kern_return_t
SetAudioDriverName(IOUserAudioDriver * driver, const char * name)
{
    if (driver == nullptr || name == nullptr) {
        return kIOReturnBadArgument;
    }
    OSString * string = OSString::withCString(name);
    if (string == nullptr) {
        return kIOReturnNoMemory;
    }
    kern_return_t ret = driver->SetName(string);
    string->release();
    return ret;
}

kern_return_t
ConfigureAudioDevice(FireWireOHCIProbe * driver)
{
    if (driver == nullptr) {
        return kIOReturnBadArgument;
    }

    gAudioDevice.reset();
    gAudioInputStream.reset();
    gAudioOutputStream.reset();
    OSSafeReleaseNULL(gAudioInputBuffer);
    OSSafeReleaseNULL(gAudioOutputBuffer);
    gAudioInputCPUAddress = {};
    gAudioOutputCPUAddress = {};
    gAudioInputCallbackCount = 0;
    gAudioInputLastBufferFrameSize = 0;
    gAudioInputLastSampleTime = 0;
    gAudioOutputCallbackCount = 0;
    gAudioOutputLastBufferFrameSize = 0;
    gAudioOutputLastSampleTime = 0;
    gAudioZeroTimestampHostTime = 0;
    gAudioStartDeviceCount = 0;
    gAudioStopDeviceCount = 0;
    gAudioStartDeviceObjectID = 0;
    gAudioStopDeviceObjectID = 0;
    gAudioStartDeviceStage = 0;
    gAudioStopDeviceStage = 0;
    gAudioRuntimeDeviceStarted = 0;
    gPowerStateLastFlags = 0xffffffff;
    gPowerStateLastRet = ReturnCodeToProperty(kIOReturnNotReady);
    gPowerStateChangeCount = 0;
    gPowerStateOnCount = 0;
    gPowerStateOffCount = 0;
    gPowerStateLowCount = 0;
    gPowerStateLiveStopCount = 0;
    gPowerStateWakeRestartRequestCount = 0;
    gAudioDeviceStartIOCount = 0;
    gAudioDeviceStopIOCount = 0;
    gAudioOutputStreamCreateRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioOutputAddStreamRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioOutputStreamActiveRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioOutputBufferCreateRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioOutputBufferSetLengthRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioOutputBufferRangeRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRefreshCaptureAttemptCount = 0;
    gAudioRefreshCaptureSuccessCount = 0;
    gAudioRefreshCaptureInProgress = 0;
    gAudioRefreshCaptureRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRefreshCaptureFrameCount = 0;
    gAudioRefreshCapturePeakAbs = 0;
    gAudioRefreshCaptureRxBytes = 0;
    gAudioRefreshWorkerRunning = 0;
    gAudioRefreshWorkerStopWaitLoops = 0;
    gAudioRefreshWorkerLastRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRefreshWorkerLastGeneration = gAudioCaptureGeneration;
    gAudioRefreshWorkerLivePublishCount = 0;
    gAudioRefreshWorkerLivePublishSkipCount = 0;
    gAudioRefreshWorkerBacklogNoSleepCount = 0;
    gAudioRefreshWorkerLowWaterNoSleepCount = 0;
    ResetOHCIInterruptDiagnostics();
    gAudioInputCallbackHarvestAttemptCount = 0;
    gAudioInputCallbackHarvestSuccessCount = 0;
    gAudioInputCallbackHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioInputCallbackHarvestLastFillFrames = 0;
    gAudioRuntimeRestartInProgress = 0;
    gAudioRuntimeRestartLastReason = 0;
    gAudioRuntimeRestartLastRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRuntimeRestartLastLiveRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRuntimeRestartLastDigiRunning = 0;
    gAudioRuntimeRestartLastDigiReady = 0;
    gAudioRuntimeRestartLastWorkerRunning = 0;
    gAudioRuntimeRestartRequestCount = 0;
    gAudioRuntimeRestartDispatchCount = 0;
    gAudioRuntimeRestartSuccessCount = 0;
    gAudioRuntimeRestartSkippedCount = 0;
    gAudioRuntimeRestartBusyCount = 0;
    gDigiLiveStartAttemptCount = 0;
    gDigiLiveStartSuccessCount = 0;
    gDigiLiveStopAttemptCount = 0;
    gDigiLiveStopSuccessCount = 0;
    gDigiLiveStopDrainWaitLoops = 0;
    gDigiLiveRunning = 0;
    gDigiLiveStarting = 0;
    gDigiLiveStopping = 0;
    gDigiLiveReady = 0;
    gDigiLiveState = kDigiLiveStateStopped;
    gDigiLiveDrainBusy = 0;
    gDigiLiveStartRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveStopRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveStartStage = 0;
    gDigiLiveBeginTransactionStage = 0;
    gDigiLiveBeginTransactionRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveIsoStartStage = 0;
    gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveIRReadIndex = 0;
    gDigiLiveIRCommandPtrPacketIndex = 0xffffffff;
    gDigiLiveIRHardwareCursorValid = 0;
    gDigiLiveIRHardwarePacketCursor = 0;
    gDigiLiveIRSoftwarePacketCursor = 0;
    gDigiLiveIRBacklogPackets = 0;
    gDigiLiveIREmptyCatchUpCount = 0;
    gDigiLiveIREmptyCatchUpSkippedPackets = 0;
    gDigiLiveIREmptyCatchUpScanCount = 0;
    gDigiLiveIREmptyCatchUpScanFoundCount = 0;
    gDigiLiveIREmptyCatchUpScanPackets = 0;
    gDigiLiveIREmptyCatchUpLastFromIndex = 0xffffffff;
    gDigiLiveIREmptyCatchUpLastToIndex = 0xffffffff;
    gDigiLiveIREmptyCatchUpLastHardwareIndex = 0xffffffff;
    gDigiLiveIREmptyCatchUpLastSkippedPackets = 0;
    gDigiLiveIREmptyCatchUpLastScannedPackets = 0;
    gDigiLiveLastDescriptorIndex = 0xffffffff;
    gDigiLiveLastDescriptorBytes = 0;
    gDigiLiveLastDescriptorDataBlocks = 0;
    gDigiLiveLastDescriptorHeaderStatus = 0;
    gDigiLiveLastDescriptorPayloadStatus = 0;
    gDigiLiveLastDescriptorHeaderResCount = 0;
    gDigiLiveLastDescriptorPayloadResCount = 0;
    gDigiLiveHarvestAttemptCount = 0;
    gDigiLiveHarvestSuccessCount = 0;
    gDigiLiveHarvestPacketCount = 0;
    gDigiLiveHarvestFrameCount = 0;
    gDigiLiveHarvestRxBytes = 0;
    gDigiLiveDrainAttemptCount = 0;
    gDigiLiveDrainBusyCount = 0;
    gDigiLiveLastHarvestPackets = 0;
    gDigiLiveLastHarvestFrames = 0;
    gDigiLiveLastHarvestBytes = 0;
    gDigiLiveLastHarvestPeakAbs = 0;
    gDigiLiveLastHarvestLabelMismatchCount = 0;
    gDigiLiveHarvestPeakAbs = 0;
    gDigiLiveDescriptorResetCount = 0;
    gDigiLiveEmptyPollCount = 0;
    gDigiLiveSlot0LastLabel = 0;
    gDigiLiveSlot0LastValue24 = 0;
    gDigiLiveSlot0NonzeroCount = 0;
    gDigiLiveMidiMessageCount = 0;
    gDigiLiveMidiPhysicalMessageCount = 0;
    gDigiLiveMidiConsoleMessageCount = 0;
    gDigiLiveMidiInvalidLengthCount = 0;
    gDigiLiveMidiLastRawWordBE = 0;
    gDigiLiveMidiLastMarker = 0;
    gDigiLiveMidiLastData0 = 0;
    gDigiLiveMidiLastData1 = 0;
    gDigiLiveMidiLastControl = 0;
    gDigiLiveMidiLastPortNibble = 0;
    gDigiLiveMidiLastLength = 0;
    gDigiLiveMidiLastMessageRawWordBE = 0;
    gDigiLiveMidiLastMessageMarker = 0;
    gDigiLiveMidiLastMessageData0 = 0;
    gDigiLiveMidiLastMessageData1 = 0;
    gDigiLiveMidiLastMessageControl = 0;
    gDigiLiveMidiLastMessagePortNibble = 0;
    gDigiLiveMidiLastMessageLength = 0;
    gDigiLiveMidiRecentIndex = 0;
    gDigiLiveMidiRecentCount = 0;
    for (uint32_t i = 0; i < kDigiLiveMidiRecentMessageCount; ++i) {
        gDigiLiveMidiRecentRawWordBE[i] = 0;
    }
    for (uint32_t port = 0; port < kDigiLiveMidiPortCount; ++port) {
        gDigiLiveMidiPendingByteCount[port] = 0;
        for (uint32_t byte = 0; byte < kDigiLiveMidiBytesPerMessage; ++byte) {
            gDigiLiveMidiPendingBytes[port][byte] = 0;
        }
    }
    gDigiLiveMidiDecodedMessageCount = 0;
    gDigiLiveMidiNoteMessageCount = 0;
    gDigiLiveMidiControlChangeMessageCount = 0;
    gDigiLiveMidiLastDecodedMessage = 0;
    gDigiLiveMidiLastDecodedPort = 0;
    gDigiLiveMidiLastDecodedStatus = 0;
    gDigiLiveMidiLastDecodedData1 = 0;
    gDigiLiveMidiLastDecodedData2 = 0;
    gDigiLiveMidiLastNoteNumber = 0;
    gDigiLiveMidiLastNoteVelocity = 0;
    gDigiLiveMidiLastControlNumber = 0;
    gDigiLiveMidiLastControlValue = 0;
    gDigiLiveMidiDecodedRecentIndex = 0;
    gDigiLiveMidiDecodedRecentCount = 0;
    for (uint32_t i = 0; i < kDigiLiveMidiDecodedRecentMessageCount; ++i) {
        gDigiLiveMidiDecodedRecentMessages[i] = 0;
    }
    gDigiLiveControlMappedMessageCount = 0;
    gDigiLiveControlUnknownMessageCount = 0;
    gDigiLiveControlLastMappedKind = kDigiLiveControlKindUnknown;
    gDigiLiveControlLastMappedChannel = 0xffffffff;
    for (uint32_t i = 0; i < kDigiLiveControlChannelStripCount; ++i) {
        gDigiLiveControlChannelSelectPressed[i] = 0;
        gDigiLiveControlChannelSoloPressed[i] = 0;
        gDigiLiveControlChannelMutePressed[i] = 0;
        gDigiLiveControlChannelFaderTouched[i] = 0;
        gDigiLiveControlChannelFaderControlNumber[i] = 0;
        gDigiLiveControlChannelFaderValue[i] = 0;
        gDigiLiveControlChannelFaderUpdateCount[i] = 0;
        gDigiLiveControlMotorTestToggle[i] = 0;
        gDigiLiveControlFaderFeedbackMovesSinceSend[i] = 0;
        gDigiLiveControlFaderFeedbackLastSentValue[i] = 0;
        gDigiLiveControlFaderFeedbackLastSentValid[i] = 0;
    }
    gDigiLiveControlFaderFeedbackSentCount = 0;
    gDigiLiveControlFaderFeedbackSkippedCount = 0;
    gDigiLiveControlFaderFeedbackFlushCount = 0;
    gDigiLiveControlFaderFeedbackLastChannel = 0xffffffff;
    gDigiLiveControlFaderFeedbackLastCC = 0;
    gDigiLiveControlFaderFeedbackLastValue = 0;
    gDigiLiveControlSelect1Pressed = 0;
    gDigiLiveControlFader1Touched = 0;
    gDigiLiveControlFader1ControlNumber = 0;
    gDigiLiveControlFader1Value = 0;
    gDigiLiveControlFader1UpdateCount = 0;
    gDigiLiveControlTransportRTZPressed = 0;
    gDigiLiveControlTransportRewindPressed = 0;
    gDigiLiveControlTransportFastForwardPressed = 0;
    gDigiLiveControlStopPressed = 0;
    gDigiLiveControlPlayPressed = 0;
    gDigiLiveControlTransportRecordPressed = 0;
    gDigiLiveControlArrowLeftPressed = 0;
    gDigiLiveControlArrowRightPressed = 0;
    gDigiLiveControlArrowUpPressed = 0;
    gDigiLiveControlArrowDownPressed = 0;
    gDigiLiveControlJogWheelValue = 0;
    gDigiLiveControlJogWheelDirection = 0;
    gDigiLiveControlJogWheelStep = 0;
    gDigiLiveControlJogWheelUpdateCount = 0;
    gDigiLiveControlShuttleValue = 0;
    gDigiLiveControlShuttleUpdateCount = 0;
    for (uint32_t i = 0; i < kDigiLiveControlModeViewButtonCount; ++i) {
        gDigiLiveControlModeViewButtonPressed[i] = 0;
    }
    gDigiLiveControlModeViewLastNote = 0xffffffff;
    gDigiLiveControlModeViewLastIndex = 0xffffffff;
    gDigiLiveControlModeViewUpdateCount = 0;
    for (uint32_t i = 0; i < kDigiLiveControlAboveTransportButtonCount; ++i) {
        gDigiLiveControlAboveTransportButtonPressed[i] = 0;
    }
    gDigiLiveControlAboveTransportLastNote = 0xffffffff;
    gDigiLiveControlAboveTransportLastIndex = 0xffffffff;
    gDigiLiveControlAboveTransportUpdateCount = 0;
    for (uint32_t i = 0; i < kDigiLiveControlTransportSectionButtonCount; ++i) {
        gDigiLiveControlTransportSectionButtonPressed[i] = 0;
    }
    gDigiLiveControlTransportSectionLastGroup = 0xffffffff;
    gDigiLiveControlTransportSectionLastNote = 0xffffffff;
    gDigiLiveControlTransportSectionLastIndex = 0xffffffff;
    gDigiLiveControlTransportSectionUpdateCount = 0;
    gDigiLiveControlNavigationModeLedNote = 0xffffffff;
    for (uint32_t i = 0; i < kDigiLiveControlHardwareMonitorButtonCount; ++i) {
        gDigiLiveControlHardwareMonitorButtonPressed[i] = 0;
    }
    gDigiLiveControlHardwareMonitorLastNote = 0xffffffff;
    gDigiLiveControlHardwareMonitorLastIndex = 0xffffffff;
    gDigiLiveControlHardwareMonitorUpdateCount = 0;
    gDigiLiveControlDisplayModePressed = 0;
    gDigiLiveControlDisplayModeUpdateCount = 0;
    for (uint32_t i = 0; i < kDigiLiveControlEncoderAssignButtonCount; ++i) {
        gDigiLiveControlEncoderAssignButtonPressed[i] = 0;
    }
    gDigiLiveControlEncoderAssignLastNote = 0xffffffff;
    gDigiLiveControlEncoderAssignLastIndex = 0xffffffff;
    gDigiLiveControlEncoderAssignUpdateCount = 0;
    for (uint32_t i = 0; i < kDigiLiveControlRotaryEncoderCount; ++i) {
        gDigiLiveControlRotaryEncoderValue[i] = 0;
        gDigiLiveControlRotaryEncoderDirection[i] = 0;
        gDigiLiveControlRotaryEncoderStep[i] = 0;
        gDigiLiveControlRotaryEncoderUpdateCount[i] = 0;
    }
    gDigiLiveControlRotaryEncoderLastIndex = 0xffffffff;
    gDigiLiveControlMotorTestTriggerCount = 0;
    gDigiLiveControlMotorTestMessageCount = 0;
    gDigiLiveControlMotorTestSkippedCount = 0;
    gDigiLiveControlMotorTestLastChannel = 0xffffffff;
    gDigiLiveControlMotorTestLastTarget10 = 0;
    gDigiLiveControlMotorTestLastCC = 0;
    gDigiLiveControlMotorTestLastValue = 0;
    gDigiLiveMidiLoggedMessageCount = 0;
    gDigiLiveMidiEchoQueueBusy = 0;
    gDigiLiveMidiEchoReadIndex = 0;
    gDigiLiveMidiEchoWriteIndex = 0;
    gDigiLiveMidiEchoQueueCount = 0;
    gDigiLiveMidiEchoLastQueuedRawWordBE = 0;
    gDigiLiveMidiEchoLastTransmitRawWordBE = 0;
    for (uint32_t i = 0; i < kDigiLiveMidiEchoQueueSize; ++i) {
        gDigiLiveMidiEchoQueue[i] = 0;
    }
    gDigiLiveMidiFeedbackMessageCount = 0;
    gDigiLiveMidiFeedbackSkippedCount = 0;
    gDigiLiveMidiEchoAppendCount = 0;
    gDigiLiveMidiEchoDropCount = 0;
    gDigiLiveMidiEchoTransmitCount = 0;
    gDigiLiveMidiEchoBusyCount = 0;
    gDigiLiveSyncForDeviceRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveSyncForCPURet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveCompleteRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveRxDescriptorSyncForCPURet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveRxPayloadSyncForCPURet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveRxDescriptorSyncCount = 0;
    gDigiLiveRxDescriptorSyncBytes = 0;
    gDigiLiveRxPayloadSyncCount = 0;
    gDigiLiveRxPayloadSyncBytes = 0;
    gDigiLiveXmitMaskSupport = 0;
    gDigiLiveRecvMaskSupport = 0;
    gDigiLiveXmitContextSupported = 0;
    gDigiLiveRecvContextSupported = 0;
    gDigiLiveITCommandPtr = 0;
    gDigiLiveIRCommandPtr = 0;
    gDigiLiveIRContextMatch = 0;
    gDigiLiveITControlAfterRun = 0;
    gDigiLiveIRControlAfterRun = 0;
    gDigiLiveITControlAfterStop = 0;
    gDigiLiveIRControlAfterStop = 0;
    gDigiLiveITStopLoops = 0;
    gDigiLiveIRStopLoops = 0;
    gDigiLiveITMaskAfterRun = 0;
    gDigiLiveIRMaskAfterRun = 0;
    gDigiLiveITMaskAfterClear = 0;
    gDigiLiveIRMaskAfterClear = 0;
    gDigiLiveITEventAfterRun = 0;
    gDigiLiveIREventAfterRun = 0;
    gDigiLiveITCommandPtrLastRead = 0;
    gDigiLiveIRCommandPtrLastRead = 0;
    gDigiLiveITFirstPayloadResCount = 0;
    gDigiLiveITFirstPayloadStatus = 0;
    gDigiLiveITLastPayloadResCount = 0;
    gDigiLiveITLastPayloadStatus = 0;
    gDigiLiveITEventPollCount = 0;
    gDigiLiveITEventHitCount = 0;
    gDigiLiveITEventMissCount = 0;
    gDigiLiveITEventClearCount = 0;
    gDigiLiveITEventLastBeforeHarvest = 0;
    gDigiLiveITEventLastAfterClear = 0;
    gDigiLiveIREventPollCount = 0;
    gDigiLiveIREventHitCount = 0;
    gDigiLiveIREventMissCount = 0;
    gDigiLiveIREventClearCount = 0;
    gDigiLiveIREventGateSkipCount = 0;
    gDigiLiveIREventGateBypassCount = 0;
    gDigiLiveIREventConsecutiveMissCount = 0;
    gDigiLiveIREventLastBeforeHarvest = 0;
    gDigiLiveIREventLastAfterClear = 0;
    gDigiLiveRxHeader0Raw = 0;
    gDigiLiveRxHeader1Raw = 0;
    gDigiLiveRxHeader2Raw = 0;
    gDigiLiveRxHeader3Raw = 0;
    gDigiLiveRxIsoHeader = 0;
    gDigiLiveRxTimestamp = 0;
    gDigiLiveRxCycle = 0xffffffff;
    gDigiLiveRxExpectedCycle = 0xffffffff;
    gDigiLiveRxCycleDelta = 0;
    gDigiLiveRxMaxCycleDelta = 0;
    gDigiLiveRxCycleLostCount = 0;
    gDigiLiveRxCyclePacketCount = 0;
    gDigiLiveRxCIPHeader0 = 0;
    gDigiLiveRxCIPHeader1 = 0;
    gDigiLiveRxDBC = 0xffffffff;
    gDigiLiveRxExpectedDBC = 0xffffffff;
    gDigiLiveRxDBCPacketCount = 0;
    gDigiLiveRxDBCLostCount = 0;
    gDigiLiveRxDBCInitCount = 0;
    gDigiLiveRxSYT = 0xffffffff;
    gDigiLiveRxSYTNoInfoCount = 0;
    gDigiLiveRxSYTZeroCount = 0;
    for (size_t index = 0;
         index < sizeof(gDigiLiveRxDataBlockHistogram) / sizeof(gDigiLiveRxDataBlockHistogram[0]);
         ++index) {
        gDigiLiveRxDataBlockHistogram[index] = 0;
    }
    gDigiLiveRxUnexpectedDataBlockCount = 0;
    ResetAudioRingBuffer();
    ResetAudioOutputRingBuffer();
    ResetDigiLiveOutputState();
    if (gAudioRefreshQueue == nullptr) {
        IODispatchQueueName queueName = "Digi003AudioRefresh";
        gAudioRefreshQueueCreateRet =
            ReturnCodeToProperty(IODispatchQueue::Create(queueName,
                                                         kIODispatchQueueReentrant,
                                                         0,
                                                         &gAudioRefreshQueue));
    }
    if (kOHCIInterruptDispatchEnabled != 0 &&
        gAudioRefreshQueueCreateRet == ReturnCodeToProperty(kIOReturnSuccess) &&
        gPCIDevice != nullptr) {
        (void)ConfigureOHCIInterruptDispatch(driver, gPCIDevice);
    }

    gAudioBufferCreateRet =
        ReturnCodeToProperty(IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut,
                                                              kAudioInputBufferBytes,
                                                              4096,
                                                              &gAudioInputBuffer));
    if (gAudioBufferCreateRet != ReturnCodeToProperty(kIOReturnSuccess)) {
        return static_cast<kern_return_t>(gAudioBufferCreateRet);
    }

    gAudioBufferSetLengthRet = ReturnCodeToProperty(gAudioInputBuffer->SetLength(kAudioInputBufferBytes));
    if (gAudioBufferSetLengthRet != ReturnCodeToProperty(kIOReturnSuccess)) {
        return static_cast<kern_return_t>(gAudioBufferSetLengthRet);
    }

    gAudioBufferRangeRet = ReturnCodeToProperty(gAudioInputBuffer->GetAddressRange(&gAudioInputCPUAddress));
    if (gAudioBufferRangeRet != ReturnCodeToProperty(kIOReturnSuccess)) {
        return static_cast<kern_return_t>(gAudioBufferRangeRet);
    }
    ClearAudioInputBuffer();

    if (kAudioOutputStreamEnabled != 0) {
        gAudioOutputBufferCreateRet =
            ReturnCodeToProperty(IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut,
                                                                  kAudioOutputBufferBytes,
                                                                  4096,
                                                                  &gAudioOutputBuffer));
        if (gAudioOutputBufferCreateRet != ReturnCodeToProperty(kIOReturnSuccess)) {
            return static_cast<kern_return_t>(gAudioOutputBufferCreateRet);
        }

        gAudioOutputBufferSetLengthRet =
            ReturnCodeToProperty(gAudioOutputBuffer->SetLength(kAudioOutputBufferBytes));
        if (gAudioOutputBufferSetLengthRet != ReturnCodeToProperty(kIOReturnSuccess)) {
            return static_cast<kern_return_t>(gAudioOutputBufferSetLengthRet);
        }

        gAudioOutputBufferRangeRet =
            ReturnCodeToProperty(gAudioOutputBuffer->GetAddressRange(&gAudioOutputCPUAddress));
        if (gAudioOutputBufferRangeRet != ReturnCodeToProperty(kIOReturnSuccess)) {
            return static_cast<kern_return_t>(gAudioOutputBufferRangeRet);
        }
        ClearAudioOutputBuffer();
    }

    OSString * deviceUID = OSString::withCString("com.axelheckert.digi003.input");
    OSString * modelUID = OSString::withCString("Digidesign.003");
    OSString * manufacturerUID = OSString::withCString("Digidesign");
    if (deviceUID == nullptr || modelUID == nullptr || manufacturerUID == nullptr) {
        OSSafeReleaseNULL(deviceUID);
        OSSafeReleaseNULL(modelUID);
        OSSafeReleaseNULL(manufacturerUID);
        return kIOReturnNoMemory;
    }

    IOUserAudioDevice * audioDevice = OSTypeAlloc(FireWireOHCIProbeAudioDevice);
    if (audioDevice != nullptr &&
        !audioDevice->init(driver,
                           false,
                           deviceUID,
                           modelUID,
                           manufacturerUID,
                           kAudioDeviceZeroTimestampPeriod)) {
        audioDevice->release();
        audioDevice = nullptr;
    }
    deviceUID->release();
    modelUID->release();
    manufacturerUID->release();
    if (audioDevice == nullptr) {
        gAudioDeviceCreateRet = ReturnCodeToProperty(kIOReturnNoMemory);
        return kIOReturnNoMemory;
    }
    gAudioDevice.reset(audioDevice, OSNoRetain);
    gAudioDeviceCreateRet = ReturnCodeToProperty(kIOReturnSuccess);

    SetAudioDriverName(driver, "Digi 003 FireWire Driver");
    SetAudioObjectName(gAudioDevice.get(), "Digi 003 FireWire");
    gAudioDevice->SetTransportType(IOUserAudioTransportType::FireWire);
    gAudioDevice->SetCanBeDefaultInputDevice(true);
    gAudioDevice->SetCanBeDefaultOutputDevice(kAudioOutputStreamEnabled != 0);
    gAudioDevice->SetCanBeDefaultSystemOutputDevice(false);
    gAudioDevice->SetInputSafetyOffset(kAudioDeviceZeroTimestampPeriod);
    gAudioDevice->SetOutputSafetyOffset(kAudioDeviceZeroTimestampPeriod);
    gAudioDevice->SetInputLatency(kAudioDeviceZeroTimestampPeriod);
    gAudioDevice->SetOutputLatency(kAudioDeviceZeroTimestampPeriod);
    gAudioDevice->SetClockDomain(0);
    gAudioDevice->SetClockAlgorithm(IOUserAudioClockAlgorithm::Raw);
    gAudioDevice->SetClockIsStable(true);
    double sampleRates[] = {
        static_cast<double>(kDigi00xDuplexSampleRate44100),
        static_cast<double>(kDigi00xDuplexSampleRate48000),
    };
    gAudioDevice->SetAvailableSampleRates(sampleRates, 2);
    gAudioDevice->SetSampleRate(static_cast<double>(gDigi00xCurrentSampleRate));
    gAudioZeroTimestampHostTime = mach_absolute_time();
    gAudioDevice->UpdateCurrentZeroTimestamp(0, gAudioZeroTimestampHostTime);
    IOUserAudioChannelLabel inputLayout[kDigi00xDuplexIRCapturePCMChannelCount] = {
        IOUserAudioChannelLabel::Left,
        IOUserAudioChannelLabel::Right,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
    };
    gAudioDevice->SetPreferredInputChannelLayout(inputLayout, kDigi00xDuplexIRCapturePCMChannelCount);
    IOUserAudioChannelLabel outputLayout[kAudioOutputChannelCount] = {
        IOUserAudioChannelLabel::Left,
        IOUserAudioChannelLabel::Right,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
        IOUserAudioChannelLabel::Unknown,
    };
    if (kAudioOutputStreamEnabled != 0) {
        gAudioDevice->SetPreferredOutputChannelLayout(outputLayout, kAudioOutputChannelCount);
    }
    gAudioDevice->SetPreferredChannelsForStereo(1, 2);

    IOUserAudioStreamBasicDescription inputStreamFormats[2] = {};
    IOUserAudioStreamBasicDescription outputStreamFormats[2] = {};
    for (uint32_t rateIndex = 0; rateIndex < 2; ++rateIndex) {
        double rate = rateIndex == 0
            ? static_cast<double>(kDigi00xDuplexSampleRate44100)
            : static_cast<double>(kDigi00xDuplexSampleRate48000);
        inputStreamFormats[rateIndex].mSampleRate = rate;
        inputStreamFormats[rateIndex].mFormatID = IOUserAudioFormatID::LinearPCM;
        inputStreamFormats[rateIndex].mFormatFlags = static_cast<IOUserAudioFormatFlags>(
            FormatFlagIsSignedInteger | FormatFlagIsPacked);
        inputStreamFormats[rateIndex].mBytesPerPacket = kAudioInputBytesPerFrame;
        inputStreamFormats[rateIndex].mFramesPerPacket = 1;
        inputStreamFormats[rateIndex].mBytesPerFrame = kAudioInputBytesPerFrame;
        inputStreamFormats[rateIndex].mChannelsPerFrame =
            kDigi00xDuplexIRCapturePCMChannelCount;
        inputStreamFormats[rateIndex].mBitsPerChannel = kAudioInputBytesPerSample * 8;

        outputStreamFormats[rateIndex] = inputStreamFormats[rateIndex];
        outputStreamFormats[rateIndex].mBytesPerPacket = kAudioOutputBytesPerFrame;
        outputStreamFormats[rateIndex].mBytesPerFrame = kAudioOutputBytesPerFrame;
        outputStreamFormats[rateIndex].mChannelsPerFrame = kAudioOutputChannelCount;
        outputStreamFormats[rateIndex].mBitsPerChannel = kAudioOutputBytesPerSample * 8;
    }
    uint32_t currentFormatIndex =
        gDigi00xCurrentSampleRate == kDigi00xDuplexSampleRate44100 ? 0u : 1u;

    gAudioInputStream = IOUserAudioStream::Create(driver,
                                                  IOUserAudioStreamDirection::Input,
                                                  gAudioInputBuffer);
    if (!gAudioInputStream) {
        gAudioStreamCreateRet = ReturnCodeToProperty(kIOReturnNoMemory);
        return kIOReturnNoMemory;
    }
    gAudioStreamCreateRet = ReturnCodeToProperty(kIOReturnSuccess);
    SetAudioObjectName(gAudioInputStream.get(), "Digi 003 Inputs 1-8");
    gAudioInputStream->SetAvailableStreamFormats(inputStreamFormats, 2);
    gAudioInputStream->SetCurrentStreamFormat(&inputStreamFormats[currentFormatIndex]);
    gAudioInputStream->SetTerminalType(IOUserAudioStreamTerminalType::Line);
    gAudioInputStream->SetStartingChannel(1);
    gAudioInputStream->SetLatency(kAudioDeviceZeroTimestampPeriod);
    gAudioStreamActiveRet = ReturnCodeToProperty(gAudioInputStream->SetStreamIsActive(true));

    gAudioAddStreamRet = ReturnCodeToProperty(gAudioDevice->AddStream(gAudioInputStream.get()));
    if (gAudioAddStreamRet != ReturnCodeToProperty(kIOReturnSuccess)) {
        return static_cast<kern_return_t>(gAudioAddStreamRet);
    }

    if (kAudioOutputStreamEnabled != 0) {
        gAudioOutputStream = IOUserAudioStream::Create(driver,
                                                       IOUserAudioStreamDirection::Output,
                                                       gAudioOutputBuffer);
        if (!gAudioOutputStream) {
            gAudioOutputStreamCreateRet = ReturnCodeToProperty(kIOReturnNoMemory);
            return kIOReturnNoMemory;
        }
        gAudioOutputStreamCreateRet = ReturnCodeToProperty(kIOReturnSuccess);
        SetAudioObjectName(gAudioOutputStream.get(), "Digi 003 Outputs 1-8");
        gAudioOutputStream->SetAvailableStreamFormats(outputStreamFormats, 2);
        gAudioOutputStream->SetCurrentStreamFormat(&outputStreamFormats[currentFormatIndex]);
        gAudioOutputStream->SetTerminalType(IOUserAudioStreamTerminalType::Line);
        gAudioOutputStream->SetStartingChannel(1);
        gAudioOutputStream->SetLatency(kAudioDeviceZeroTimestampPeriod);
        gAudioOutputStreamActiveRet =
            ReturnCodeToProperty(gAudioOutputStream->SetStreamIsActive(true));

        gAudioOutputAddStreamRet =
            ReturnCodeToProperty(gAudioDevice->AddStream(gAudioOutputStream.get()));
        if (gAudioOutputAddStreamRet != ReturnCodeToProperty(kIOReturnSuccess)) {
            return static_cast<kern_return_t>(gAudioOutputAddStreamRet);
        }
    }

    IOOperationHandler ioOperation =
        ^kern_return_t(IOUserAudioObjectID in_device,
                       IOUserAudioIOOperation in_io_operation,
                       uint32_t in_io_buffer_frame_size,
                       uint64_t in_sample_time,
                       uint64_t in_host_time) {
            (void)in_device;
            (void)in_host_time;
            if (in_io_operation == IOUserAudioIOOperationBeginRead) {
                gAudioInputCallbackCount++;
                gAudioInputLastBufferFrameSize = in_io_buffer_frame_size;
                gAudioInputLastSampleTime = in_sample_time;
                if (gAudioRefreshWorkerRunning == 0 ||
                    gDigiLiveRunning == 0 ||
                    gDigiLiveReady == 0) {
                    RequestAudioRuntimeRestart(kAudioRuntimeRestartReasonInputCallback);
                }
                HarvestDigiLiveForAudioCallback(in_io_buffer_frame_size);
                FillAudioInputBuffer(in_io_buffer_frame_size, in_sample_time);
            } else if (in_io_operation == IOUserAudioIOOperationWriteEnd &&
                       kAudioOutputStreamEnabled != 0) {
                gAudioOutputCallbackCount++;
                gAudioOutputLastBufferFrameSize = in_io_buffer_frame_size;
                gAudioOutputLastSampleTime = in_sample_time;
                AppendAudioOutputBufferToRing(in_io_buffer_frame_size, in_sample_time);
                if (gAudioRefreshWorkerRunning == 0 ||
                    gDigiLiveRunning == 0 ||
                    gDigiLiveReady == 0) {
                    RequestAudioRuntimeRestart(kAudioRuntimeRestartReasonOutputCallback);
                }
                (void)PushAudioOutputToDigiLiveTransmit();
            }
            return kIOReturnSuccess;
        };
    gAudioIOHandlerRet = ReturnCodeToProperty(gAudioDevice->SetIOOperationHandler(ioOperation));
    if (gAudioIOHandlerRet != ReturnCodeToProperty(kIOReturnSuccess)) {
        return static_cast<kern_return_t>(gAudioIOHandlerRet);
    }

    gAudioAddObjectRet = ReturnCodeToProperty(driver->AddObject(gAudioDevice.get()));
    if (gAudioAddObjectRet == ReturnCodeToProperty(kIOReturnSuccess)) {
        gAudioRegisterIOThreadRet =
            ReturnCodeToProperty(gAudioDevice->_RegisterIOThread(gAudioDevice->GetObjectID(),
                                                                 static_cast<double>(gDigi00xCurrentSampleRate),
                                                                 kAudioDeviceZeroTimestampPeriod));
    }
    return static_cast<kern_return_t>(gAudioAddObjectRet);
}

uint16_t
CRCITU16(uint16_t crc, uint8_t data)
{
    crc ^= static_cast<uint16_t>(data) << 8;
    for (uint8_t i = 0; i < 8; ++i) {
        if ((crc & 0x8000) != 0) {
            crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
        } else {
            crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

uint16_t
ComputeConfigROMCRC(const uint32_t * hostOrderWords, size_t wordCount)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < wordCount; ++i) {
        uint32_t word = hostOrderWords[i];
        crc = CRCITU16(crc, static_cast<uint8_t>((word >> 24) & 0xff));
        crc = CRCITU16(crc, static_cast<uint8_t>((word >> 16) & 0xff));
        crc = CRCITU16(crc, static_cast<uint8_t>((word >> 8) & 0xff));
        crc = CRCITU16(crc, static_cast<uint8_t>(word & 0xff));
    }
    return crc;
}

uint32_t
BuildConfigROMBusOptions(uint32_t controllerBusOptions)
{
    uint32_t linkSpeed = controllerBusOptions & 0x7;
    uint32_t maxReceive = (controllerBusOptions >> 12) & 0xf;
    return linkSpeed |
           (kConfigROMGeneration << 4) |
           (kConfigROMMaxROM << 8) |
           (maxReceive << 12) |
           (1u << 28) |
           (1u << 29) |
           (1u << 30) |
           (1u << 31);
}

void
ReadRegisterSnapshot(IOPCIDevice * pciDevice, uint8_t memoryIndex, RegisterSnapshot * snapshots, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        snapshots[i].value = 0xffffffff;
        pciDevice->MemoryRead32(memoryIndex, snapshots[i].offset, &snapshots[i].value);
    }
}

kern_return_t
SoftwareResetOHCI(IOPCIDevice * pciDevice,
                  uint8_t memoryIndex,
                  uint32_t * before,
                  uint32_t * after,
                  uint32_t * loops)
{
    *before = 0xffffffff;
    *after = 0xffffffff;
    *loops = 0;

    pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, before);
    pciDevice->MemoryWrite32(memoryIndex, kOhciHcControlSetOffset, kOhciHcControlSoftReset);

    for (uint32_t i = 0; i < 500; ++i) {
        uint32_t value = 0xffffffff;
        pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, &value);
        *after = value;
        *loops = i + 1;
        if (value == 0xffffffff) {
            return kIOReturnNoDevice;
        }
        if ((value & kOhciHcControlSoftReset) == 0) {
            return kIOReturnSuccess;
        }
        IOSleep(1);
    }

    return kIOReturnTimeout;
}

kern_return_t
ReadPhyRegister(IOPCIDevice * pciDevice, uint8_t memoryIndex, uint8_t address, uint32_t * value)
{
    uint32_t command = (static_cast<uint32_t>(address & 0xf) << 8) | 0x00008000;
    pciDevice->MemoryWrite32(memoryIndex, kOhciPhyControlOffset, command);

    for (uint32_t i = 0; i < 1000; ++i) {
        uint32_t control = 0xffffffff;
        pciDevice->MemoryRead32(memoryIndex, kOhciPhyControlOffset, &control);
        if (control == 0xffffffff) {
            return kIOReturnNoDevice;
        }
        if ((control & kOhciPhyControlReadDone) != 0) {
            *value = (control >> 16) & 0xff;
            return kIOReturnSuccess;
        }
        IODelay(10);
    }

    return kIOReturnTimeout;
}

kern_return_t
WritePhyRegister(IOPCIDevice * pciDevice, uint8_t memoryIndex, uint8_t address, uint32_t value)
{
    uint32_t command = (static_cast<uint32_t>(address & 0xf) << 8) |
                       (value & 0xff) |
                       kOhciPhyControlWritePending;
    pciDevice->MemoryWrite32(memoryIndex,
                             kOhciPhyControlOffset,
                             command);

    for (uint32_t i = 0; i < 1000; ++i) {
        uint32_t control = 0xffffffff;
        pciDevice->MemoryRead32(memoryIndex, kOhciPhyControlOffset, &control);
        if (control == 0xffffffff) {
            return kIOReturnNoDevice;
        }
        if ((control & kOhciPhyControlWritePending) == 0) {
            return kIOReturnSuccess;
        }
        IODelay(10);
    }

    return kIOReturnTimeout;
}

kern_return_t
UpdatePhyRegister(IOPCIDevice * pciDevice,
                  uint8_t memoryIndex,
                  uint8_t address,
                  uint32_t clearBits,
                  uint32_t setBits,
                  uint32_t * before,
                  uint32_t * after)
{
    kern_return_t ret = ReadPhyRegister(pciDevice, memoryIndex, address, before);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    if (address == 5) {
        clearBits |= kPhyInterruptStatusBits;
    }

    uint32_t updated = (*before & ~clearBits) | setBits;
    ret = WritePhyRegister(pciDevice, memoryIndex, address, updated);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    return ReadPhyRegister(pciDevice, memoryIndex, address, after);
}

bool
IsSelfIDReady(uint32_t nodeID, uint32_t selfIDCount, uint32_t intEvent)
{
    bool nodeIDValid = (nodeID & 0x80000000) != 0;
    bool selfIDComplete = (intEvent & kOhciEventSelfIDComplete) != 0;
    bool selfIDHasData = ((selfIDCount & 0x000007fc) >> 2) != 0;
    return nodeIDValid || selfIDComplete || selfIDHasData;
}

void
WaitForSelfID(IOPCIDevice * pciDevice,
              uint8_t memoryIndex,
              uint32_t attempts,
              uint32_t sleepMilliseconds,
              uint32_t * loops,
              uint32_t * nodeID,
              uint32_t * selfIDCount,
              uint32_t * intEvent)
{
    for (uint32_t i = 0; i < attempts; ++i) {
        pciDevice->MemoryRead32(memoryIndex, kOhciNodeIdOffset, nodeID);
        pciDevice->MemoryRead32(memoryIndex, kOhciSelfIdCountOffset, selfIDCount);
        pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, intEvent);
        *loops = i + 1;

        if (IsSelfIDReady(*nodeID, *selfIDCount, *intEvent)) {
            break;
        }
        IOSleep(sleepMilliseconds);
    }
}

void
AcknowledgeBusResetLikeLinux(IOPCIDevice * pciDevice,
                             uint8_t memoryIndex,
                             uint32_t attempts,
                             uint32_t sleepMilliseconds,
                             uint32_t * attempted,
                             uint32_t * loops,
                             uint32_t * eventBefore,
                             uint32_t * eventAfter,
                             uint32_t * maskAfter)
{
    *attempted = 1;
    for (uint32_t i = 0; i < attempts; ++i) {
        pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, eventBefore);
        *loops = i + 1;
        if ((*eventBefore & kOhciEventBusReset) != 0) {
            uint32_t clearNow = *eventBefore & ~kOhciEventBusReset;
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, clearNow);
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskClearOffset, kOhciEventBusReset);
            pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, eventAfter);
            pciDevice->MemoryRead32(memoryIndex, kOhciIntMaskSetOffset, maskAfter);
            return;
        }
        IOSleep(sleepMilliseconds);
    }

    pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, eventAfter);
    pciDevice->MemoryRead32(memoryIndex, kOhciIntMaskSetOffset, maskAfter);
}

kern_return_t
ReadPagedPhyRegister(IOPCIDevice * pciDevice,
                     uint8_t memoryIndex,
                     uint8_t page,
                     uint8_t address,
                     uint32_t * value,
                     uint32_t * pageSelectBefore,
                     uint32_t * pageSelectAfter)
{
    kern_return_t ret = UpdatePhyRegister(pciDevice,
                                          memoryIndex,
                                          7,
                                          kPhyPageSelect,
                                          static_cast<uint32_t>(page) << 5,
                                          pageSelectBefore,
                                          pageSelectAfter);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    return ReadPhyRegister(pciDevice, memoryIndex, address, value);
}

kern_return_t
Configure1394AEnhancements(IOPCIDevice * pciDevice,
                           uint8_t memoryIndex,
                           uint32_t * hcControlBefore,
                           uint32_t * hcControlAfter,
                           uint32_t * phyReg2,
                           uint32_t * page1Reg8,
                           uint32_t * pageSelectBefore,
                           uint32_t * pageSelectAfter,
                           uint32_t * supported,
                           uint32_t * phyReg5Before,
                           uint32_t * phyReg5After,
                           kern_return_t * phyReg2ReadRet,
                           kern_return_t * page1ReadRet,
                           kern_return_t * phyReg5UpdateRet)
{
    *supported = 0;
    pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, hcControlBefore);
    if ((*hcControlBefore & kOhciHcControlProgramPhyEnable) == 0) {
        *hcControlAfter = *hcControlBefore;
        return kIOReturnSuccess;
    }

    *phyReg2ReadRet = ReadPhyRegister(pciDevice, memoryIndex, 2, phyReg2);
    if (*phyReg2ReadRet != kIOReturnSuccess) {
        return *phyReg2ReadRet;
    }

    bool enable1394A = false;
    if ((*phyReg2 & kPhyExtendedRegisters) == kPhyExtendedRegisters) {
        *page1ReadRet = ReadPagedPhyRegister(pciDevice,
                                             memoryIndex,
                                             kPhyPage1394A,
                                             8,
                                             page1Reg8,
                                             pageSelectBefore,
                                             pageSelectAfter);
        if (*page1ReadRet != kIOReturnSuccess) {
            return *page1ReadRet;
        }
        enable1394A = *page1Reg8 >= 1;
    } else {
        *page1ReadRet = kIOReturnUnsupported;
    }

    *supported = enable1394A ? 1 : 0;
    uint32_t clearBits = enable1394A ? 0 : (kPhyEnableAccelerated | kPhyEnableMulti);
    uint32_t setBits = enable1394A ? (kPhyEnableAccelerated | kPhyEnableMulti) : 0;
    *phyReg5UpdateRet = UpdatePhyRegister(pciDevice,
                                          memoryIndex,
                                          5,
                                          clearBits,
                                          setBits,
                                          phyReg5Before,
                                          phyReg5After);
    if (*phyReg5UpdateRet != kIOReturnSuccess) {
        return *phyReg5UpdateRet;
    }

    pciDevice->MemoryWrite32(memoryIndex,
                             enable1394A ? kOhciHcControlSetOffset : kOhciHcControlClearOffset,
                             kOhciHcControlAPhyEnhanceEnable);
    pciDevice->MemoryWrite32(memoryIndex,
                             kOhciHcControlClearOffset,
                             kOhciHcControlProgramPhyEnable);
    pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, hcControlAfter);
    return kIOReturnSuccess;
}

kern_return_t
ReadPhyPortStatus(IOPCIDevice * pciDevice,
                  uint8_t memoryIndex,
                  uint8_t port,
                  uint32_t * rawStatus,
                  uint32_t * decodedStatus)
{
    kern_return_t ret = WritePhyRegister(pciDevice, memoryIndex, 7, port);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    ret = ReadPhyRegister(pciDevice, memoryIndex, 8, rawStatus);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    uint32_t lowNibble = *rawStatus & 0x0f;
    switch (lowNibble) {
        case 0x06:
            *decodedStatus = 1; // connected to parent
            break;
        case 0x0e:
            *decodedStatus = 2; // connected to child
            break;
        default:
            *decodedStatus = 0; // not connected
            break;
    }
    return kIOReturnSuccess;
}

void
ReleaseDMABuffer(DMABuffer * buffer)
{
    if (buffer->cpuMap != nullptr) {
        buffer->cpuMap->release();
        buffer->cpuMap = nullptr;
    }
    if (buffer->command != nullptr) {
        if (buffer->completed == 0) {
            buffer->completeRet = buffer->command->CompleteDMA(kIODMACommandCompleteDMANoOptions);
            buffer->completed = 1;
        }
        buffer->command->release();
    }
    if (buffer->memory != nullptr) {
        buffer->memory->release();
    }
    *buffer = {};
}

kern_return_t
CompleteDMABufferMapping(DMABuffer * buffer)
{
    if (buffer->command == nullptr) {
        return kIOReturnNotReady;
    }
    if (buffer->completed != 0) {
        return buffer->completeRet;
    }

    buffer->completeRet = buffer->command->CompleteDMA(kIODMACommandCompleteDMANoOptions);
    buffer->completed = 1;
    return buffer->completeRet;
}

kern_return_t
SyncDMABufferForDevice(DMABuffer * buffer, uint64_t size)
{
    if (buffer->command == nullptr || buffer->memory == nullptr || buffer->completed != 0) {
        return kIOReturnNotReady;
    }
    if (buffer->cacheInhibitMapping != 0) {
        __sync_synchronize();
        buffer->syncForDeviceRet = kIOReturnSuccess;
        return buffer->syncForDeviceRet;
    }

    buffer->syncForDeviceRet = buffer->command->PerformOperation(kIODMACommandPerformOperationOptionWrite,
                                                                 0,
                                                                 size,
                                                                 0,
                                                                 buffer->memory);
    return buffer->syncForDeviceRet;
}

kern_return_t
SyncDMABufferForDeviceRange(DMABuffer * buffer, uint64_t offset, uint64_t size)
{
    if (buffer->command == nullptr || buffer->memory == nullptr || buffer->completed != 0) {
        return kIOReturnNotReady;
    }
    if (offset > buffer->cpuRange.length || size > buffer->cpuRange.length - offset) {
        return kIOReturnBadArgument;
    }
    if (buffer->cacheInhibitMapping != 0) {
        __sync_synchronize();
        buffer->syncForDeviceRet = kIOReturnSuccess;
        return buffer->syncForDeviceRet;
    }

    buffer->syncForDeviceRet = buffer->command->PerformOperation(kIODMACommandPerformOperationOptionWrite,
                                                                 offset,
                                                                 size,
                                                                 0,
                                                                 buffer->memory);
    return buffer->syncForDeviceRet;
}

kern_return_t
SyncDMABufferForCPU(DMABuffer * buffer, uint64_t size)
{
    if (buffer->command == nullptr || buffer->memory == nullptr || buffer->completed != 0) {
        return kIOReturnNotReady;
    }
    if (buffer->cacheInhibitMapping != 0) {
        __sync_synchronize();
        buffer->syncForCPURet = kIOReturnSuccess;
        return buffer->syncForCPURet;
    }

    buffer->syncForCPURet = buffer->command->PerformOperation(kIODMACommandPerformOperationOptionRead,
                                                             0,
                                                             size,
                                                             0,
                                                             buffer->memory);
    return buffer->syncForCPURet;
}

kern_return_t
SyncDMABufferForCPURange(DMABuffer * buffer, uint64_t offset, uint64_t size)
{
    if (buffer->command == nullptr || buffer->memory == nullptr || buffer->completed != 0) {
        return kIOReturnNotReady;
    }
    if (offset > buffer->cpuRange.length || size > buffer->cpuRange.length - offset) {
        return kIOReturnBadArgument;
    }
    if (buffer->cacheInhibitMapping != 0) {
        __sync_synchronize();
        buffer->syncForCPURet = kIOReturnSuccess;
        return buffer->syncForCPURet;
    }

    buffer->syncForCPURet = buffer->command->PerformOperation(kIODMACommandPerformOperationOptionRead,
                                                              offset,
                                                              size,
                                                              0,
                                                              buffer->memory);
    return buffer->syncForCPURet;
}

void
ZeroCPUBuffer(DMABuffer * buffer, uint64_t size)
{
    if (buffer->rangeRet != kIOReturnSuccess || buffer->cpuRange.address == 0) {
        return;
    }

    volatile uint32_t * words = reinterpret_cast<volatile uint32_t *>(buffer->cpuRange.address);
    for (uint64_t i = 0; i < (size / sizeof(uint32_t)); ++i) {
        words[i] = 0;
    }
}

kern_return_t
CreateDMABufferWithMaxAddressBits(IOPCIDevice * pciDevice,
                                  uint64_t size,
                                  uint32_t maxAddressBits,
                                  bool cacheInhibitMapping,
                                  DMABuffer * buffer)
{
    ReleaseDMABuffer(buffer);
    buffer->maxAddressBits = maxAddressBits;
    buffer->cacheInhibitMapping = cacheInhibitMapping ? 1 : 0;

    buffer->createRet = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, size, 4096, &buffer->memory);
    if (buffer->createRet != kIOReturnSuccess) {
        buffer->result = buffer->createRet;
        return buffer->createRet;
    }

    buffer->setLengthRet = buffer->memory->SetLength(size);
    if (buffer->setLengthRet != kIOReturnSuccess) {
        buffer->result = buffer->setLengthRet;
        return buffer->setLengthRet;
    }

    if (cacheInhibitMapping) {
        buffer->mappingRet = buffer->memory->CreateMapping(kIOMemoryMapCacheModeInhibit,
                                                           0,
                                                           0,
                                                           size,
                                                           0,
                                                           &buffer->cpuMap);
        if (buffer->mappingRet != kIOReturnSuccess || buffer->cpuMap == nullptr) {
            buffer->rangeRet = buffer->mappingRet;
            buffer->result =
                buffer->mappingRet == kIOReturnSuccess ? kIOReturnNoMemory : buffer->mappingRet;
            return buffer->result;
        }
        buffer->cpuRange.address = buffer->cpuMap->GetAddress();
        buffer->cpuRange.length = buffer->cpuMap->GetLength();
        if (buffer->cpuRange.address == 0 || buffer->cpuRange.length < size) {
            buffer->rangeRet = kIOReturnNoMemory;
            buffer->result = buffer->rangeRet;
            return buffer->rangeRet;
        }
        buffer->rangeRet = kIOReturnSuccess;
    } else {
        buffer->mappingRet = kIOReturnNotReady;
        buffer->rangeRet = buffer->memory->GetAddressRange(&buffer->cpuRange);
        if (buffer->rangeRet != kIOReturnSuccess) {
            buffer->result = buffer->rangeRet;
            return buffer->rangeRet;
        }
    }

    ZeroCPUBuffer(buffer, size);

    IODMACommandSpecification specification = {};
    specification.options = kIODMACommandSpecificationNoOptions;
    specification.maxAddressBits = maxAddressBits;

    buffer->dmaCreateRet = IODMACommand::Create(pciDevice,
                                                kIODMACommandCreateNoOptions,
                                                &specification,
                                                &buffer->command);
    if (buffer->dmaCreateRet != kIOReturnSuccess) {
        buffer->result = buffer->dmaCreateRet;
        return buffer->dmaCreateRet;
    }

    IOAddressSegment segments[32] = {};
    buffer->segmentCount = 32;
    buffer->prepareRet = buffer->command->PrepareForDMA(kIODMACommandPrepareForDMANoOptions,
                                                        buffer->memory,
                                                        0,
                                                        0,
                                                        &buffer->flags,
                                                        &buffer->segmentCount,
                                                        segments);
    if (buffer->prepareRet != kIOReturnSuccess) {
        buffer->result = buffer->prepareRet;
        return buffer->prepareRet;
    }

    if (buffer->segmentCount > 0) {
        buffer->dmaSegment = segments[0];
    }

    buffer->completeRet = kIOReturnNotReady;
    buffer->syncForDeviceRet = kIOReturnNotReady;
    buffer->syncForCPURet = kIOReturnNotReady;
    buffer->completed = 0;
    buffer->result = kIOReturnSuccess;
    return kIOReturnSuccess;
}

kern_return_t
CreateDMABuffer(IOPCIDevice * pciDevice, uint64_t size, DMABuffer * buffer)
{
    kern_return_t lowRet = CreateDMABufferWithMaxAddressBits(pciDevice, size, 31, false, buffer);
    uint32_t lowSegmentCount = buffer->segmentCount;
    uint64_t lowDMAAddress = buffer->dmaSegment.address;
    uint64_t lowDMALength = buffer->dmaSegment.length;
    if (lowRet == kIOReturnSuccess && lowDMAAddress <= 0x7fffffffull) {
        buffer->lowAddressAttempted = 1;
        buffer->lowAddressResult = lowRet;
        buffer->lowAddressSegmentCount = lowSegmentCount;
        buffer->lowAddressDMAAddress = lowDMAAddress;
        buffer->lowAddressDMALength = lowDMALength;
        return lowRet;
    }

    kern_return_t ret = CreateDMABufferWithMaxAddressBits(pciDevice, size, 32, false, buffer);
    buffer->lowAddressAttempted = 1;
    buffer->lowAddressResult = lowRet;
    buffer->lowAddressSegmentCount = lowSegmentCount;
    buffer->lowAddressDMAAddress = lowDMAAddress;
    buffer->lowAddressDMALength = lowDMALength;
    return ret;
}

kern_return_t
CreateUncachedDMABuffer(IOPCIDevice * pciDevice, uint64_t size, DMABuffer * buffer)
{
    kern_return_t lowRet = CreateDMABufferWithMaxAddressBits(pciDevice, size, 31, true, buffer);
    uint32_t lowSegmentCount = buffer->segmentCount;
    uint64_t lowDMAAddress = buffer->dmaSegment.address;
    uint64_t lowDMALength = buffer->dmaSegment.length;
    if (lowRet == kIOReturnSuccess && lowDMAAddress <= 0x7fffffffull) {
        buffer->lowAddressAttempted = 1;
        buffer->lowAddressResult = lowRet;
        buffer->lowAddressSegmentCount = lowSegmentCount;
        buffer->lowAddressDMAAddress = lowDMAAddress;
        buffer->lowAddressDMALength = lowDMALength;
        return lowRet;
    }

    kern_return_t ret = CreateDMABufferWithMaxAddressBits(pciDevice, size, 32, true, buffer);
    buffer->lowAddressAttempted = 1;
    buffer->lowAddressResult = lowRet;
    buffer->lowAddressSegmentCount = lowSegmentCount;
    buffer->lowAddressDMAAddress = lowDMAAddress;
    buffer->lowAddressDMALength = lowDMALength;
    return ret;
}

uint32_t
WriteConfigROM(DMABuffer * buffer,
               uint32_t controllerBusOptions,
               uint32_t guidHi,
               uint32_t guidLo,
               uint32_t * generatedBusOptions,
               uint32_t * rootHeader)
{
    if (buffer->rangeRet != kIOReturnSuccess || buffer->cpuRange.address == 0) {
        return 0;
    }

    *generatedBusOptions = BuildConfigROMBusOptions(controllerBusOptions);
    uint32_t busInfoWords[] = {
        0x31333934,
        *generatedBusOptions,
        guidHi,
        guidLo,
    };
    uint16_t busInfoCRC = ComputeConfigROMCRC(busInfoWords, 4);
    uint32_t header = (kConfigROMBusInfoLength << 24) | (kConfigROMCRCLength << 16) | busInfoCRC;

    uint32_t rootWords[] = {
        kConfigROMNodeCapabilities,
    };
    uint16_t rootCRC = ComputeConfigROMCRC(rootWords, 1);
    *rootHeader = (1u << 16) | rootCRC;

    volatile uint32_t * words = reinterpret_cast<volatile uint32_t *>(buffer->cpuRange.address);
    words[0] = ToBigEndian32(header);
    words[1] = ToBigEndian32(0x31333934);
    words[2] = ToBigEndian32(*generatedBusOptions);
    words[3] = ToBigEndian32(guidHi);
    words[4] = ToBigEndian32(guidLo);
    words[5] = ToBigEndian32(*rootHeader);
    words[6] = ToBigEndian32(kConfigROMNodeCapabilities);

    return header;
}

void
ReadSelfIDWords(DMABuffer * buffer, uint32_t * words, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        words[i] = 0xffffffff;
    }
    if (buffer->rangeRet != kIOReturnSuccess || buffer->cpuRange.address == 0) {
        return;
    }

    volatile uint32_t * source = reinterpret_cast<volatile uint32_t *>(buffer->cpuRange.address);
    for (size_t i = 0; i < count; ++i) {
        words[i] = source[i];
    }
}

void
AddDMADiagnostics(OSDictionary * diagnostics, const char * prefix, DMABuffer * buffer)
{
    if (prefix[0] == 'S') {
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferResult", ReturnCodeToProperty(buffer->result), 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferCreateRet", ReturnCodeToProperty(buffer->createRet), 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferSetLengthRet", ReturnCodeToProperty(buffer->setLengthRet), 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferRangeRet", ReturnCodeToProperty(buffer->rangeRet), 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferDMACreateRet", ReturnCodeToProperty(buffer->dmaCreateRet), 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferPrepareRet", ReturnCodeToProperty(buffer->prepareRet), 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferSegmentCount", buffer->segmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferDMAAddress", buffer->dmaSegment.address, 64);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferDMALength", buffer->dmaSegment.length, 64);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferCPUAddress", buffer->cpuRange.address, 64);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferCPULength", buffer->cpuRange.length, 64);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferDMAFlags", buffer->flags, 64);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferMaxAddressBits", buffer->maxAddressBits, 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferLowAddressAttempted", buffer->lowAddressAttempted, 8);
        AddNumberProperty(diagnostics,
                          "ProbeSelfIDBufferLowAddressResult",
                          ReturnCodeToProperty(buffer->lowAddressResult),
                          32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferLowAddressSegmentCount", buffer->lowAddressSegmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferLowAddressDMAAddress", buffer->lowAddressDMAAddress, 64);
        AddNumberProperty(diagnostics, "ProbeSelfIDBufferLowAddressDMALength", buffer->lowAddressDMALength, 64);
    } else {
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferResult", ReturnCodeToProperty(buffer->result), 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferCreateRet", ReturnCodeToProperty(buffer->createRet), 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferSetLengthRet", ReturnCodeToProperty(buffer->setLengthRet), 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferRangeRet", ReturnCodeToProperty(buffer->rangeRet), 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferDMACreateRet", ReturnCodeToProperty(buffer->dmaCreateRet), 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferPrepareRet", ReturnCodeToProperty(buffer->prepareRet), 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferSegmentCount", buffer->segmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferDMAAddress", buffer->dmaSegment.address, 64);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferDMALength", buffer->dmaSegment.length, 64);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferCPUAddress", buffer->cpuRange.address, 64);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferCPULength", buffer->cpuRange.length, 64);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferDMAFlags", buffer->flags, 64);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferMaxAddressBits", buffer->maxAddressBits, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferLowAddressAttempted", buffer->lowAddressAttempted, 8);
        AddNumberProperty(diagnostics,
                          "ProbeConfigROMBufferLowAddressResult",
                          ReturnCodeToProperty(buffer->lowAddressResult),
                          32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferLowAddressSegmentCount", buffer->lowAddressSegmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferLowAddressDMAAddress", buffer->lowAddressDMAAddress, 64);
        AddNumberProperty(diagnostics, "ProbeConfigROMBufferLowAddressDMALength", buffer->lowAddressDMALength, 64);
    }
}

void
AddIsoTestDiagnostics(OSDictionary * diagnostics, const IsoTestDiagnostics * isoTest, const DMABuffer * buffer)
{
    AddNumberProperty(diagnostics, "ProbeIsoTestEnabled", isoTest->enabled, 32);
    AddNumberProperty(diagnostics, "ProbeIsoTestAttempted", isoTest->attempted, 32);
    AddNumberProperty(diagnostics, "ProbeIsoTestReady", isoTest->ready, 32);
    AddNumberProperty(diagnostics, "ProbeIsoTestContextIndex", isoTest->contextIndex, 32);
    AddNumberProperty(diagnostics, "ProbeIsoTestChannel", isoTest->channel, 32);
    AddNumberProperty(diagnostics, "ProbeIsoXmitMaskSupport", isoTest->xmitMaskSupport, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRecvMaskSupport", isoTest->recvMaskSupport, 32);
    AddNumberProperty(diagnostics, "ProbeIsoXmitContextSupported", isoTest->xmitContextSupported, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRecvContextSupported", isoTest->recvContextSupported, 32);
    AddNumberProperty(diagnostics, "ProbeIsoXmitEventBefore", isoTest->xmitEventBefore, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRecvEventBefore", isoTest->recvEventBefore, 32);
    AddNumberProperty(diagnostics, "ProbeIsoXmitEventAfter", isoTest->xmitEventAfter, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRecvEventAfter", isoTest->recvEventAfter, 32);
    AddNumberProperty(diagnostics, "ProbeIsoXmitMaskAfterClear", isoTest->xmitMaskAfterClear, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRecvMaskAfterClear", isoTest->recvMaskAfterClear, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITControlBefore", isoTest->itControlBefore, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITControlAfterStop", isoTest->itControlAfterStop, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITControlAfterCommandPtr", isoTest->itControlAfterCommandPtr, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITStopLoops", isoTest->itStopLoops, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITCommandPtr", isoTest->itCommandPtr, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITCommandPtrReadBack", isoTest->itCommandPtrReadBack, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRControlBefore", isoTest->irControlBefore, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRControlAfterStop", isoTest->irControlAfterStop, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRControlAfterCommandPtr", isoTest->irControlAfterCommandPtr, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRStopLoops", isoTest->irStopLoops, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRCommandPtr", isoTest->irCommandPtr, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRCommandPtrReadBack", isoTest->irCommandPtrReadBack, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRContextMatch", isoTest->irContextMatch, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRContextMatchReadBack", isoTest->irContextMatchReadBack, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRunReceiveEnabled", isoTest->runReceiveEnabled, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRunReceiveAttempted", isoTest->runReceiveAttempted, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRunReceiveMilliseconds", isoTest->runReceiveMilliseconds, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRRunControl", isoTest->irRunControl, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRControlAfterRun", isoTest->irControlAfterRun, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRControlAfterWait", isoTest->irControlAfterWait, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRControlAfterFinalStop", isoTest->irControlAfterFinalStop, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRRunStopLoops", isoTest->irRunStopLoops, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIREventAfterRun", isoTest->irEventAfterRun, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRMaskAfterRun", isoTest->irMaskAfterRun, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRMaskAfterFinalClear", isoTest->irMaskAfterFinalClear, 32);
    AddNumberProperty(diagnostics, "ProbeIsoSyncForDeviceRet", isoTest->syncForDeviceRet, 32);
    AddNumberProperty(diagnostics, "ProbeIsoSyncForCPURet", isoTest->syncForCPURet, 32);
    AddNumberProperty(diagnostics, "ProbeIsoCompleteRet", isoTest->completeRet, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITDescriptorControl", isoTest->itDescriptorControl, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITDescriptorReqCount", isoTest->itDescriptorReqCount, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITDescriptorDataAddress", isoTest->itDescriptorDataAddress, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITDescriptorBranchAddress", isoTest->itDescriptorBranchAddress, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITDescriptorResCount", isoTest->itDescriptorResCount, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITDescriptorStatus", isoTest->itDescriptorStatus, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRDescriptorControl", isoTest->irDescriptorControl, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRDescriptorReqCount", isoTest->irDescriptorReqCount, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRDescriptorDataAddress", isoTest->irDescriptorDataAddress, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRDescriptorBranchAddress", isoTest->irDescriptorBranchAddress, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRDescriptorResCount", isoTest->irDescriptorResCount, 32);
    AddNumberProperty(diagnostics, "ProbeIsoIRDescriptorStatus", isoTest->irDescriptorStatus, 32);
    AddNumberProperty(diagnostics,
                      "ProbeIsoIRDescriptorResCountAfterRun",
                      isoTest->irDescriptorResCountAfterRun,
                      32);
    AddNumberProperty(diagnostics,
                      "ProbeIsoIRDescriptorStatusAfterRun",
                      isoTest->irDescriptorStatusAfterRun,
                      32);
    AddNumberProperty(diagnostics, "ProbeIsoRunTransmitEnabled", isoTest->runTransmitEnabled, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRunTransmitAttempted", isoTest->runTransmitAttempted, 32);
    AddNumberProperty(diagnostics, "ProbeIsoRunTransmitMilliseconds", isoTest->runTransmitMilliseconds, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITRunControl", isoTest->itRunControl, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITControlAfterRun", isoTest->itControlAfterRun, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITControlAfterWait", isoTest->itControlAfterWait, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITControlAfterFinalStop", isoTest->itControlAfterFinalStop, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITRunStopLoops", isoTest->itRunStopLoops, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITEventAfterRun", isoTest->itEventAfterRun, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITMaskAfterRun", isoTest->itMaskAfterRun, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITMaskAfterFinalClear", isoTest->itMaskAfterFinalClear, 32);
    AddNumberProperty(diagnostics,
                      "ProbeIsoITDescriptorResCountAfterRun",
                      isoTest->itDescriptorResCountAfterRun,
                      32);
    AddNumberProperty(diagnostics,
                      "ProbeIsoITDescriptorStatusAfterRun",
                      isoTest->itDescriptorStatusAfterRun,
                      32);
    AddNumberProperty(diagnostics, "ProbeIsoITImmediateHeader0", isoTest->itImmediateHeader0, 32);
    AddNumberProperty(diagnostics, "ProbeIsoITImmediateHeader1", isoTest->itImmediateHeader1, 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferResult", ReturnCodeToProperty(buffer->result), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferCreateRet", ReturnCodeToProperty(buffer->createRet), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferSetLengthRet", ReturnCodeToProperty(buffer->setLengthRet), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferRangeRet", ReturnCodeToProperty(buffer->rangeRet), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferDMACreateRet", ReturnCodeToProperty(buffer->dmaCreateRet), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferPrepareRet", ReturnCodeToProperty(buffer->prepareRet), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferSegmentCount", buffer->segmentCount, 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferDMAAddress", buffer->dmaSegment.address, 64);
    AddNumberProperty(diagnostics, "ProbeIsoBufferDMALength", buffer->dmaSegment.length, 64);
    AddNumberProperty(diagnostics, "ProbeIsoBufferCPUAddress", buffer->cpuRange.address, 64);
    AddNumberProperty(diagnostics, "ProbeIsoBufferCPULength", buffer->cpuRange.length, 64);
    AddNumberProperty(diagnostics, "ProbeIsoBufferDMAFlags", buffer->flags, 64);
    AddNumberProperty(diagnostics, "ProbeIsoBufferMaxAddressBits", buffer->maxAddressBits, 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferLowAddressAttempted", buffer->lowAddressAttempted, 8);
    AddNumberProperty(diagnostics, "ProbeIsoBufferLowAddressResult", ReturnCodeToProperty(buffer->lowAddressResult), 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferLowAddressSegmentCount", buffer->lowAddressSegmentCount, 32);
    AddNumberProperty(diagnostics, "ProbeIsoBufferLowAddressDMAAddress", buffer->lowAddressDMAAddress, 64);
    AddNumberProperty(diagnostics, "ProbeIsoBufferLowAddressDMALength", buffer->lowAddressDMALength, 64);
}

void
AddDigiDuplexDiagnostics(OSDictionary * properties,
                         const DigiDuplexDiagnostics * diagnostics,
                         const DMABuffer * buffer)
{
    AddNumberProperty(properties, "ProbeDigiDuplexEnabled", diagnostics->enabled, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexAttempted", diagnostics->attempted, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexDestinationID", diagnostics->destinationID, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexDeviceTransmitChannel", diagnostics->deviceTransmitChannel, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexDeviceReceiveChannel", diagnostics->deviceReceiveChannel, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexSessionChannelsBusValue", diagnostics->sessionChannelsBusValue, 32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexInitialStreamingStateBusValue",
                      diagnostics->initialStreamingStateBusValue,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexBeginSetFirstBusValue", diagnostics->beginSetFirstBusValue, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexBeginSetSecondBusValue", diagnostics->beginSetSecondBusValue, 32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexFinalStreamingStateBusValue",
                      diagnostics->finalStreamingStateBusValue,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexFinalIsocChannelsBusValue",
                      diagnostics->finalIsocChannelsBusValue,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexStepCount", diagnostics->stepCount, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexCompletedSteps", diagnostics->completedSteps, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexSuccess", diagnostics->success, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexFinishAttempted", diagnostics->finishAttempted, 32);

    const char * opNames[kDigi00xDuplexStepCount] = {
        "ProbeDigiDuplexStep0Op",
        "ProbeDigiDuplexStep1Op",
        "ProbeDigiDuplexStep2Op",
        "ProbeDigiDuplexStep3Op",
        "ProbeDigiDuplexStep4Op",
        "ProbeDigiDuplexStep5Op",
        "ProbeDigiDuplexStep6Op",
        "ProbeDigiDuplexStep7Op",
        "ProbeDigiDuplexStep8Op",
    };
    const char * offsetNames[kDigi00xDuplexStepCount] = {
        "ProbeDigiDuplexStep0Offset",
        "ProbeDigiDuplexStep1Offset",
        "ProbeDigiDuplexStep2Offset",
        "ProbeDigiDuplexStep3Offset",
        "ProbeDigiDuplexStep4Offset",
        "ProbeDigiDuplexStep5Offset",
        "ProbeDigiDuplexStep6Offset",
        "ProbeDigiDuplexStep7Offset",
        "ProbeDigiDuplexStep8Offset",
    };
    const char * busValueNames[kDigi00xDuplexStepCount] = {
        "ProbeDigiDuplexStep0BusValue",
        "ProbeDigiDuplexStep1BusValue",
        "ProbeDigiDuplexStep2BusValue",
        "ProbeDigiDuplexStep3BusValue",
        "ProbeDigiDuplexStep4BusValue",
        "ProbeDigiDuplexStep5BusValue",
        "ProbeDigiDuplexStep6BusValue",
        "ProbeDigiDuplexStep7BusValue",
        "ProbeDigiDuplexStep8BusValue",
    };
    const char * successNames[kDigi00xDuplexStepCount] = {
        "ProbeDigiDuplexStep0Success",
        "ProbeDigiDuplexStep1Success",
        "ProbeDigiDuplexStep2Success",
        "ProbeDigiDuplexStep3Success",
        "ProbeDigiDuplexStep4Success",
        "ProbeDigiDuplexStep5Success",
        "ProbeDigiDuplexStep6Success",
        "ProbeDigiDuplexStep7Success",
        "ProbeDigiDuplexStep8Success",
    };
    const char * rcodeNames[kDigi00xDuplexStepCount] = {
        "ProbeDigiDuplexStep0ResponseRCode",
        "ProbeDigiDuplexStep1ResponseRCode",
        "ProbeDigiDuplexStep2ResponseRCode",
        "ProbeDigiDuplexStep3ResponseRCode",
        "ProbeDigiDuplexStep4ResponseRCode",
        "ProbeDigiDuplexStep5ResponseRCode",
        "ProbeDigiDuplexStep6ResponseRCode",
        "ProbeDigiDuplexStep7ResponseRCode",
        "ProbeDigiDuplexStep8ResponseRCode",
    };
    const char * dataNames[kDigi00xDuplexStepCount] = {
        "ProbeDigiDuplexStep0ResponseData",
        "ProbeDigiDuplexStep1ResponseData",
        "ProbeDigiDuplexStep2ResponseData",
        "ProbeDigiDuplexStep3ResponseData",
        "ProbeDigiDuplexStep4ResponseData",
        "ProbeDigiDuplexStep5ResponseData",
        "ProbeDigiDuplexStep6ResponseData",
        "ProbeDigiDuplexStep7ResponseData",
        "ProbeDigiDuplexStep8ResponseData",
    };
    for (size_t i = 0; i < kDigi00xDuplexStepCount; ++i) {
        AddNumberProperty(properties, opNames[i], diagnostics->stepOp[i], 32);
        AddNumberProperty(properties, offsetNames[i], diagnostics->stepOffsetLo[i], 32);
        AddNumberProperty(properties, busValueNames[i], diagnostics->stepBusValue[i], 32);
        AddNumberProperty(properties, successNames[i], diagnostics->stepSuccess[i], 32);
        AddNumberProperty(properties, rcodeNames[i], diagnostics->stepResponseRCode[i], 32);
        AddNumberProperty(properties, dataNames[i], diagnostics->stepResponseData[i], 32);
    }

    AddNumberProperty(properties, "ProbeDigiDuplexIsoAttempted", diagnostics->isoAttempted, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIsoReady", diagnostics->isoReady, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexXmitMaskSupport", diagnostics->xmitMaskSupport, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexRecvMaskSupport", diagnostics->recvMaskSupport, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexXmitContextSupported", diagnostics->xmitContextSupported, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexRecvContextSupported", diagnostics->recvContextSupported, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITCommandPtr", diagnostics->itCommandPtr, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRCommandPtr", diagnostics->irCommandPtr, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRContextMatch", diagnostics->irContextMatch, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITImmediateHeader0", diagnostics->itImmediateHeader0, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITImmediateHeader1", diagnostics->itImmediateHeader1, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITPacketCount", diagnostics->itPacketCount, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITDataBlockQuadlets", diagnostics->itDataBlockQuadlets, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITTotalDataBlocks", diagnostics->itTotalDataBlocks, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITFirstDataBlocks", diagnostics->itFirstDataBlocks, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITLastDataBlocks", diagnostics->itLastDataBlocks, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITFirstPayloadBytes", diagnostics->itFirstPayloadBytes, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITLastPayloadBytes", diagnostics->itLastPayloadBytes, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITFirstCIPHeader0", diagnostics->itFirstCIPHeader0, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITFirstCIPHeader1", diagnostics->itFirstCIPHeader1, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITLastCIPHeader0", diagnostics->itLastCIPHeader0, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITLastCIPHeader1", diagnostics->itLastCIPHeader1, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITLastDescriptorResCount",
                      diagnostics->itLastDescriptorResCount,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexITLastDescriptorStatus",
                      diagnostics->itLastDescriptorStatus,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexSourceNodeIDField", diagnostics->sourceNodeIDField, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITControlAfterRun", diagnostics->itControlAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITControlAfterWait", diagnostics->itControlAfterWait, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITControlAfterStop", diagnostics->itControlAfterStop, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITStopLoops", diagnostics->itStopLoops, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITEventAfterRun", diagnostics->itEventAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITMaskAfterRun", diagnostics->itMaskAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITMaskAfterClear", diagnostics->itMaskAfterClear, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRControlAfterRun", diagnostics->irControlAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRControlAfterWait", diagnostics->irControlAfterWait, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRControlAfterStop", diagnostics->irControlAfterStop, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRStopLoops", diagnostics->irStopLoops, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIREventAfterRun", diagnostics->irEventAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRMaskAfterRun", diagnostics->irMaskAfterRun, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRMaskAfterClear", diagnostics->irMaskAfterClear, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITDescriptorResCount", diagnostics->itDescriptorResCount, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexITDescriptorStatus", diagnostics->itDescriptorStatus, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptorResCount", diagnostics->irDescriptorResCount, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptorStatus", diagnostics->irDescriptorStatus, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptorCount", diagnostics->irDescriptorCount, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptorDataSize", diagnostics->irDescriptorDataSize, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRActiveDescriptorCount",
                      diagnostics->irActiveDescriptorCount,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptor1ResCount",
                      diagnostics->irDescriptor1ResCount,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptor1Status",
                      diagnostics->irDescriptor1Status,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptor2ResCount",
                      diagnostics->irDescriptor2ResCount,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptor2Status",
                      diagnostics->irDescriptor2Status,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptor3ResCount",
                      diagnostics->irDescriptor3ResCount,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptor3Status",
                      diagnostics->irDescriptor3Status,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRDescriptorLastTouched",
                      diagnostics->irDescriptorLastTouched,
                      32);
    AddNumberProperty(properties, "ProbeDigiDuplexIRRxBytes", diagnostics->irRxBytes, 32);
    const char * headerNames[8] = {
        "ProbeDigiDuplexIRHeader0",
        "ProbeDigiDuplexIRHeader1",
        "ProbeDigiDuplexIRHeader2",
        "ProbeDigiDuplexIRHeader3",
        "ProbeDigiDuplexIRHeader4",
        "ProbeDigiDuplexIRHeader5",
        "ProbeDigiDuplexIRHeader6",
        "ProbeDigiDuplexIRHeader7",
    };
    for (size_t i = 0; i < 8; ++i) {
        AddNumberProperty(properties, headerNames[i], diagnostics->irHeader[i], 32);
    }
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRSamplePacketCount",
                      diagnostics->irSamplePacketCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRSamplePayloadWordCount",
                      diagnostics->irSamplePayloadWordCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRSampleDataBlockBytes",
                      diagnostics->irSampleDataBlockBytes,
                      32);
    for (size_t packet = 0; packet < kDigi00xDuplexIRSamplePacketCount; ++packet) {
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "Index",
                                 diagnostics->irSamplePacketIndex[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "Bytes",
                                 diagnostics->irSampleBytes[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "DataBlocks",
                                 diagnostics->irSampleDataBlocks[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "RemainderBytes",
                                 diagnostics->irSampleRemainderBytes[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "HeaderStatus",
                                 diagnostics->irSampleHeaderStatus[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "PayloadStatus",
                                 diagnostics->irSamplePayloadStatus[packet],
                                 32);
        for (size_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
            char suffix[24];
            int written = snprintf(suffix, sizeof(suffix), "Header%zu", word);
            if (written > 0 && static_cast<size_t>(written) < sizeof(suffix)) {
                AddIndexedNumberProperty(properties,
                                         "ProbeDigiDuplexIRSample",
                                         packet,
                                         suffix,
                                         diagnostics->irSampleHeader[packet][word],
                                         32);
            }
            written = snprintf(suffix, sizeof(suffix), "Header%zuBE", word);
            if (written > 0 && static_cast<size_t>(written) < sizeof(suffix)) {
                AddIndexedNumberProperty(properties,
                                         "ProbeDigiDuplexIRSample",
                                         packet,
                                         suffix,
                                         diagnostics->irSampleHeaderBE[packet][word],
                                         32);
            }
        }
        for (size_t word = 0; word < kDigi00xDuplexIRSamplePayloadWordCount; ++word) {
            char suffix[24];
            int written = snprintf(suffix, sizeof(suffix), "Payload%zu", word);
            if (written > 0 && static_cast<size_t>(written) < sizeof(suffix)) {
                AddIndexedNumberProperty(properties,
                                         "ProbeDigiDuplexIRSample",
                                         packet,
                                         suffix,
                                         diagnostics->irSamplePayload[packet][word],
                                         32);
            }
            written = snprintf(suffix, sizeof(suffix), "Payload%zuBE", word);
            if (written > 0 && static_cast<size_t>(written) < sizeof(suffix)) {
                AddIndexedNumberProperty(properties,
                                         "ProbeDigiDuplexIRSample",
                                         packet,
                                         suffix,
                                         diagnostics->irSamplePayloadBE[packet][word],
                                         32);
            }
        }
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "FirstWordTag",
                                 diagnostics->irSampleFirstWordTag[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "FirstAudioTag",
                                 diagnostics->irSampleFirstAudioTag[packet],
                                 32);
        AddIndexedNumberProperty(properties,
                                 "ProbeDigiDuplexIRSample",
                                 packet,
                                 "FirstAudioValue24",
                                 diagnostics->irSampleFirstAudioValue24[packet],
                                 32);
    }
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRChannelSamplePacketIndex",
                      diagnostics->irChannelSamplePacketIndex,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRChannelSampleBytes",
                      diagnostics->irChannelSampleBytes,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRChannelSampleDataBlocks",
                      diagnostics->irChannelSampleDataBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRChannelSampleCapturedBlocks",
                      diagnostics->irChannelSampleCapturedBlocks,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRChannelSampleChannelCount",
                      diagnostics->irChannelSampleChannelCount,
                      32);
    for (size_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
        char key[96];
        int written = snprintf(key,
                               sizeof(key),
                               "ProbeDigiDuplexIRChannelSampleHeader%zuBE",
                               word);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irChannelSampleHeaderBE[word], 32);
        }
    }
    for (size_t block = 0; block < kDigi00xDuplexIRChannelSampleMaxDataBlocks; ++block) {
        char key[128];
        int written = snprintf(key,
                               sizeof(key),
                               "ProbeDigiDuplexIRChannelSampleBlock%zuTag",
                               block);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irChannelSampleBlockTag[block], 32);
        }
        for (size_t channel = 0; channel < kDigi00xDuplexIRChannelSampleChannelCount; ++channel) {
            written = snprintf(key,
                               sizeof(key),
                               "ProbeDigiDuplexIRChannelSampleBlock%zuChannel%zuBE",
                               block,
                               channel);
            if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
                AddNumberProperty(properties,
                                  key,
                                  diagnostics->irChannelSampleWordBE[block][channel],
                                  32);
            }
            written = snprintf(key,
                               sizeof(key),
                               "ProbeDigiDuplexIRChannelSampleBlock%zuChannel%zuLabel",
                               block,
                               channel);
            if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
                AddNumberProperty(properties,
                                  key,
                                  diagnostics->irChannelSampleLabel[block][channel],
                                  32);
            }
            written = snprintf(key,
                               sizeof(key),
                               "ProbeDigiDuplexIRChannelSampleBlock%zuChannel%zuValue24",
                               block,
                               channel);
            if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
                AddNumberProperty(properties,
                                  key,
                                  diagnostics->irChannelSampleValue24[block][channel],
                                  32);
            }
        }
    }
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCaptureSummaryFrameCount",
                      diagnostics->irCaptureSummaryFrameCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCaptureSummaryPacketCount",
                      diagnostics->irCaptureSummaryPacketCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCaptureSummaryChannelCount",
                      diagnostics->irCaptureSummaryChannelCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCaptureSummaryLabelMismatchCount",
                      diagnostics->irCaptureSummaryLabelMismatchCount,
                      32);
    for (size_t channel = 0; channel < kDigi00xDuplexIRCaptureSummaryChannelCount; ++channel) {
        char key[128];
        int written = snprintf(key,
                               sizeof(key),
                               "ProbeDigiDuplexIRCaptureChannel%zuNonzeroCount",
                               channel);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irCaptureChannelNonzeroCount[channel], 32);
        }
        written = snprintf(key,
                           sizeof(key),
                           "ProbeDigiDuplexIRCaptureChannel%zuFirstNonzeroFrame",
                           channel);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irCaptureChannelFirstNonzeroFrame[channel], 32);
        }
        written = snprintf(key,
                           sizeof(key),
                           "ProbeDigiDuplexIRCaptureChannel%zuMinValue",
                           channel);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irCaptureChannelMinValue[channel], 32);
        }
        written = snprintf(key,
                           sizeof(key),
                           "ProbeDigiDuplexIRCaptureChannel%zuMaxValue",
                           channel);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irCaptureChannelMaxValue[channel], 32);
        }
        written = snprintf(key,
                           sizeof(key),
                           "ProbeDigiDuplexIRCaptureChannel%zuPeakAbs",
                           channel);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irCaptureChannelPeakAbs[channel], 32);
        }
        written = snprintf(key,
                           sizeof(key),
                           "ProbeDigiDuplexIRCaptureChannel%zuLastValue",
                           channel);
        if (written > 0 && static_cast<size_t>(written) < sizeof(key)) {
            AddNumberProperty(properties, key, diagnostics->irCaptureChannelLastValue[channel], 32);
        }
    }
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCapturePCMFrameLimit",
                      diagnostics->irCapturePCMFrameLimit,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCapturePCMFrameCount",
                      diagnostics->irCapturePCMFrameCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCapturePCMChannelCount",
                      diagnostics->irCapturePCMChannelCount,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCapturePCMSampleRate",
                      diagnostics->irCapturePCMSampleRate,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCapturePCMBytes",
                      diagnostics->irCapturePCMBytes,
                      32);
    AddNumberProperty(properties,
                      "ProbeDigiDuplexIRCapturePCMPeakAbs",
                      diagnostics->irCapturePCMPeakAbs,
                      32);
    AddDataProperty(properties,
                    "ProbeDigiDuplexIRCapturePCMS24LE",
                    diagnostics->irCapturePCMS24,
                    diagnostics->irCapturePCMBytes);
    AddNumberProperty(properties, "ProbeDigiDuplexSyncForDeviceRet", diagnostics->syncForDeviceRet, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexSyncForCPURet", diagnostics->syncForCPURet, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexCompleteRet", diagnostics->completeRet, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexBufferResult", ReturnCodeToProperty(buffer->result), 32);
    AddNumberProperty(properties, "ProbeDigiDuplexBufferSegmentCount", buffer->segmentCount, 32);
    AddNumberProperty(properties, "ProbeDigiDuplexBufferDMAAddress", buffer->dmaSegment.address, 64);
    AddNumberProperty(properties, "ProbeDigiDuplexBufferDMALength", buffer->dmaSegment.length, 64);
}

void
InitializeAsyncDiagnostics(AsyncReadDiagnostics * diagnostics)
{
    uint32_t * words = reinterpret_cast<uint32_t *>(diagnostics);
    for (size_t i = 0; i < sizeof(*diagnostics) / sizeof(uint32_t); ++i) {
        words[i] = 0xffffffff;
    }
    diagnostics->attempted = 0;
    diagnostics->ready = 0;
    diagnostics->waitLoops = 0;
    diagnostics->configuredAttempts = 0;
    diagnostics->completedAttempts = 0;
    diagnostics->ackPendingAttempts = 0;
    diagnostics->waitLoopsPerAttempt = 0;
    diagnostics->retrySettleMilliseconds = 0;
    diagnostics->configROMReadCount = 0;
    diagnostics->configROMReadSuccessCount = 0;
    diagnostics->digiRegisterReadCount = 0;
    diagnostics->digiRegisterReadSuccessCount = 0;
    diagnostics->digiWritePlanEnabled = kDigi00xWritePlanEnabled;
    diagnostics->digiWritePlanExecuted = 0;
    diagnostics->digiWritePlanCount = 0;
    diagnostics->digiNoopWriteEnabled = kDigi00xNoopWriteEnabled;
    diagnostics->digiNoopWriteAttempted = 0;
    diagnostics->digiNoopWriteExecuted = 0;
    diagnostics->digiNoopWriteSuccess = 0;
    diagnostics->digiNoopWriteCompletedAttempts = 0;
    diagnostics->digiNoopWriteWaitLoops = 0;
    diagnostics->digiStateWriteEnabled = kDigi00xStateWriteEnabled;
    diagnostics->digiStateWriteAttempted = 0;
    diagnostics->digiStateWritePrereqNoopSuccess = 0;
    diagnostics->digiStateWriteExecuted = 0;
    diagnostics->digiStateWriteSuccess = 0;
    diagnostics->digiStateWriteCompletedAttempts = 0;
    diagnostics->digiStateWriteWaitLoops = 0;
    diagnostics->digiStateSequenceEnabled = kDigi00xStateSequenceEnabled;
    diagnostics->digiStateSequenceAttempted = 0;
    diagnostics->digiStateSequencePrereqStateWriteSuccess = 0;
    diagnostics->digiStateSequenceStepCount = 0;
    diagnostics->digiStateSequenceCompletedSteps = 0;
    diagnostics->digiStateSequenceSuccess = 0;
    for (size_t i = 0; i < kDigi00xStateSequenceStepCount; ++i) {
        diagnostics->digiStateSequenceOp[i] = 0;
        diagnostics->digiStateSequenceOffsetLo[i] = 0;
        diagnostics->digiStateSequenceBusValue[i] = 0;
        diagnostics->digiStateSequenceTxData[i] = 0;
        diagnostics->digiStateSequenceReqCount[i] = 0;
        diagnostics->digiStateSequenceCompletedAttempts[i] = 0;
        diagnostics->digiStateSequenceWaitLoops[i] = 0;
        diagnostics->digiStateSequenceSuccessByStep[i] = 0;
        diagnostics->digiStateSequenceHeader0[i] = 0;
        diagnostics->digiStateSequenceHeader1[i] = 0;
        diagnostics->digiStateSequenceHeader2[i] = 0;
        diagnostics->digiStateSequenceHeader3[i] = 0;
        diagnostics->digiStateSequenceTxStatus[i] = 0;
        diagnostics->digiStateSequenceRxBytes[i] = 0;
        diagnostics->digiStateSequenceResponseTCode[i] = 0;
        diagnostics->digiStateSequenceResponseTLabel[i] = 0;
        diagnostics->digiStateSequenceResponseSource[i] = 0;
        diagnostics->digiStateSequenceResponseRCode[i] = 0;
        diagnostics->digiStateSequenceResponseData[i] = 0;
    }
}

void
InitializeIsoTestDiagnostics(IsoTestDiagnostics * diagnostics)
{
    uint32_t * words = reinterpret_cast<uint32_t *>(diagnostics);
    for (size_t i = 0; i < sizeof(*diagnostics) / sizeof(uint32_t); ++i) {
        words[i] = 0xffffffff;
    }
    diagnostics->enabled = kIsoTestEnabled;
    diagnostics->attempted = 0;
    diagnostics->ready = 0;
    diagnostics->contextIndex = kIsoTestContextIndex;
    diagnostics->channel = kIsoTestChannel;
    diagnostics->runReceiveEnabled = kIsoTestRunReceiveEnabled;
    diagnostics->runReceiveAttempted = 0;
    diagnostics->runReceiveMilliseconds = kIsoTestRunReceiveMilliseconds;
    diagnostics->runTransmitEnabled = kIsoTestRunTransmitEnabled;
    diagnostics->runTransmitAttempted = 0;
    diagnostics->runTransmitMilliseconds = kIsoTestRunTransmitMilliseconds;
    diagnostics->syncForDeviceRet = ReturnCodeToProperty(kIOReturnNotReady);
    diagnostics->syncForCPURet = ReturnCodeToProperty(kIOReturnNotReady);
    diagnostics->completeRet = ReturnCodeToProperty(kIOReturnNotReady);
}

void
InitializeDigiDuplexDiagnostics(DigiDuplexDiagnostics * diagnostics)
{
    uint32_t * words = reinterpret_cast<uint32_t *>(diagnostics);
    for (size_t i = 0; i < sizeof(*diagnostics) / sizeof(uint32_t); ++i) {
        words[i] = 0xffffffff;
    }
    diagnostics->enabled = kDigi00xDuplexProbeEnabled;
    diagnostics->attempted = 0;
    diagnostics->destinationID = 0xffffffff;
    diagnostics->deviceTransmitChannel = kDigi00xDuplexDeviceTransmitChannel;
    diagnostics->deviceReceiveChannel = kDigi00xDuplexDeviceReceiveChannel;
    diagnostics->sessionChannelsBusValue =
        (kDigi00xDuplexDeviceTransmitChannel << 16) | kDigi00xDuplexDeviceReceiveChannel;
    diagnostics->stepCount = 0;
    diagnostics->completedSteps = 0;
    diagnostics->success = 0;
    diagnostics->finishAttempted = 0;
    diagnostics->isoAttempted = 0;
    diagnostics->isoReady = 0;
    diagnostics->itPacketCount = kDigi00xDuplexITPacketCount;
    diagnostics->itDataBlockQuadlets = kDigi00xDuplexDataBlockQuadlets;
    diagnostics->itTotalDataBlocks = 0;
    diagnostics->itFirstDataBlocks = 0;
    diagnostics->itLastDataBlocks = 0;
    diagnostics->itFirstPayloadBytes = 0;
    diagnostics->itLastPayloadBytes = 0;
    diagnostics->sourceNodeIDField = 0;
    diagnostics->irDescriptorCount = kDigi00xDuplexIRDescriptorCount;
    diagnostics->irDescriptorDataSize = kDigi00xDuplexIRDescriptorDataSize;
    diagnostics->irActiveDescriptorCount = 0;
    diagnostics->irRxBytes = 0;
    diagnostics->syncForDeviceRet = ReturnCodeToProperty(kIOReturnNotReady);
    diagnostics->syncForCPURet = ReturnCodeToProperty(kIOReturnNotReady);
    diagnostics->completeRet = ReturnCodeToProperty(kIOReturnNotReady);
    for (size_t i = 0; i < kDigi00xDuplexStepCount; ++i) {
        diagnostics->stepOp[i] = 0;
        diagnostics->stepOffsetLo[i] = 0;
        diagnostics->stepBusValue[i] = 0;
        diagnostics->stepTxData[i] = 0;
        diagnostics->stepSuccess[i] = 0;
        diagnostics->stepCompletedAttempts[i] = 0;
        diagnostics->stepWaitLoops[i] = 0;
        diagnostics->stepTxStatus[i] = 0;
        diagnostics->stepRxBytes[i] = 0;
        diagnostics->stepResponseTCode[i] = 0;
        diagnostics->stepResponseTLabel[i] = 0;
        diagnostics->stepResponseSource[i] = 0;
        diagnostics->stepResponseRCode[i] = 0;
        diagnostics->stepResponseData[i] = 0;
    }
    for (size_t i = 0; i < 8; ++i) {
        diagnostics->irHeader[i] = 0;
    }
    diagnostics->irSamplePacketCount = kDigi00xDuplexIRSamplePacketCount;
    diagnostics->irSamplePayloadWordCount = kDigi00xDuplexIRSamplePayloadWordCount;
    diagnostics->irSampleDataBlockBytes = kDigi00xDuplexDataBlockBytes;
    for (size_t packet = 0; packet < kDigi00xDuplexIRSamplePacketCount; ++packet) {
        diagnostics->irSamplePacketIndex[packet] = static_cast<uint32_t>(packet);
        diagnostics->irSampleBytes[packet] = 0;
        diagnostics->irSampleDataBlocks[packet] = 0;
        diagnostics->irSampleRemainderBytes[packet] = 0;
        diagnostics->irSampleHeaderStatus[packet] = 0;
        diagnostics->irSamplePayloadStatus[packet] = 0;
        diagnostics->irSampleFirstWordTag[packet] = 0;
        diagnostics->irSampleFirstAudioTag[packet] = 0;
        diagnostics->irSampleFirstAudioValue24[packet] = 0;
        for (size_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
            diagnostics->irSampleHeader[packet][word] = 0;
            diagnostics->irSampleHeaderBE[packet][word] = 0;
        }
        for (size_t word = 0; word < kDigi00xDuplexIRSamplePayloadWordCount; ++word) {
            diagnostics->irSamplePayload[packet][word] = 0;
            diagnostics->irSamplePayloadBE[packet][word] = 0;
        }
    }
    diagnostics->irChannelSamplePacketIndex = 0xffffffff;
    diagnostics->irChannelSampleBytes = 0;
    diagnostics->irChannelSampleDataBlocks = 0;
    diagnostics->irChannelSampleCapturedBlocks = 0;
    diagnostics->irChannelSampleChannelCount = kDigi00xDuplexIRChannelSampleChannelCount;
    for (size_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
        diagnostics->irChannelSampleHeaderBE[word] = 0;
    }
    for (size_t block = 0; block < kDigi00xDuplexIRChannelSampleMaxDataBlocks; ++block) {
        diagnostics->irChannelSampleBlockTag[block] = 0;
        for (size_t channel = 0; channel < kDigi00xDuplexIRChannelSampleChannelCount; ++channel) {
            diagnostics->irChannelSampleWordBE[block][channel] = 0;
            diagnostics->irChannelSampleLabel[block][channel] = 0;
            diagnostics->irChannelSampleValue24[block][channel] = 0;
        }
    }
    diagnostics->irCaptureSummaryFrameCount = 0;
    diagnostics->irCaptureSummaryPacketCount = 0;
    diagnostics->irCaptureSummaryChannelCount = kDigi00xDuplexIRCaptureSummaryChannelCount;
    diagnostics->irCaptureSummaryLabelMismatchCount = 0;
    for (size_t channel = 0; channel < kDigi00xDuplexIRCaptureSummaryChannelCount; ++channel) {
        diagnostics->irCaptureChannelNonzeroCount[channel] = 0;
        diagnostics->irCaptureChannelFirstNonzeroFrame[channel] = 0xffffffff;
        diagnostics->irCaptureChannelMinValue[channel] = 0;
        diagnostics->irCaptureChannelMaxValue[channel] = 0;
        diagnostics->irCaptureChannelPeakAbs[channel] = 0;
        diagnostics->irCaptureChannelLastValue[channel] = 0;
    }
    diagnostics->irCapturePCMFrameLimit = kDigi00xDuplexIRCapturePCMFrameLimit;
    diagnostics->irCapturePCMFrameCount = 0;
    diagnostics->irCapturePCMChannelCount = kDigi00xDuplexIRCapturePCMChannelCount;
    diagnostics->irCapturePCMSampleRate = gDigi00xCurrentSampleRate;
    diagnostics->irCapturePCMBytes = 0;
    diagnostics->irCapturePCMPeakAbs = 0;
    for (size_t frame = 0; frame < kDigi00xDuplexIRCapturePCMFrameLimit; ++frame) {
        for (size_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
            diagnostics->irCapturePCMS24[frame][channel] = 0;
        }
    }
}

void
StopContext(IOPCIDevice * pciDevice,
            uint8_t memoryIndex,
            uint64_t controlSetOffset,
            uint64_t controlClearOffset,
            uint32_t * loops,
            uint32_t * controlAfter)
{
    *loops = 0;
    *controlAfter = 0xffffffff;
    pciDevice->MemoryWrite32(memoryIndex, controlClearOffset, 0xffffffff);
    for (uint32_t i = 0; i < 1000; ++i) {
        uint32_t value = 0xffffffff;
        pciDevice->MemoryRead32(memoryIndex, controlSetOffset, &value);
        *controlAfter = value;
        *loops = i + 1;
        if ((value & kContextActive) == 0) {
            break;
        }
        IODelay(10);
    }
}

void
RunIsoContextProbe(IOPCIDevice * pciDevice, uint8_t memoryIndex, IsoTestDiagnostics * diagnostics)
{
    if (kIsoTestEnabled == 0) {
        return;
    }

    diagnostics->attempted = 1;
    CreateDMABuffer(pciDevice, kIsoTestBufferSize, &gIsoTestBuffer);
    if (gIsoTestBuffer.result != kIOReturnSuccess ||
        gIsoTestBuffer.segmentCount == 0 ||
        gIsoTestBuffer.dmaSegment.address > 0xffffffffull ||
        gIsoTestBuffer.dmaSegment.length < kIsoTestBufferSize) {
        return;
    }
    diagnostics->ready = 1;

    volatile OHCIAsyncDescriptor * itDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gIsoTestBuffer.cpuRange.address +
                                                        kIsoTestITDescriptorOffset);
    volatile OHCIAsyncDescriptor * irDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gIsoTestBuffer.cpuRange.address +
                                                        kIsoTestIRDescriptorOffset);
    uint32_t dmaBase = static_cast<uint32_t>(gIsoTestBuffer.dmaSegment.address);
    uint32_t itDescriptorDMA = dmaBase + kIsoTestITDescriptorOffset;
    uint32_t irDescriptorDMA = dmaBase + kIsoTestIRDescriptorOffset;
    uint32_t irDataDMA = dmaBase + kIsoTestIRDataOffset;
    volatile uint32_t * itImmediateHeader = reinterpret_cast<volatile uint32_t *>(&itDescriptor[1]);

    diagnostics->itImmediateHeader0 =
        (kSCode400 << 16) |
        (kIsoTestChannel << 8) |
        (kTCodeStreamData << 4);
    diagnostics->itImmediateHeader1 = 0;
    itDescriptor[0].reqCount = 8;
    itDescriptor[0].control = kDescriptorKeyImmediate |
                              kDescriptorOutputLast |
                              kDescriptorStatus |
                              kDescriptorIrqAlways |
                              kDescriptorBranchAlways;
    itDescriptor[0].dataAddress = 0;
    itDescriptor[0].branchAddress = 0;
    itDescriptor[0].resCount = 8;
    itDescriptor[0].transferStatus = 0;
    itImmediateHeader[0] = diagnostics->itImmediateHeader0;
    itImmediateHeader[1] = diagnostics->itImmediateHeader1;

    irDescriptor[0].reqCount = kIsoTestIRDataSize;
    irDescriptor[0].control = kDescriptorInputMore | kDescriptorStatus | kDescriptorBranchAlways;
    irDescriptor[0].dataAddress = irDataDMA;
    irDescriptor[0].branchAddress = irDescriptorDMA | 1u;
    irDescriptor[0].resCount = kIsoTestIRDataSize;
    irDescriptor[0].transferStatus = 0;

    diagnostics->itDescriptorControl = itDescriptor[0].control;
    diagnostics->itDescriptorReqCount = itDescriptor[0].reqCount;
    diagnostics->itDescriptorDataAddress = itDescriptor[0].dataAddress;
    diagnostics->itDescriptorBranchAddress = itDescriptor[0].branchAddress;
    diagnostics->itDescriptorResCount = itDescriptor[0].resCount;
    diagnostics->itDescriptorStatus = itDescriptor[0].transferStatus;
    diagnostics->irDescriptorControl = irDescriptor[0].control;
    diagnostics->irDescriptorReqCount = irDescriptor[0].reqCount;
    diagnostics->irDescriptorDataAddress = irDescriptor[0].dataAddress;
    diagnostics->irDescriptorBranchAddress = irDescriptor[0].branchAddress;
    diagnostics->irDescriptorResCount = irDescriptor[0].resCount;
    diagnostics->irDescriptorStatus = irDescriptor[0].transferStatus;
    diagnostics->syncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gIsoTestBuffer,
                                                                                kIsoTestBufferSize));

    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntEventSetOffset, &diagnostics->xmitEventBefore);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntEventSetOffset, &diagnostics->recvEventBefore);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, 0xffffffff);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->xmitMaskSupport);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskClearOffset, 0xffffffff);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->xmitMaskAfterClear);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, 0xffffffff);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->recvMaskSupport);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskClearOffset, 0xffffffff);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->recvMaskAfterClear);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntEventClearOffset, 0xffffffff);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntEventClearOffset, 0xffffffff);

    uint32_t contextBit = 1u << kIsoTestContextIndex;
    diagnostics->xmitContextSupported = (diagnostics->xmitMaskSupport & contextBit) != 0 ? 1 : 0;
    diagnostics->recvContextSupported = (diagnostics->recvMaskSupport & contextBit) != 0 ? 1 : 0;

    uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kIsoTestContextIndex);
    uint64_t itControlClear = OhciIsoXmitContextControlClearOffset(kIsoTestContextIndex);
    uint64_t itCommandPtrOffset = OhciIsoXmitCommandPtrOffset(kIsoTestContextIndex);
    uint64_t irControlSet = OhciIsoRcvContextControlSetOffset(kIsoTestContextIndex);
    uint64_t irControlClear = OhciIsoRcvContextControlClearOffset(kIsoTestContextIndex);
    uint64_t irCommandPtrOffset = OhciIsoRcvCommandPtrOffset(kIsoTestContextIndex);
    uint64_t irContextMatchOffset = OhciIsoRcvContextMatchOffset(kIsoTestContextIndex);

    if (diagnostics->xmitContextSupported != 0) {
        pciDevice->MemoryRead32(memoryIndex, itControlSet, &diagnostics->itControlBefore);
        StopContext(pciDevice,
                    memoryIndex,
                    itControlSet,
                    itControlClear,
                    &diagnostics->itStopLoops,
                    &diagnostics->itControlAfterStop);
        diagnostics->itCommandPtr = itDescriptorDMA | 2u;
        pciDevice->MemoryWrite32(memoryIndex, itCommandPtrOffset, diagnostics->itCommandPtr);
        pciDevice->MemoryRead32(memoryIndex, itCommandPtrOffset, &diagnostics->itCommandPtrReadBack);
        pciDevice->MemoryRead32(memoryIndex, itControlSet, &diagnostics->itControlAfterCommandPtr);
    }

    if (diagnostics->recvContextSupported != 0) {
        pciDevice->MemoryRead32(memoryIndex, irControlSet, &diagnostics->irControlBefore);
        StopContext(pciDevice,
                    memoryIndex,
                    irControlSet,
                    irControlClear,
                    &diagnostics->irStopLoops,
                    &diagnostics->irControlAfterStop);
        diagnostics->irCommandPtr = irDescriptorDMA | 1u;
        diagnostics->irContextMatch = (0xfu << 28) | kIsoTestChannel;
        pciDevice->MemoryWrite32(memoryIndex, irCommandPtrOffset, diagnostics->irCommandPtr);
        pciDevice->MemoryWrite32(memoryIndex, irContextMatchOffset, diagnostics->irContextMatch);
        pciDevice->MemoryRead32(memoryIndex, irCommandPtrOffset, &diagnostics->irCommandPtrReadBack);
        pciDevice->MemoryRead32(memoryIndex, irContextMatchOffset, &diagnostics->irContextMatchReadBack);
        pciDevice->MemoryRead32(memoryIndex, irControlSet, &diagnostics->irControlAfterCommandPtr);
    }

    if (kIsoTestRunReceiveEnabled != 0 &&
        diagnostics->recvContextSupported != 0 &&
        diagnostics->syncForDeviceRet == ReturnCodeToProperty(kIOReturnSuccess)) {
        diagnostics->runReceiveAttempted = 1;
        diagnostics->irRunControl = kIrContextIsochHeader | kContextRun;
        pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntEventClearOffset, contextBit);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, contextBit);
        pciDevice->MemoryWrite32(memoryIndex, irControlSet, diagnostics->irRunControl);
        pciDevice->MemoryRead32(memoryIndex, irControlSet, &diagnostics->irControlAfterRun);
        IOSleep(kIsoTestRunReceiveMilliseconds);
        pciDevice->MemoryRead32(memoryIndex, irControlSet, &diagnostics->irControlAfterWait);
        pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntEventSetOffset, &diagnostics->irEventAfterRun);
        pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->irMaskAfterRun);
        StopContext(pciDevice,
                    memoryIndex,
                    irControlSet,
                    irControlClear,
                    &diagnostics->irRunStopLoops,
                    &diagnostics->irControlAfterFinalStop);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskClearOffset, contextBit);
        pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->irMaskAfterFinalClear);
    }

    if (kIsoTestRunTransmitEnabled != 0 &&
        diagnostics->xmitContextSupported != 0 &&
        diagnostics->syncForDeviceRet == ReturnCodeToProperty(kIOReturnSuccess)) {
        diagnostics->runTransmitAttempted = 1;
        diagnostics->itRunControl = kContextRun;
        pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntEventClearOffset, contextBit);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, contextBit);
        pciDevice->MemoryWrite32(memoryIndex, itCommandPtrOffset, diagnostics->itCommandPtr);
        pciDevice->MemoryWrite32(memoryIndex, itControlClear, 0xffffffff);
        pciDevice->MemoryWrite32(memoryIndex, itControlSet, diagnostics->itRunControl);
        pciDevice->MemoryRead32(memoryIndex, itControlSet, &diagnostics->itControlAfterRun);
        IOSleep(kIsoTestRunTransmitMilliseconds);
        pciDevice->MemoryRead32(memoryIndex, itControlSet, &diagnostics->itControlAfterWait);
        pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntEventSetOffset, &diagnostics->itEventAfterRun);
        pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->itMaskAfterRun);
        diagnostics->itDescriptorResCountAfterRun = itDescriptor[0].resCount;
        diagnostics->itDescriptorStatusAfterRun = itDescriptor[0].transferStatus;
        StopContext(pciDevice,
                    memoryIndex,
                    itControlSet,
                    itControlClear,
                    &diagnostics->itRunStopLoops,
                    &diagnostics->itControlAfterFinalStop);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskClearOffset, contextBit);
        pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->itMaskAfterFinalClear);
    }

    diagnostics->syncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gIsoTestBuffer,
                                                                          kIsoTestBufferSize));
    diagnostics->itDescriptorResCount = itDescriptor[0].resCount;
    diagnostics->itDescriptorStatus = itDescriptor[0].transferStatus;
    diagnostics->irDescriptorResCount = irDescriptor[0].resCount;
    diagnostics->irDescriptorStatus = irDescriptor[0].transferStatus;
    diagnostics->irDescriptorResCountAfterRun = irDescriptor[0].resCount;
    diagnostics->irDescriptorStatusAfterRun = irDescriptor[0].transferStatus;
    diagnostics->itDescriptorResCountAfterRun = itDescriptor[0].resCount;
    diagnostics->itDescriptorStatusAfterRun = itDescriptor[0].transferStatus;
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntEventSetOffset, &diagnostics->xmitEventAfter);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntEventSetOffset, &diagnostics->recvEventAfter);
    diagnostics->completeRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gIsoTestBuffer));
}

uint32_t
BuildIsoTransmitHeader0(uint32_t channel)
{
    return (kSCode400 << 16) |
           (1u << 14) |
           ((channel & 0x3fu) << 8) |
           (kTCodeStreamData << 4);
}

uint32_t
BuildIsoTransmitHeader1(uint32_t dataLength)
{
    return (dataLength & 0xffffu) << 16;
}

uint32_t
Digi00xDuplexDataBlocksForPacket(uint32_t packetIndex)
{
    if (gDigi00xCurrentSampleRate == kDigi00xDuplexSampleRate48000) {
        return 6u;
    }

    uint32_t phase = packetIndex % 80u;
    return 5u + (((phase & 1u) ^ ((phase == 0u || phase >= 40u) ? 1u : 0u)) & 1u);
}

void
ResetDigiLiveSequenceReplayState()
{
    for (uint32_t i = 0; i < kDigiLiveSequenceReplayPeriodPackets; ++i) {
        gDigiLiveSequenceReplayPeriod[i] = 0;
    }
    for (uint32_t i = 0; i < kDigiLiveSequenceReplayMovingQueuePackets; ++i) {
        gDigiLiveSequenceReplayMovingQueue[i] = 0;
    }
    for (uint32_t i = 0; i < kDigi00xDuplexITPacketCount; ++i) {
        gDigiLiveTxDataBlocks[i] = 0;
    }
    gDigiLiveSequenceReplayPeriodCount = 0;
    gDigiLiveSequenceReplayReady = 0;
    gDigiLiveSequenceReplayActive = 0;
    gDigiLiveSequenceReplayApplyAttemptCount = 0;
    gDigiLiveSequenceReplayApplySuccessCount = 0;
    gDigiLiveSequenceReplayApplyRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveSequenceReplayResetCount = 0;
    gDigiLiveSequenceReplayInvalidCount = 0;
    gDigiLiveSequenceReplayDiscontinuityCount = 0;
    gDigiLiveSequenceReplayObservedTotalDataBlocks = 0;
    gDigiLiveSequenceReplayBadTotalCount = 0;
    gDigiLiveSequenceReplayLastBadTotalDataBlocks = 0;
    gDigiLiveSequenceReplayIdealMismatchCount = 0;
    gDigiLiveSequenceReplayFirstDataBlocks = 0;
    gDigiLiveSequenceReplayLastDataBlocks = 0;
    gDigiLiveSequenceReplayMovingQueueReadIndex = 0;
    gDigiLiveSequenceReplayMovingQueueWriteIndex = 0;
    gDigiLiveSequenceReplayMovingQueueCount = 0;
    gDigiLiveSequenceReplayMovingAppendCount = 0;
    gDigiLiveSequenceReplayMovingDropCount = 0;
    gDigiLiveSequenceReplayMovingClearCount = 0;
    gDigiLiveSequenceReplayMovingDiscontinuityCount = 0;
    gDigiLiveSequenceReplayMovingInvalidCount = 0;
    gDigiLiveSequenceReplayMovingUpdateAttemptCount = 0;
    gDigiLiveSequenceReplayMovingUpdateSuccessCount = 0;
    gDigiLiveSequenceReplayMovingUpdatePacketCount = 0;
    gDigiLiveSequenceReplayMovingDryRunSuccessCount = 0;
    gDigiLiveSequenceReplayMovingDryRunPacketCount = 0;
    gDigiLiveSequenceReplayMovingShortQueueCount = 0;
    gDigiLiveSequenceReplayMovingBadTotalCount = 0;
    gDigiLiveSequenceReplayMovingBadCommandPtrCount = 0;
    gDigiLiveSequenceReplayMovingCadencePhaseUseCount = 0;
    gDigiLiveSequenceReplayMovingCadencePhasePacketCount = 0;
    gDigiLiveSequenceReplayMovingCadenceNotReadyCount = 0;
    gDigiLiveSequenceReplayMovingCadenceMismatchRejectCount = 0;
    gDigiLiveSequenceReplayMovingCadenceLearnCount = 0;
    gDigiLiveSequenceReplayMovingCadenceLearnPacketCount = 0;
    gDigiLiveSequenceReplayMovingCadenceLearnRejectCount = 0;
    gDigiLiveSequenceReplayMovingCadenceCachedUseCount = 0;
    gDigiLiveSequenceReplayMovingGuardEligibleCount = 0;
    gDigiLiveSequenceReplayMovingGuardRejectCount = 0;
    gDigiLiveSequenceReplayMovingGuardDryRunWouldWriteCount = 0;
    gDigiLiveSequenceReplayMovingGuardDryRunWouldRejectCount = 0;
    gDigiLiveSequenceReplayMovingLastCurrentPacketIndex = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastUpdateStartIndex = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastUpdatePackets = 0;
    gDigiLiveSequenceReplayMovingLastTotalDataBlocks = 0;
    gDigiLiveSequenceReplayMovingLastRawTotalDataBlocks = 0;
    gDigiLiveSequenceReplayMovingLastCadencePhase = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastCadenceMismatchCount = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastCadenceSource = 0;
    gDigiLiveSequenceReplayMovingCachedCadencePhase = 0xffffffff;
    gDigiLiveSequenceReplayMovingCachedCadenceMismatchCount = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastStartDistancePackets = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastEndDistancePackets = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastWindowWrapsHardware = 0;
    gDigiLiveSequenceReplayMovingLastGuardWouldWrite = 0;
    gDigiLiveSequenceReplayMovingLastStartDBC = 0;
    gDigiLiveSequenceReplayMovingLastEndDBC = 0;
    gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveSourceNodeIDField = 0;
}

void
ClearDigiLiveMovingReplayQueue()
{
    gDigiLiveSequenceReplayMovingQueueReadIndex = 0;
    gDigiLiveSequenceReplayMovingQueueWriteIndex = 0;
    gDigiLiveSequenceReplayMovingQueueCount = 0;
    gDigiLiveSequenceReplayMovingClearCount++;
}

void
AppendDigiLiveMovingReplayPacket(uint32_t dataBlocks, bool continuous)
{
    if (kDigiLiveSequenceReplayMovingEnabled == 0) {
        return;
    }
    if (dataBlocks != 5 && dataBlocks != 6) {
        gDigiLiveSequenceReplayMovingInvalidCount++;
        ClearDigiLiveMovingReplayQueue();
        return;
    }
    if (!continuous) {
        gDigiLiveSequenceReplayMovingDiscontinuityCount++;
        if (kDigiLiveSequenceReplayMovingRequireContinuity != 0) {
            ClearDigiLiveMovingReplayQueue();
            return;
        }
    }

    gDigiLiveSequenceReplayMovingQueue[gDigiLiveSequenceReplayMovingQueueWriteIndex] =
        static_cast<uint8_t>(dataBlocks);
    gDigiLiveSequenceReplayMovingQueueWriteIndex =
        (gDigiLiveSequenceReplayMovingQueueWriteIndex + 1) %
        kDigiLiveSequenceReplayMovingQueuePackets;
    if (gDigiLiveSequenceReplayMovingQueueCount < kDigiLiveSequenceReplayMovingQueuePackets) {
        gDigiLiveSequenceReplayMovingQueueCount++;
    } else {
        gDigiLiveSequenceReplayMovingQueueReadIndex =
            (gDigiLiveSequenceReplayMovingQueueReadIndex + 1) %
            kDigiLiveSequenceReplayMovingQueuePackets;
        gDigiLiveSequenceReplayMovingDropCount++;
    }
    gDigiLiveSequenceReplayMovingAppendCount++;
}

void
RecordDigiLiveSequenceReplayPacket(uint32_t dataBlocks, bool continuous)
{
    if (kDigiLiveSequenceReplayMovingEnabled != 0) {
        AppendDigiLiveMovingReplayPacket(dataBlocks, continuous);
    }

    if (kDigiLiveSequenceReplayEnabled == 0 ||
        gDigiLiveSequenceReplayActive != 0) {
        return;
    }
    if (gDigiLiveSequenceReplayReady != 0) {
        return;
    }

    if (dataBlocks != 5 && dataBlocks != 6) {
        gDigiLiveSequenceReplayInvalidCount++;
        if (gDigiLiveSequenceReplayPeriodCount != 0) {
            gDigiLiveSequenceReplayResetCount++;
        }
        gDigiLiveSequenceReplayPeriodCount = 0;
        gDigiLiveSequenceReplayObservedTotalDataBlocks = 0;
        return;
    }

    if (!continuous) {
        gDigiLiveSequenceReplayDiscontinuityCount++;
        if (kDigiLiveSequenceReplayRequireContinuity != 0) {
            if (gDigiLiveSequenceReplayPeriodCount != 0) {
                gDigiLiveSequenceReplayResetCount++;
            }
            gDigiLiveSequenceReplayPeriodCount = 0;
            gDigiLiveSequenceReplayObservedTotalDataBlocks = 0;
            return;
        }
    }

    uint32_t index = gDigiLiveSequenceReplayPeriodCount;
    gDigiLiveSequenceReplayPeriod[index] = static_cast<uint8_t>(dataBlocks);
    gDigiLiveSequenceReplayObservedTotalDataBlocks += dataBlocks;
    if (index == 0) {
        gDigiLiveSequenceReplayFirstDataBlocks = dataBlocks;
    }
    gDigiLiveSequenceReplayLastDataBlocks = dataBlocks;
    if (dataBlocks != Digi00xDuplexDataBlocksForPacket(index)) {
        gDigiLiveSequenceReplayIdealMismatchCount++;
    }

    gDigiLiveSequenceReplayPeriodCount++;
    if (gDigiLiveSequenceReplayPeriodCount >= kDigiLiveSequenceReplayPeriodPackets) {
        if (gDigiLiveSequenceReplayObservedTotalDataBlocks ==
            DigiLiveSequenceReplayPeriodDataBlocks()) {
            gDigiLiveSequenceReplayReady = 1;
        } else {
            gDigiLiveSequenceReplayBadTotalCount++;
            gDigiLiveSequenceReplayLastBadTotalDataBlocks =
                gDigiLiveSequenceReplayObservedTotalDataBlocks;
            gDigiLiveSequenceReplayResetCount++;
            gDigiLiveSequenceReplayPeriodCount = 0;
            gDigiLiveSequenceReplayObservedTotalDataBlocks = 0;
        }
    }
}

uint32_t
DigiLiveTransmitDataBlocksForPacket(uint32_t packetIndex,
                                    const uint8_t * replayPeriod,
                                    uint32_t replayPeriodCount)
{
    if (replayPeriod != nullptr && replayPeriodCount != 0) {
        uint32_t dataBlocks = replayPeriod[packetIndex % replayPeriodCount];
        if (dataBlocks == 5 || dataBlocks == 6) {
            return dataBlocks;
        }
    }
    return Digi00xDuplexDataBlocksForPacket(packetIndex);
}

void
WriteDigiLiveSilentTransmitDataBlock(volatile uint32_t * payload)
{
    payload[0] = ToBigEndian32(PopDigiLiveMidiEchoWordBE());
    for (uint32_t channel = 0; channel < kDigi00xDuplexPCMAudioChannels; ++channel) {
        payload[1 + channel] = ToBigEndian32(0x40000000u);
    }
}

void
UpdateDigiLiveTransmitPacketDescriptor(volatile OHCIAsyncDescriptor * itDescriptor,
                                       volatile uint32_t * itHeaderStorage,
                                       uint32_t packetIndex,
                                       uint32_t dataBlocks,
                                       uint32_t sourceNodeIDField,
                                       uint32_t dataBlockCounter)
{
    volatile OHCIAsyncDescriptor * packetDescriptor =
        itDescriptor + (packetIndex * kDigi00xDuplexITDescriptorsPerPacket);
    uint32_t payloadLength = dataBlocks * kDigi00xDuplexDataBlockQuadlets * sizeof(uint32_t);
    uint32_t packetDataLength = 8u + payloadLength;
    uint32_t cipHeader0 =
        sourceNodeIDField |
        (kDigi00xDuplexDataBlockQuadlets << 16) |
        (dataBlockCounter & 0xffu);
    uint32_t cipHeader1 =
        0x80000000u |
        (0x10u << 24) |
        (gDigi00xCurrentCIPSFC << 16) |
        0xffffu;

    volatile uint32_t * immediateHeader = reinterpret_cast<volatile uint32_t *>(&packetDescriptor[1]);
    immediateHeader[1] = BuildIsoTransmitHeader1(packetDataLength);
    packetDescriptor[3].reqCount = static_cast<uint16_t>(payloadLength);
    packetDescriptor[3].resCount = static_cast<uint16_t>(payloadLength);
    packetDescriptor[3].transferStatus = 0;

    volatile uint32_t * cipHeader = itHeaderStorage + (packetIndex * 2);
    cipHeader[0] = ToBigEndian32(cipHeader0);
    cipHeader[1] = ToBigEndian32(cipHeader1);
    gDigiLiveTxDataBlocks[packetIndex] = static_cast<uint8_t>(dataBlocks);
}

uint32_t
DigiLiveTransmitDBCForPacket(volatile uint32_t * itHeaderStorage, uint32_t packetIndex)
{
    if (packetIndex == 0) {
        return 0;
    }

    uint32_t previousIndex = packetIndex - 1;
    uint32_t previousHeader0 = ToBigEndian32(itHeaderStorage[previousIndex * 2]);
    uint32_t previousDBC = previousHeader0 & kDigi00xCIPDBCMask;
    uint32_t previousDataBlocks = gDigiLiveTxDataBlocks[previousIndex];
    if (previousDataBlocks != 5 && previousDataBlocks != 6) {
        previousDataBlocks = Digi00xDuplexDataBlocksForPacket(previousIndex);
    }
    return (previousDBC + previousDataBlocks) & 0xffu;
}

bool
DigiLiveITPacketIndexFromCommandPtr(uint32_t commandPtr,
                                    uint32_t itDescriptorDMA,
                                    uint32_t * packetIndex)
{
    uint32_t descriptorAddress = commandPtr & ~0xfu;
    uint32_t packetStride =
        kDigi00xDuplexITDescriptorsPerPacket * static_cast<uint32_t>(sizeof(OHCIAsyncDescriptor));
    uint32_t ringBytes = kDigi00xDuplexITPacketCount * packetStride;
    if (descriptorAddress < itDescriptorDMA ||
        descriptorAddress >= itDescriptorDMA + ringBytes) {
        return false;
    }

    *packetIndex = (descriptorAddress - itDescriptorDMA) / packetStride;
    return true;
}

bool
DigiLiveIRPacketIndexFromCommandPtr(uint32_t commandPtr,
                                    uint32_t irDescriptorDMA,
                                    uint32_t * packetIndex)
{
    uint32_t descriptorAddress = commandPtr & ~0xfu;
    uint32_t packetStride =
        kDigi00xDuplexIRDescriptorsPerPacketStorage * static_cast<uint32_t>(sizeof(OHCIAsyncDescriptor));
    uint32_t ringBytes = kDigi00xDuplexIRDescriptorCount * packetStride;
    if (descriptorAddress < irDescriptorDMA ||
        descriptorAddress >= irDescriptorDMA + ringBytes) {
        return false;
    }

    *packetIndex = (descriptorAddress - irDescriptorDMA) / packetStride;
    return true;
}

void
ResetDigiLiveIRCommandPtrCursor()
{
    gDigiLiveITHardwareCursorValid = 0;
    gDigiLiveITHardwarePacketCursor = 0;
    gDigiLiveIRCommandPtrPacketIndex = 0xffffffff;
    gDigiLiveIRHardwareCursorValid = 0;
    gDigiLiveIRHardwarePacketCursor = 0;
    gDigiLiveIRSoftwarePacketCursor = 0;
    gDigiLiveIRBacklogPackets = 0;
    gDigiLiveIREmptyCatchUpLastFromIndex = 0xffffffff;
    gDigiLiveIREmptyCatchUpLastToIndex = 0xffffffff;
    gDigiLiveIREmptyCatchUpLastHardwareIndex = 0xffffffff;
    gDigiLiveIREmptyCatchUpLastSkippedPackets = 0;
    gDigiLiveIREmptyCatchUpLastScannedPackets = 0;
    gDigiLiveIRSegmentCatchUpLastFromIndex = 0xffffffff;
    gDigiLiveIRSegmentCatchUpLastToIndex = 0xffffffff;
    gDigiLiveIRSegmentCatchUpLastHardwareIndex = 0xffffffff;
    gDigiLiveIRSegmentCatchUpLastSkippedPackets = 0;
    gDigiLiveIRSegmentCatchUpLastScannedSegments = 0;
}

void
UpdateDigiLiveITCommandPtrCursor(uint32_t commandPtr, uint32_t itDescriptorDMA)
{
    uint32_t packetIndex = 0;
    if (!DigiLiveITPacketIndexFromCommandPtr(commandPtr, itDescriptorDMA, &packetIndex)) {
        gDigiLiveITHardwareCursorValid = 0;
        gDigiLiveOutputLastCurrentPacketIndex = 0xffffffff;
        return;
    }

    if (gDigiLiveITHardwareCursorValid == 0) {
        gDigiLiveITHardwarePacketCursor = packetIndex;
        gDigiLiveITHardwareCursorValid = 1;
    } else {
        uint32_t previousIndex =
            static_cast<uint32_t>(gDigiLiveITHardwarePacketCursor %
                                  kDigi00xDuplexITPacketCount);
        uint32_t delta =
            (packetIndex + kDigi00xDuplexITPacketCount - previousIndex) %
            kDigi00xDuplexITPacketCount;
        gDigiLiveITHardwarePacketCursor += delta;
    }

    gDigiLiveOutputLastCurrentPacketIndex = packetIndex;
}

void
ResetDigiLiveOutputState()
{
    gDigiLiveITHardwareCursorValid = 0;
    gDigiLiveITHardwarePacketCursor = 0;
    gDigiLiveOutputPacketCursorValid = 0;
    gDigiLiveOutputPacketCursor = 0;
    gDigiLiveOutputPushInProgress = 0;
    gDigiLiveOutputPushBusyCount = 0;
    gDigiLiveOutputPushAttemptCount = 0;
    gDigiLiveOutputPushSuccessCount = 0;
    gDigiLiveOutputWorkerPushAudioCount = 0;
    gDigiLiveOutputWorkerPushSkippedAudioCount = 0;
    gDigiLiveOutputPacketWriteCount = 0;
    gDigiLiveOutputFrameWriteCount = 0;
    gDigiLiveOutputSilentFrameWriteCount = 0;
    gDigiLiveOutputPlannedSilentFrameWriteCount = 0;
    gDigiLiveOutputAudioStartCount = 0;
    gDigiLiveOutputCursorCatchUpCount = 0;
    gDigiLiveOutputLastCurrentPacketIndex = 0xffffffff;
    gDigiLiveOutputLastStartPacketIndex = 0xffffffff;
    gDigiLiveOutputLastPacketCount = 0;
    gDigiLiveOutputLastFrameCount = 0;
    gDigiLiveOutputLastSilentFrameCount = 0;
    gDigiLiveOutputLastRingFillFrames = 0;
    gDigiLiveOutputLastStartDistancePackets = 0xffffffff;
    gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
    ResetDigiDotState(&gDigiLiveOutputDotState);
    gDigiLiveOutputDotResetCount++;
    gDigiLiveOutputDotLastInputWordBE = 0;
    gDigiLiveOutputDotLastOutputWordBE = 0;
    gDigiLiveOutputDotLastCarry = 0;
}

void
UpdateDigiLiveIRCommandPtrCursor(uint32_t commandPtr, uint32_t irDescriptorDMA)
{
    uint32_t packetIndex = 0;
    if (!DigiLiveIRPacketIndexFromCommandPtr(commandPtr, irDescriptorDMA, &packetIndex)) {
        gDigiLiveIRCommandPtrPacketIndex = 0xffffffff;
        gDigiLiveIRHardwareCursorValid = 0;
        gDigiLiveIRBacklogPackets = 0;
        return;
    }

    if (gDigiLiveIRHardwareCursorValid == 0) {
        gDigiLiveIRHardwarePacketCursor = packetIndex;
        gDigiLiveIRHardwareCursorValid = 1;
    } else {
        uint32_t previousIndex =
            static_cast<uint32_t>(gDigiLiveIRHardwarePacketCursor %
                                  kDigi00xDuplexIRDescriptorCount);
        uint32_t delta =
            (packetIndex + kDigi00xDuplexIRDescriptorCount - previousIndex) %
            kDigi00xDuplexIRDescriptorCount;
        gDigiLiveIRHardwarePacketCursor += delta;
    }

    gDigiLiveIRCommandPtrPacketIndex = packetIndex;
    if (gDigiLiveIRHardwarePacketCursor > gDigiLiveIRSoftwarePacketCursor) {
        uint64_t backlog = gDigiLiveIRHardwarePacketCursor - gDigiLiveIRSoftwarePacketCursor;
        gDigiLiveIRBacklogPackets = backlog > 0xffffffffull
            ? 0xffffffffu
            : static_cast<uint32_t>(backlog);
    } else {
        gDigiLiveIRBacklogPackets = 0;
    }
}

bool
DigiLiveReceivePacketAtIndexIsEmpty(volatile OHCIAsyncDescriptor * ring,
                                    uint64_t bufferAddress,
                                    uint32_t index)
{
    DigiLiveReceivePacket packet =
        DigiLiveReceivePacketAt(ring, bufferAddress, index);
    volatile OHCIAsyncDescriptor * packetDescriptor = packet.descriptor;
    uint32_t payloadResCount = kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? packetDescriptor[0].resCount
        : packetDescriptor[1].resCount;
    uint32_t headerStatus = kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? 0
        : packetDescriptor[0].transferStatus;
    uint32_t payloadStatus = kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? packetDescriptor[0].transferStatus
        : packetDescriptor[1].transferStatus;
    uint32_t descriptorBytes = DigiLiveReceiveDescriptorBytes(payloadResCount);
    return DigiLiveReceiveDescriptorIsEmpty(descriptorBytes, headerStatus, payloadStatus);
}

bool
DigiLiveReceiveSegmentAtCursorIsReady(volatile OHCIAsyncDescriptor * ring,
                                      uint64_t bufferAddress,
                                      uint64_t startCursor,
                                      uint32_t packetCount)
{
    if (packetCount == 0) {
        return false;
    }

    for (uint32_t offset = 0; offset < packetCount; ++offset) {
        uint32_t index =
            static_cast<uint32_t>((startCursor + offset) %
                                  kDigi00xDuplexIRDescriptorCount);
        if (DigiLiveReceivePacketAtIndexIsEmpty(ring, bufferAddress, index)) {
            return false;
        }
    }
    return true;
}

void
UpdateDigiLiveIRBacklogAfterSoftwareCursor()
{
    if (gDigiLiveIRHardwarePacketCursor > gDigiLiveIRSoftwarePacketCursor) {
        uint64_t backlog = gDigiLiveIRHardwarePacketCursor - gDigiLiveIRSoftwarePacketCursor;
        gDigiLiveIRBacklogPackets = backlog > 0xffffffffull
            ? 0xffffffffu
            : static_cast<uint32_t>(backlog);
    } else {
        gDigiLiveIRBacklogPackets = 0;
    }
}

bool
CatchUpDigiLiveIRReadIndexToReadySegment(volatile OHCIAsyncDescriptor * irDescriptor,
                                         uint64_t bufferAddress)
{
    if (kDigiLiveIRSegmentCatchUpEnabled == 0 ||
        kDigiLiveIRSegmentCatchUpPacketCount == 0 ||
        kDigiLiveIRCommandPtrCatchUpScanEnabled == 0 ||
        irDescriptor == nullptr ||
        bufferAddress == 0 ||
        gDigiLiveIRBacklogPackets < kDigiLiveIRSegmentCatchUpPacketCount) {
        return false;
    }

    gDigiLiveIRSegmentCatchUpAttemptCount++;
    uint32_t fromIndex = gDigiLiveIRReadIndex;
    uint32_t scanLimit = gDigiLiveIRBacklogPackets;
    if (scanLimit > kDigiLiveIRCommandPtrCatchUpScanMaxPackets) {
        scanLimit = kDigiLiveIRCommandPtrCatchUpScanMaxPackets;
    }
    if (scanLimit < kDigiLiveIRSegmentCatchUpPacketCount) {
        gDigiLiveIRSegmentCatchUpFailureCount++;
        return false;
    }

    uint32_t scannedSegments = 0;
    uint32_t maxDistance = scanLimit - kDigiLiveIRSegmentCatchUpPacketCount + 1;
    gDigiLiveIREmptyCatchUpScanCount++;
    for (uint32_t distance = 1; distance <= maxDistance; ++distance) {
        uint64_t candidateCursor = gDigiLiveIRSoftwarePacketCursor + distance;
        scannedSegments++;
        if (!DigiLiveReceiveSegmentAtCursorIsReady(irDescriptor,
                                                   bufferAddress,
                                                   candidateCursor,
                                                   kDigiLiveIRSegmentCatchUpPacketCount)) {
            continue;
        }

        uint32_t candidateIndex =
            static_cast<uint32_t>(candidateCursor % kDigi00xDuplexIRDescriptorCount);
        gDigiLiveIRSoftwarePacketCursor = candidateCursor;
        gDigiLiveIRReadIndex = candidateIndex;
        UpdateDigiLiveIRBacklogAfterSoftwareCursor();

        gDigiLiveIREmptyCatchUpCount++;
        gDigiLiveIREmptyCatchUpSkippedPackets += distance;
        gDigiLiveIREmptyCatchUpScanFoundCount++;
        gDigiLiveIREmptyCatchUpScanPackets += scannedSegments;
        gDigiLiveIREmptyCatchUpLastFromIndex = fromIndex;
        gDigiLiveIREmptyCatchUpLastToIndex = candidateIndex;
        gDigiLiveIREmptyCatchUpLastHardwareIndex = gDigiLiveIRCommandPtrPacketIndex;
        gDigiLiveIREmptyCatchUpLastSkippedPackets = distance;
        gDigiLiveIREmptyCatchUpLastScannedPackets = scannedSegments;

        gDigiLiveIRSegmentCatchUpSuccessCount++;
        gDigiLiveIRSegmentCatchUpScannedSegments += scannedSegments;
        gDigiLiveIRSegmentCatchUpLastFromIndex = fromIndex;
        gDigiLiveIRSegmentCatchUpLastToIndex = candidateIndex;
        gDigiLiveIRSegmentCatchUpLastHardwareIndex = gDigiLiveIRCommandPtrPacketIndex;
        gDigiLiveIRSegmentCatchUpLastSkippedPackets = distance;
        gDigiLiveIRSegmentCatchUpLastScannedSegments = scannedSegments;
        return true;
    }

    gDigiLiveIRSegmentCatchUpFailureCount++;
    gDigiLiveIRSegmentCatchUpScannedSegments += scannedSegments;
    gDigiLiveIRSegmentCatchUpLastFromIndex = fromIndex;
    gDigiLiveIRSegmentCatchUpLastToIndex = 0xffffffff;
    gDigiLiveIRSegmentCatchUpLastHardwareIndex = gDigiLiveIRCommandPtrPacketIndex;
    gDigiLiveIRSegmentCatchUpLastSkippedPackets = 0;
    gDigiLiveIRSegmentCatchUpLastScannedSegments = scannedSegments;
    return false;
}

bool
CatchUpDigiLiveIRReadIndexAfterEmptyDescriptor(volatile OHCIAsyncDescriptor * irDescriptor,
                                               uint64_t bufferAddress)
{
    if (kDigiLiveIRCommandPtrCatchUpEnabled == 0 ||
        gDigiLiveIRHardwareCursorValid == 0 ||
        gDigiLiveIRBacklogPackets < kDigiLiveIRCommandPtrCatchUpMinPackets) {
        return false;
    }

    if (CatchUpDigiLiveIRReadIndexToReadySegment(irDescriptor, bufferAddress)) {
        return true;
    }
    if (kDigiLiveIRSegmentCatchUpEnabled != 0 &&
        kDigiLiveIRSegmentCatchUpFallbackToSinglePacket == 0) {
        return false;
    }

    uint32_t fromIndex = gDigiLiveIRReadIndex;
    uint32_t skipped = gDigiLiveIRBacklogPackets;
    uint32_t scanned = 0;

    if (kDigiLiveIRCommandPtrCatchUpScanEnabled != 0 &&
        irDescriptor != nullptr &&
        bufferAddress != 0) {
        uint32_t scanLimit = gDigiLiveIRBacklogPackets;
        if (scanLimit > kDigiLiveIRCommandPtrCatchUpScanMaxPackets) {
            scanLimit = kDigiLiveIRCommandPtrCatchUpScanMaxPackets;
        }
        if (scanLimit > 1) {
            gDigiLiveIREmptyCatchUpScanCount++;
            for (uint32_t distance = 1; distance < scanLimit; ++distance) {
                uint64_t candidateCursor = gDigiLiveIRSoftwarePacketCursor + distance;
                uint32_t candidateIndex =
                    static_cast<uint32_t>(candidateCursor %
                                          kDigi00xDuplexIRDescriptorCount);
                scanned++;
                if (!DigiLiveReceivePacketAtIndexIsEmpty(irDescriptor,
                                                         bufferAddress,
                                                         candidateIndex)) {
                    gDigiLiveIRSoftwarePacketCursor = candidateCursor;
                    gDigiLiveIRReadIndex = candidateIndex;
                    skipped = distance;
                    UpdateDigiLiveIRBacklogAfterSoftwareCursor();
                    gDigiLiveIREmptyCatchUpCount++;
                    gDigiLiveIREmptyCatchUpSkippedPackets += skipped;
                    gDigiLiveIREmptyCatchUpScanFoundCount++;
                    gDigiLiveIREmptyCatchUpScanPackets += scanned;
                    gDigiLiveIREmptyCatchUpLastFromIndex = fromIndex;
                    gDigiLiveIREmptyCatchUpLastToIndex = candidateIndex;
                    gDigiLiveIREmptyCatchUpLastHardwareIndex = gDigiLiveIRCommandPtrPacketIndex;
                    gDigiLiveIREmptyCatchUpLastSkippedPackets = skipped;
                    gDigiLiveIREmptyCatchUpLastScannedPackets = scanned;
                    return true;
                }
            }
            gDigiLiveIREmptyCatchUpScanPackets += scanned;
        }
    }

    uint32_t toIndex =
        static_cast<uint32_t>(gDigiLiveIRHardwarePacketCursor %
                              kDigi00xDuplexIRDescriptorCount);
    gDigiLiveIRSoftwarePacketCursor = gDigiLiveIRHardwarePacketCursor;
    gDigiLiveIRReadIndex = toIndex;
    UpdateDigiLiveIRBacklogAfterSoftwareCursor();
    gDigiLiveIREmptyCatchUpCount++;
    gDigiLiveIREmptyCatchUpSkippedPackets += skipped;
    gDigiLiveIREmptyCatchUpLastFromIndex = fromIndex;
    gDigiLiveIREmptyCatchUpLastToIndex = toIndex;
    gDigiLiveIREmptyCatchUpLastHardwareIndex = gDigiLiveIRCommandPtrPacketIndex;
    gDigiLiveIREmptyCatchUpLastSkippedPackets = skipped;
    gDigiLiveIREmptyCatchUpLastScannedPackets = scanned;
    return true;
}

kern_return_t
SyncDigiLiveTransmitPacketRange(uint32_t startPacket, uint32_t packetCount)
{
    uint32_t packetStride =
        kDigi00xDuplexITDescriptorsPerPacket * static_cast<uint32_t>(sizeof(OHCIAsyncDescriptor));
    uint64_t descriptorOffset =
        kDigi00xDuplexITDescriptorOffset + static_cast<uint64_t>(startPacket) * packetStride;
    uint64_t descriptorBytes = static_cast<uint64_t>(packetCount) * packetStride;
    kern_return_t ret = SyncDMABufferForDeviceRange(&gDigiLiveBuffer, descriptorOffset, descriptorBytes);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    uint64_t headerOffset =
        kDigi00xDuplexITHeaderStorageOffset + static_cast<uint64_t>(startPacket) * 8u;
    uint64_t headerBytes = static_cast<uint64_t>(packetCount) * 8u;
    return SyncDMABufferForDeviceRange(&gDigiLiveBuffer, headerOffset, headerBytes);
}

kern_return_t
SyncDigiLiveTransmitReplayRange(uint32_t startPacket, uint32_t packetCount)
{
    if (startPacket + packetCount <= kDigi00xDuplexITPacketCount) {
        return SyncDigiLiveTransmitPacketRange(startPacket, packetCount);
    }

    uint32_t firstCount = kDigi00xDuplexITPacketCount - startPacket;
    kern_return_t ret = SyncDigiLiveTransmitPacketRange(startPacket, firstCount);
    if (ret != kIOReturnSuccess) {
        return ret;
    }
    return SyncDigiLiveTransmitPacketRange(0, packetCount - firstCount);
}

kern_return_t
SyncDigiLiveTransmitPayloadPacketRange(uint32_t startPacket, uint32_t packetCount)
{
    if (packetCount == 0) {
        return kIOReturnSuccess;
    }

    uint64_t payloadOffset =
        kDigi00xDuplexITPayloadOffset +
        static_cast<uint64_t>(startPacket) * kDigiLiveITPayloadStrideBytes;
    uint64_t payloadBytes =
        static_cast<uint64_t>(packetCount) * kDigiLiveITPayloadStrideBytes;
    return SyncDMABufferForDeviceRange(&gDigiLiveBuffer, payloadOffset, payloadBytes);
}

kern_return_t
SyncDigiLiveTransmitPayloadReplayRange(uint32_t startPacket, uint32_t packetCount)
{
    if (packetCount == 0) {
        return kIOReturnSuccess;
    }
    if (startPacket + packetCount <= kDigi00xDuplexITPacketCount) {
        return SyncDigiLiveTransmitPayloadPacketRange(startPacket, packetCount);
    }

    uint32_t firstCount = kDigi00xDuplexITPacketCount - startPacket;
    kern_return_t ret = SyncDigiLiveTransmitPayloadPacketRange(startPacket, firstCount);
    if (ret != kIOReturnSuccess) {
        return ret;
    }
    return SyncDigiLiveTransmitPayloadPacketRange(0, packetCount - firstCount);
}

bool
PopAudioOutputRingFrame(int32_t samples[kAudioOutputChannelCount])
{
    if (samples == nullptr || gAudioOutputRingReadFrame >= gAudioOutputRingWriteFrame) {
        return false;
    }

    uint32_t ringFrame =
        static_cast<uint32_t>(gAudioOutputRingReadFrame %
                              kAudioOutputRingBufferFrameCount);
    for (uint32_t channel = 0; channel < kAudioOutputChannelCount; ++channel) {
        samples[channel] = gAudioOutputRingPCM[ringFrame][channel];
    }
    gAudioOutputRingReadFrame++;
    gAudioOutputRingConsumedFrames++;
    return true;
}

uint32_t
AudioOutputSampleToAM824WordBE(int32_t sample)
{
    uint32_t value24 = static_cast<uint32_t>(sample >> 8) & 0x00ffffffu;
    return 0x40000000u | value24;
}

void
WriteDigiLiveOutputTransmitDataBlock(volatile uint32_t * payload,
                                     const int32_t samples[kAudioOutputChannelCount])
{
    payload[0] = ToBigEndian32(PopDigiLiveMidiEchoWordBE());
    for (uint32_t channel = 0; channel < kDigi00xDuplexPCMAudioChannels; ++channel) {
        int32_t sample = channel < kAudioOutputChannelCount ? samples[channel] : 0;
        uint32_t wordBE = AudioOutputSampleToAM824WordBE(sample);
        uint32_t encodedWordBE = DigiDotEncodeAM824Word(&gDigiLiveOutputDotState, wordBE);
        payload[1 + channel] = ToBigEndian32(encodedWordBE);
    }
}

struct ScopedDigiLiveOutputPushGuard {
    bool acquired;

    ScopedDigiLiveOutputPushGuard()
        : acquired(__sync_lock_test_and_set(&gDigiLiveOutputPushInProgress, 1) == 0)
    {
    }

    ~ScopedDigiLiveOutputPushGuard()
    {
        if (acquired) {
            __sync_lock_release(&gDigiLiveOutputPushInProgress);
        }
    }
};

kern_return_t
PushAudioOutputToDigiLiveTransmit()
{
    if (kAudioOutputStreamEnabled == 0 ||
        kDigiLiveOutputPayloadUpdateEnabled == 0 ||
        gDigiLiveRunning == 0 ||
        gDigiLiveBuffer.cpuRange.address == 0 ||
        gDigiLiveBuffer.command == nullptr ||
        gPCIDevice == nullptr ||
        gPCIMemoryIndex == 0xff) {
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    ScopedDigiLiveOutputPushGuard pushGuard;
    if (!pushGuard.acquired) {
        gDigiLiveOutputPushBusyCount++;
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnBusy);
        return kIOReturnBusy;
    }

    gDigiLiveOutputPushAttemptCount++;
    gDigiLiveOutputLastPacketCount = 0;
    gDigiLiveOutputLastFrameCount = 0;
    gDigiLiveOutputLastSilentFrameCount = 0;
    uint32_t ringFillFrames = AudioOutputRingFillFrames();
    gDigiLiveOutputLastRingFillFrames = ringFillFrames;
    bool audioJustStarted = false;
    bool audioReady = false;
    uint32_t frameBudget = 0;
    if (ringFillFrames == 0) {
        gAudioOutputRingPrebuffered = 0;
    } else if (gAudioOutputRingPrebuffered == 0) {
        if (ringFillFrames < kAudioOutputRingPrebufferFrames) {
            gAudioOutputRingPrebufferHoldCount++;
        } else {
            gAudioOutputRingPrebuffered = 1;
            gAudioOutputRingPrebufferReadyCount++;
            gDigiLiveOutputAudioStartCount++;
            audioJustStarted = true;
        }
    }
    if (gAudioOutputRingPrebuffered != 0 && ringFillFrames > kAudioOutputRingKeepFrames) {
        audioReady = true;
        frameBudget = ringFillFrames - kAudioOutputRingKeepFrames;
    } else if (ringFillFrames != 0) {
        gAudioOutputRingPrebufferHoldCount++;
    }

    uint32_t dmaBase = static_cast<uint32_t>(gDigiLiveBuffer.dmaSegment.address);
    uint32_t itDescriptorDMA = dmaBase + kDigi00xDuplexITDescriptorOffset;
    gPCIDevice->MemoryRead32(gPCIMemoryIndex,
                             OhciIsoXmitCommandPtrOffset(kDigi00xDuplexContextIndex),
                             &gDigiLiveITCommandPtrLastRead);
    UpdateDigiLiveITCommandPtrCursor(gDigiLiveITCommandPtrLastRead, itDescriptorDMA);
    if (gDigiLiveITHardwareCursorValid == 0) {
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnBadArgument);
        return kIOReturnBadArgument;
    }

    uint64_t safeStartCursor =
        gDigiLiveITHardwarePacketCursor + kDigiLiveOutputLeadPackets;
    uint64_t safeEndCursor =
        gDigiLiveITHardwarePacketCursor +
        kDigi00xDuplexITPacketCount -
        kDigiLiveOutputLeadPackets;
    uint32_t targetAheadPackets = audioReady ?
        kDigiLiveOutputServiceAheadPackets :
        kDigiLiveOutputSilenceAheadPackets;
    uint64_t targetEndCursor = safeStartCursor + targetAheadPackets;
    if (targetEndCursor > safeEndCursor) {
        targetEndCursor = safeEndCursor;
    }
    if (gDigiLiveOutputPacketCursorValid == 0 ||
        gDigiLiveOutputPacketCursor < safeStartCursor ||
        audioJustStarted) {
        if (gDigiLiveOutputPacketCursorValid != 0 &&
            gDigiLiveOutputPacketCursor < safeStartCursor) {
            gDigiLiveOutputCursorCatchUpCount++;
        }
        gDigiLiveOutputPacketCursor = safeStartCursor;
        gDigiLiveOutputPacketCursorValid = 1;
        ResetDigiDotState(&gDigiLiveOutputDotState);
        gDigiLiveOutputDotResetCount++;
    }
    if (gDigiLiveOutputPacketCursor >= safeEndCursor) {
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnBusy);
        return kIOReturnBusy;
    }
    if (gDigiLiveOutputPacketCursor >= targetEndCursor) {
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    uint32_t writablePackets =
        static_cast<uint32_t>(targetEndCursor - gDigiLiveOutputPacketCursor);
    if (writablePackets > kDigiLiveOutputMaxPacketsPerPush) {
        writablePackets = kDigiLiveOutputMaxPacketsPerPush;
    }
    if (writablePackets == 0) {
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnBusy);
        return kIOReturnBusy;
    }

    volatile uint32_t * itPayloadStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiLiveBuffer.cpuRange.address +
                                              kDigi00xDuplexITPayloadOffset);
    uint64_t startCursor = gDigiLiveOutputPacketCursor;
    uint32_t startPacket =
        static_cast<uint32_t>(startCursor % kDigi00xDuplexITPacketCount);
    uint32_t currentPacket =
        static_cast<uint32_t>(gDigiLiveITHardwarePacketCursor %
                              kDigi00xDuplexITPacketCount);
    uint32_t startDistance =
        static_cast<uint32_t>((startPacket + kDigi00xDuplexITPacketCount -
                               currentPacket) %
                              kDigi00xDuplexITPacketCount);

    uint32_t writtenPackets = 0;
    uint32_t writtenFrames = 0;
    uint32_t underrunSilentFrames = 0;
    uint32_t plannedSilentFrames = 0;
    for (uint32_t packetOffset = 0; packetOffset < writablePackets; ++packetOffset) {
        uint32_t packetIndex =
            static_cast<uint32_t>((gDigiLiveOutputPacketCursor + packetOffset) %
                                  kDigi00xDuplexITPacketCount);
        uint32_t dataBlocks = gDigiLiveTxDataBlocks[packetIndex];
        if (dataBlocks != 5 && dataBlocks != 6) {
            dataBlocks = Digi00xDuplexDataBlocksForPacket(packetIndex);
        }
        if (audioReady && frameBudget < dataBlocks) {
            break;
        }

        volatile uint32_t * payload =
            itPayloadStorage +
            ((packetIndex * kDigiLiveITPayloadStrideBytes) / sizeof(uint32_t));
        for (uint32_t block = 0; block < dataBlocks; ++block) {
            if (audioReady) {
                int32_t frameSamples[kAudioOutputChannelCount] = {};
                if (PopAudioOutputRingFrame(frameSamples)) {
                    WriteDigiLiveOutputTransmitDataBlock(payload, frameSamples);
                    writtenFrames++;
                } else {
                    if (underrunSilentFrames == 0) {
                        ResetDigiDotState(&gDigiLiveOutputDotState);
                        gDigiLiveOutputDotResetCount++;
                    }
                    WriteDigiLiveSilentTransmitDataBlock(payload);
                    underrunSilentFrames++;
                }
            } else {
                WriteDigiLiveSilentTransmitDataBlock(payload);
                plannedSilentFrames++;
            }
            payload += kDigi00xDuplexDataBlockQuadlets;
        }

        writtenPackets++;
        if (audioReady) {
            frameBudget -= dataBlocks;
        }
    }

    if (writtenPackets == 0) {
        gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    __sync_synchronize();
    kern_return_t syncRet =
        SyncDigiLiveTransmitPayloadReplayRange(startPacket, writtenPackets);
    gDigiLiveOutputLastSyncRet = ReturnCodeToProperty(syncRet);
    gDigiLiveOutputLastStartPacketIndex = startPacket;
    gDigiLiveOutputLastStartDistancePackets = startDistance;
    gDigiLiveOutputLastPacketCount = writtenPackets;
    gDigiLiveOutputLastFrameCount = writtenFrames;
    gDigiLiveOutputLastSilentFrameCount = plannedSilentFrames + underrunSilentFrames;
    gAudioOutputRingLastConsumeFrames = writtenFrames;
    gAudioOutputRingLastConsumeUnderrunFrames = underrunSilentFrames;
    gAudioOutputRingUnderrunFrames += underrunSilentFrames;
    UpdateAudioOutputRingFill();
    if (syncRet != kIOReturnSuccess) {
        return syncRet;
    }

    gDigiLiveOutputPacketCursor += writtenPackets;
    gDigiLiveOutputPushSuccessCount++;
    gDigiLiveOutputPacketWriteCount += writtenPackets;
    gDigiLiveOutputFrameWriteCount += writtenFrames;
    gDigiLiveOutputSilentFrameWriteCount += plannedSilentFrames + underrunSilentFrames;
    gDigiLiveOutputPlannedSilentFrameWriteCount += plannedSilentFrames;
    uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlSet, kContextWake);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, itControlSet, &gDigiLiveITControlAfterRun);
    return kIOReturnSuccess;
}

uint32_t
PeekDigiLiveMovingReplayQueueTotal(uint32_t packetCount)
{
    uint32_t total = 0;
    uint32_t index = gDigiLiveSequenceReplayMovingQueueReadIndex;
    for (uint32_t i = 0; i < packetCount; ++i) {
        total += gDigiLiveSequenceReplayMovingQueue[index];
        index = (index + 1) % kDigiLiveSequenceReplayMovingQueuePackets;
    }
    return total;
}

bool
FindDigiLiveMovingReplayQueueBestPhase(uint32_t packetCount,
                                       uint32_t * bestPhase,
                                       uint32_t * bestMismatchCount)
{
    if (packetCount == 0 ||
        packetCount > gDigiLiveSequenceReplayMovingQueueCount ||
        bestPhase == nullptr ||
        bestMismatchCount == nullptr) {
        return false;
    }

    uint32_t bestMismatch = 0xffffffff;
    uint32_t phaseCandidate = 0xffffffff;
    for (uint32_t phase = 0; phase < kDigiLiveRxCadencePeriodPackets; ++phase) {
        uint32_t mismatchCount = 0;
        uint32_t index = gDigiLiveSequenceReplayMovingQueueReadIndex;
        for (uint32_t packet = 0; packet < packetCount; ++packet) {
            uint32_t idealPacket = (packet + phase) % kDigiLiveRxCadencePeriodPackets;
            uint32_t dataBlocks = gDigiLiveSequenceReplayMovingQueue[index];
            if (dataBlocks != Digi00xDuplexDataBlocksForPacket(idealPacket)) {
                mismatchCount++;
            }
            index = (index + 1) % kDigiLiveSequenceReplayMovingQueuePackets;
        }
        if (mismatchCount < bestMismatch) {
            bestMismatch = mismatchCount;
            phaseCandidate = phase;
        }
    }

    *bestPhase = phaseCandidate;
    *bestMismatchCount = bestMismatch;
    return phaseCandidate < kDigiLiveRxCadencePeriodPackets;
}

uint32_t
PopDigiLiveMovingReplayQueue()
{
    if (gDigiLiveSequenceReplayMovingQueueCount == 0) {
        return 0;
    }

    uint32_t dataBlocks =
        gDigiLiveSequenceReplayMovingQueue[gDigiLiveSequenceReplayMovingQueueReadIndex];
    gDigiLiveSequenceReplayMovingQueueReadIndex =
        (gDigiLiveSequenceReplayMovingQueueReadIndex + 1) %
        kDigiLiveSequenceReplayMovingQueuePackets;
    gDigiLiveSequenceReplayMovingQueueCount--;
    return dataBlocks;
}

kern_return_t
RefreshDigiLiveMovingSequenceReplay()
{
    if (kDigiLiveSequenceReplayMovingEnabled == 0 ||
        gDigiLiveRunning == 0 ||
        gDigiLiveBuffer.cpuRange.address == 0 ||
        gDigiLiveBuffer.command == nullptr ||
        gPCIDevice == nullptr ||
        gPCIMemoryIndex == 0xff) {
        gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    gDigiLiveSequenceReplayMovingUpdateAttemptCount++;
    gDigiLiveSequenceReplayMovingLastUpdatePackets = 0;
    if (gDigiLiveSequenceReplayMovingQueueCount < kDigiLiveSequenceReplayMovingUpdatePackets) {
        gDigiLiveSequenceReplayMovingShortQueueCount++;
        gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    uint32_t rawTotalDataBlocks =
        PeekDigiLiveMovingReplayQueueTotal(kDigiLiveSequenceReplayMovingUpdatePackets);
    gDigiLiveSequenceReplayMovingLastRawTotalDataBlocks = rawTotalDataBlocks;
    gDigiLiveSequenceReplayMovingLastCadencePhase = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastCadenceMismatchCount = 0xffffffff;
    gDigiLiveSequenceReplayMovingLastCadenceSource = 0;

    bool useCadencePhase = kDigiLiveSequenceReplayMovingUseCadencePhase != 0;
    uint32_t cadencePhase = 0xffffffff;
    uint32_t cadenceMismatchCount = 0xffffffff;
    uint32_t cadenceSource = 0;
    uint32_t totalDataBlocks = rawTotalDataBlocks;
    if (useCadencePhase) {
        if (gDigiLiveRxCadenceReady != 0 &&
            gDigiLiveRxCadenceBestPhase < kDigiLiveRxCadencePeriodPackets) {
            cadencePhase = gDigiLiveRxCadenceBestPhase;
            cadenceMismatchCount = gDigiLiveRxCadenceBestPhaseMismatchCount;
            cadenceSource = 1;
        }

        if (cadenceSource == 0 &&
            kDigiLiveSequenceReplayMovingLearnCadenceFromQueue != 0 &&
            rawTotalDataBlocks == DigiLiveSequenceReplayPeriodDataBlocks()) {
            uint32_t queuePhase = 0xffffffff;
            uint32_t queueMismatchCount = 0xffffffff;
            if (FindDigiLiveMovingReplayQueueBestPhase(kDigiLiveSequenceReplayMovingUpdatePackets,
                                                       &queuePhase,
                                                       &queueMismatchCount)) {
                if (kDigiLiveSequenceReplayMovingRequireCadenceMismatchZero == 0 ||
                    queueMismatchCount == 0) {
                    cadencePhase = queuePhase;
                    cadenceMismatchCount = queueMismatchCount;
                    cadenceSource = 2;
                    gDigiLiveSequenceReplayMovingCachedCadencePhase = queuePhase;
                    gDigiLiveSequenceReplayMovingCachedCadenceMismatchCount =
                        queueMismatchCount;
                    gDigiLiveSequenceReplayMovingCadenceLearnCount++;
                    gDigiLiveSequenceReplayMovingCadenceLearnPacketCount +=
                        kDigiLiveSequenceReplayMovingUpdatePackets;
                } else {
                    gDigiLiveSequenceReplayMovingCadenceLearnRejectCount++;
                }
            }
        }

        if (cadenceSource == 0 &&
            kDigiLiveSequenceReplayMovingAllowCachedCadencePhase != 0 &&
            gDigiLiveSequenceReplayMovingCachedCadencePhase <
                kDigiLiveRxCadencePeriodPackets) {
            cadencePhase = gDigiLiveSequenceReplayMovingCachedCadencePhase;
            cadenceMismatchCount =
                gDigiLiveSequenceReplayMovingCachedCadenceMismatchCount;
            cadenceSource = 3;
            gDigiLiveSequenceReplayMovingCadenceCachedUseCount++;
        }

        gDigiLiveSequenceReplayMovingLastCadencePhase = cadencePhase;
        gDigiLiveSequenceReplayMovingLastCadenceMismatchCount = cadenceMismatchCount;
        gDigiLiveSequenceReplayMovingLastCadenceSource = cadenceSource;
        if (cadenceSource == 0) {
            gDigiLiveSequenceReplayMovingCadenceNotReadyCount++;
            gDigiLiveSequenceReplayMovingLastTotalDataBlocks = 0;
            gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnNotReady);
            return kIOReturnNotReady;
        }
        if (kDigiLiveSequenceReplayMovingRequireCadenceMismatchZero != 0 &&
            cadenceMismatchCount != 0) {
            gDigiLiveSequenceReplayMovingCadenceMismatchRejectCount++;
            gDigiLiveSequenceReplayMovingLastTotalDataBlocks = 0;
            gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnBadArgument);
            return kIOReturnBadArgument;
        }
        totalDataBlocks = DigiLiveSequenceReplayPeriodDataBlocks();
    }
    gDigiLiveSequenceReplayMovingLastTotalDataBlocks = totalDataBlocks;
    if (!useCadencePhase &&
        totalDataBlocks != DigiLiveSequenceReplayPeriodDataBlocks()) {
        gDigiLiveSequenceReplayMovingBadTotalCount++;
        ClearDigiLiveMovingReplayQueue();
        gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnBadArgument);
        return kIOReturnBadArgument;
    }

    uint32_t dmaBase = static_cast<uint32_t>(gDigiLiveBuffer.dmaSegment.address);
    uint32_t itDescriptorDMA = dmaBase + kDigi00xDuplexITDescriptorOffset;
    uint32_t currentPacketIndex = 0;
    gPCIDevice->MemoryRead32(gPCIMemoryIndex,
                             OhciIsoXmitCommandPtrOffset(kDigi00xDuplexContextIndex),
                             &gDigiLiveITCommandPtrLastRead);
    if (!DigiLiveITPacketIndexFromCommandPtr(gDigiLiveITCommandPtrLastRead,
                                             itDescriptorDMA,
                                             &currentPacketIndex)) {
        gDigiLiveSequenceReplayMovingBadCommandPtrCount++;
        gDigiLiveSequenceReplayMovingLastCurrentPacketIndex = 0xffffffff;
        gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnBadArgument);
        return kIOReturnBadArgument;
    }

    volatile OHCIAsyncDescriptor * itDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexITDescriptorOffset);
    volatile uint32_t * itHeaderStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiLiveBuffer.cpuRange.address +
                                              kDigi00xDuplexITHeaderStorageOffset);
    uint32_t updateStart =
        (currentPacketIndex + kDigiLiveSequenceReplayMovingLeadPackets) %
        kDigi00xDuplexITPacketCount;
    uint32_t startDistance =
        (updateStart + kDigi00xDuplexITPacketCount - currentPacketIndex) %
        kDigi00xDuplexITPacketCount;
    uint32_t endDistance = startDistance + kDigiLiveSequenceReplayMovingUpdatePackets - 1;
    bool windowWrapsHardware = endDistance >= kDigi00xDuplexITPacketCount;
    bool guardWouldWrite =
        kDigiLiveSequenceReplayMovingLiveWriteGuardEnabled == 0 ||
        (!windowWrapsHardware &&
         startDistance >= kDigiLiveSequenceReplayMovingGuardMinStartDistancePackets);
    gDigiLiveSequenceReplayMovingLastStartDistancePackets = startDistance;
    gDigiLiveSequenceReplayMovingLastEndDistancePackets =
        windowWrapsHardware ? (endDistance % kDigi00xDuplexITPacketCount) : endDistance;
    gDigiLiveSequenceReplayMovingLastWindowWrapsHardware =
        windowWrapsHardware ? 1u : 0u;
    gDigiLiveSequenceReplayMovingLastGuardWouldWrite = guardWouldWrite ? 1u : 0u;
    if (kDigiLiveSequenceReplayMovingLiveWriteGuardEnabled != 0) {
        if (guardWouldWrite) {
            gDigiLiveSequenceReplayMovingGuardEligibleCount++;
        } else {
            gDigiLiveSequenceReplayMovingGuardRejectCount++;
        }
        if (kDigiLiveSequenceReplayMovingDryRunEnabled != 0) {
            if (guardWouldWrite) {
                gDigiLiveSequenceReplayMovingGuardDryRunWouldWriteCount++;
            } else {
                gDigiLiveSequenceReplayMovingGuardDryRunWouldRejectCount++;
            }
        } else if (!guardWouldWrite) {
            gDigiLiveSequenceReplayMovingLastUpdatePackets = 0;
            gDigiLiveSequenceReplayMovingLastSyncRet =
                ReturnCodeToProperty(kIOReturnNotReady);
            return kIOReturnNotReady;
        }
    }

    uint32_t dataBlockCounter = DigiLiveTransmitDBCForPacket(itHeaderStorage, updateStart);
    gDigiLiveSequenceReplayMovingLastStartDBC = dataBlockCounter;

    gDigiLiveSequenceReplayMovingLastCurrentPacketIndex = currentPacketIndex;
    gDigiLiveSequenceReplayMovingLastUpdateStartIndex = updateStart;
    gDigiLiveSequenceReplayMovingLastUpdatePackets = kDigiLiveSequenceReplayMovingUpdatePackets;

    uint32_t calculatedTotalDataBlocks = 0;
    for (uint32_t i = 0; i < kDigiLiveSequenceReplayMovingUpdatePackets; ++i) {
        uint32_t packetIndex = (updateStart + i) % kDigi00xDuplexITPacketCount;
        uint32_t rawDataBlocks = PopDigiLiveMovingReplayQueue();
        if (rawDataBlocks != 5 && rawDataBlocks != 6) {
            gDigiLiveSequenceReplayMovingInvalidCount++;
            ClearDigiLiveMovingReplayQueue();
            gDigiLiveSequenceReplayMovingLastUpdatePackets = i;
            gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnBadArgument);
            return kIOReturnBadArgument;
        }

        uint32_t dataBlocks = rawDataBlocks;
        if (useCadencePhase) {
            dataBlocks = Digi00xDuplexDataBlocksForPacket(packetIndex + cadencePhase);
        }
        calculatedTotalDataBlocks += dataBlocks;

        if (kDigiLiveSequenceReplayMovingDryRunEnabled == 0) {
            UpdateDigiLiveTransmitPacketDescriptor(itDescriptor,
                                                   itHeaderStorage,
                                                   packetIndex,
                                                   dataBlocks,
                                                   gDigiLiveSourceNodeIDField,
                                                   dataBlockCounter);
        }
        dataBlockCounter = (dataBlockCounter + dataBlocks) & 0xffu;
    }
    gDigiLiveSequenceReplayMovingLastTotalDataBlocks = calculatedTotalDataBlocks;
    if (calculatedTotalDataBlocks != DigiLiveSequenceReplayPeriodDataBlocks()) {
        gDigiLiveSequenceReplayMovingBadTotalCount++;
        ClearDigiLiveMovingReplayQueue();
        gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnBadArgument);
        return kIOReturnBadArgument;
    }
    gDigiLiveSequenceReplayMovingLastEndDBC = dataBlockCounter;
    if (useCadencePhase) {
        gDigiLiveSequenceReplayMovingCadencePhaseUseCount++;
        gDigiLiveSequenceReplayMovingCadencePhasePacketCount +=
            kDigiLiveSequenceReplayMovingUpdatePackets;
    }

    if (kDigiLiveSequenceReplayMovingDryRunEnabled != 0) {
        gDigiLiveSequenceReplayMovingDryRunSuccessCount++;
        gDigiLiveSequenceReplayMovingDryRunPacketCount += kDigiLiveSequenceReplayMovingUpdatePackets;
        gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(kIOReturnSuccess);
        return kIOReturnSuccess;
    }

    __sync_synchronize();
    kern_return_t syncRet =
        SyncDigiLiveTransmitReplayRange(updateStart, kDigiLiveSequenceReplayMovingUpdatePackets);
    gDigiLiveSequenceReplayMovingLastSyncRet = ReturnCodeToProperty(syncRet);
    if (syncRet == kIOReturnSuccess) {
        gDigiLiveSequenceReplayMovingUpdateSuccessCount++;
        gDigiLiveSequenceReplayMovingUpdatePacketCount += kDigiLiveSequenceReplayMovingUpdatePackets;
        uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlSet, kContextWake);
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, itControlSet, &gDigiLiveITControlAfterRun);
    }
    return syncRet;
}

void
RunDigiDuplexIsoProbe(IOPCIDevice * pciDevice, uint8_t memoryIndex, DigiDuplexDiagnostics * diagnostics)
{
    diagnostics->isoAttempted = 1;
    CreateDMABuffer(pciDevice, kDigi00xDuplexBufferSize, &gDigiDuplexBuffer);
    if (gDigiDuplexBuffer.result != kIOReturnSuccess ||
        gDigiDuplexBuffer.segmentCount == 0 ||
        gDigiDuplexBuffer.cpuRange.address == 0 ||
        gDigiDuplexBuffer.dmaSegment.address > 0xffffffffull ||
        gDigiDuplexBuffer.dmaSegment.length < kDigi00xDuplexBufferSize) {
        return;
    }
    diagnostics->isoReady = 1;

    volatile OHCIAsyncDescriptor * itDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiDuplexBuffer.cpuRange.address +
                                                        kDigi00xDuplexITDescriptorOffset);
    volatile OHCIAsyncDescriptor * irDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiDuplexBuffer.cpuRange.address +
                                                        kDigi00xDuplexIRDescriptorOffset);
    uint32_t dmaBase = static_cast<uint32_t>(gDigiDuplexBuffer.dmaSegment.address);
    uint32_t itDescriptorDMA = dmaBase + kDigi00xDuplexITDescriptorOffset;
    uint32_t irDescriptorDMA = dmaBase + kDigi00xDuplexIRDescriptorOffset;
    uint32_t itHeaderStorageDMA = dmaBase + kDigi00xDuplexITHeaderStorageOffset;
    uint32_t itPayloadDMA = dmaBase + kDigi00xDuplexITPayloadOffset;
    uint32_t irDataDMA = dmaBase + kDigi00xDuplexIRDataOffset;
    volatile uint32_t * itHeaderStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                              kDigi00xDuplexITHeaderStorageOffset);
    volatile uint32_t * itPayloadStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                              kDigi00xDuplexITPayloadOffset);
    volatile uint32_t * irData =
        reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address + kDigi00xDuplexIRDataOffset);

    uint32_t nodeID = 0;
    pciDevice->MemoryRead32(memoryIndex, kOhciNodeIdOffset, &nodeID);
    diagnostics->sourceNodeIDField = (nodeID & 0x3fu) << 24;

    uint32_t dataBlockCounter = 0;
    uint32_t totalDataBlocks = 0;
    uint32_t payloadByteOffset = 0;
    for (uint32_t packet = 0; packet < kDigi00xDuplexITPacketCount; ++packet) {
        volatile OHCIAsyncDescriptor * packetDescriptor =
            itDescriptor + (packet * kDigi00xDuplexITDescriptorsPerPacket);
        uint32_t packetDescriptorDMA =
            itDescriptorDMA + packet * kDigi00xDuplexITDescriptorsPerPacket * sizeof(OHCIAsyncDescriptor);
        uint32_t dataBlocks = Digi00xDuplexDataBlocksForPacket(packet);
        uint32_t payloadLength = dataBlocks * kDigi00xDuplexDataBlockQuadlets * sizeof(uint32_t);
        uint32_t packetDataLength = 8u + payloadLength;
        uint32_t headerDMA = itHeaderStorageDMA + packet * 8u;
        uint32_t payloadDMA = itPayloadDMA + payloadByteOffset;
        uint32_t cipHeader0 =
            diagnostics->sourceNodeIDField |
            (kDigi00xDuplexDataBlockQuadlets << 16) |
            (dataBlockCounter & 0xffu);
        uint32_t cipHeader1 =
            0x80000000u |
            (0x10u << 24) |
            (gDigi00xCurrentCIPSFC << 16) |
            0xffffu;

        packetDescriptor[0].reqCount = 8;
        packetDescriptor[0].control = kDescriptorKeyImmediate;
        packetDescriptor[0].dataAddress = 0;
        packetDescriptor[0].branchAddress = packetDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket;
        packetDescriptor[0].resCount = 8;
        packetDescriptor[0].transferStatus = 0;

        volatile uint32_t * immediateHeader = reinterpret_cast<volatile uint32_t *>(&packetDescriptor[1]);
        immediateHeader[0] = BuildIsoTransmitHeader0(kDigi00xDuplexDeviceReceiveChannel);
        immediateHeader[1] = BuildIsoTransmitHeader1(packetDataLength);

        packetDescriptor[2].reqCount = 8;
        packetDescriptor[2].control = 0;
        packetDescriptor[2].dataAddress = headerDMA;
        packetDescriptor[2].branchAddress = 0;
        packetDescriptor[2].resCount = 8;
        packetDescriptor[2].transferStatus = 0;

        uint32_t nextDescriptorDMA =
            packetDescriptorDMA +
            kDigi00xDuplexITDescriptorsPerPacket * sizeof(OHCIAsyncDescriptor);
        packetDescriptor[3].reqCount = static_cast<uint16_t>(payloadLength);
        packetDescriptor[3].control = kDescriptorOutputLast |
                                      kDescriptorStatus |
                                      kDescriptorBranchAlways |
                                      (packet == kDigi00xDuplexITPacketCount - 1 ? kDescriptorIrqAlways : 0);
        packetDescriptor[3].dataAddress = payloadDMA;
        packetDescriptor[3].branchAddress =
            packet == kDigi00xDuplexITPacketCount - 1
                ? 0
                : (nextDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket);
        packetDescriptor[3].resCount = static_cast<uint16_t>(payloadLength);
        packetDescriptor[3].transferStatus = 0;

        volatile uint32_t * cipHeader = itHeaderStorage + (packet * 2);
        cipHeader[0] = ToBigEndian32(cipHeader0);
        cipHeader[1] = ToBigEndian32(cipHeader1);

        volatile uint32_t * payload =
            itPayloadStorage + (payloadByteOffset / sizeof(uint32_t));
        for (uint32_t block = 0; block < dataBlocks; ++block) {
            payload[0] = ToBigEndian32(0x80000000u);
            for (uint32_t channel = 0; channel < kDigi00xDuplexPCMAudioChannels; ++channel) {
                payload[1 + channel] = ToBigEndian32(0x40000000u);
            }
            payload += kDigi00xDuplexDataBlockQuadlets;
        }

        if (packet == 0) {
            diagnostics->itImmediateHeader0 = immediateHeader[0];
            diagnostics->itImmediateHeader1 = immediateHeader[1];
            diagnostics->itFirstDataBlocks = dataBlocks;
            diagnostics->itFirstPayloadBytes = payloadLength;
            diagnostics->itFirstCIPHeader0 = cipHeader0;
            diagnostics->itFirstCIPHeader1 = cipHeader1;
        }
        if (packet == kDigi00xDuplexITPacketCount - 1) {
            diagnostics->itLastDataBlocks = dataBlocks;
            diagnostics->itLastPayloadBytes = payloadLength;
            diagnostics->itLastCIPHeader0 = cipHeader0;
            diagnostics->itLastCIPHeader1 = cipHeader1;
        }
        totalDataBlocks += dataBlocks;
        dataBlockCounter = (dataBlockCounter + dataBlocks) & 0xffu;
        payloadByteOffset += payloadLength;
    }
    diagnostics->itTotalDataBlocks = totalDataBlocks;

    for (uint32_t i = 0; i < kDigi00xDuplexIRDescriptorCount; ++i) {
        uint32_t descriptorDMA =
            irDescriptorDMA + i * kDigi00xDuplexIRDescriptorsPerPacketStorage * sizeof(OHCIAsyncDescriptor);
        uint32_t nextDescriptorDMA =
            descriptorDMA + kDigi00xDuplexIRDescriptorsPerPacketStorage * sizeof(OHCIAsyncDescriptor);
        volatile OHCIAsyncDescriptor * packetDescriptor =
            irDescriptor + i * kDigi00xDuplexIRDescriptorsPerPacketStorage;

        packetDescriptor[0].reqCount = kDigi00xDuplexIRHeaderSize;
        packetDescriptor[0].control = kDescriptorInputMore | kDescriptorStatus;
        packetDescriptor[0].dataAddress =
            descriptorDMA + kDigi00xDuplexIRDescriptorBranchCount * sizeof(OHCIAsyncDescriptor);
        packetDescriptor[0].branchAddress = 0;
        packetDescriptor[0].resCount = kDigi00xDuplexIRHeaderSize;
        packetDescriptor[0].transferStatus = 0;

        packetDescriptor[1].reqCount = kDigi00xDuplexIRDescriptorDataSize;
        packetDescriptor[1].control = kDescriptorInputLast | kDescriptorStatus | kDescriptorBranchAlways;
        packetDescriptor[1].dataAddress = irDataDMA + i * kDigi00xDuplexIRDescriptorDataSize;
        packetDescriptor[1].branchAddress =
            i == kDigi00xDuplexIRDescriptorCount - 1
                ? 0
                : (nextDescriptorDMA | kDigi00xDuplexIRDescriptorBranchCount);
        packetDescriptor[1].resCount = kDigi00xDuplexIRDescriptorDataSize;
        packetDescriptor[1].transferStatus = 0;

        packetDescriptor[2].reqCount = 0;
        packetDescriptor[2].control = 0;
        packetDescriptor[2].dataAddress = 0;
        packetDescriptor[2].branchAddress = 0;
        packetDescriptor[2].resCount = 0;
        packetDescriptor[2].transferStatus = 0;
    }

    diagnostics->syncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gDigiDuplexBuffer,
                                                                                kDigi00xDuplexBufferSize));

    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, 0xffffffff);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->xmitMaskSupport);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskClearOffset, 0xffffffff);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, 0xffffffff);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->recvMaskSupport);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskClearOffset, 0xffffffff);

    uint32_t contextBit = 1u << kDigi00xDuplexContextIndex;
    diagnostics->xmitContextSupported = (diagnostics->xmitMaskSupport & contextBit) != 0 ? 1 : 0;
    diagnostics->recvContextSupported = (diagnostics->recvMaskSupport & contextBit) != 0 ? 1 : 0;
    if (diagnostics->xmitContextSupported == 0 || diagnostics->recvContextSupported == 0) {
        diagnostics->completeRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gDigiDuplexBuffer));
        return;
    }

    uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
    uint64_t itControlClear = OhciIsoXmitContextControlClearOffset(kDigi00xDuplexContextIndex);
    uint64_t itCommandPtrOffset = OhciIsoXmitCommandPtrOffset(kDigi00xDuplexContextIndex);
    uint64_t irControlSet = OhciIsoRcvContextControlSetOffset(kDigi00xDuplexContextIndex);
    uint64_t irControlClear = OhciIsoRcvContextControlClearOffset(kDigi00xDuplexContextIndex);
    uint64_t irCommandPtrOffset = OhciIsoRcvCommandPtrOffset(kDigi00xDuplexContextIndex);
    uint64_t irContextMatchOffset = OhciIsoRcvContextMatchOffset(kDigi00xDuplexContextIndex);

    StopContext(pciDevice,
                memoryIndex,
                itControlSet,
                itControlClear,
                &diagnostics->itStopLoops,
                &diagnostics->itControlAfterStop);
    StopContext(pciDevice,
                memoryIndex,
                irControlSet,
                irControlClear,
                &diagnostics->irStopLoops,
                &diagnostics->irControlAfterStop);

    diagnostics->itCommandPtr = itDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket;
    diagnostics->irCommandPtr = irDescriptorDMA | kDigi00xDuplexIRDescriptorBranchCount;
    diagnostics->irContextMatch = (0xfu << 28) | kDigi00xDuplexDeviceTransmitChannel;

    pciDevice->MemoryWrite32(memoryIndex, itCommandPtrOffset, diagnostics->itCommandPtr);
    pciDevice->MemoryWrite32(memoryIndex, irCommandPtrOffset, diagnostics->irCommandPtr);
    pciDevice->MemoryWrite32(memoryIndex, irContextMatchOffset, diagnostics->irContextMatch);

    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntEventClearOffset, contextBit);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, contextBit);
    pciDevice->MemoryWrite32(memoryIndex, irControlSet, kIrContextIsochHeader | kContextRun);
    pciDevice->MemoryRead32(memoryIndex, irControlSet, &diagnostics->irControlAfterRun);

    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntEventClearOffset, contextBit);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, contextBit);
    pciDevice->MemoryWrite32(memoryIndex, itControlClear, 0xffffffff);
    pciDevice->MemoryWrite32(memoryIndex, itControlSet, kContextRun);
    pciDevice->MemoryRead32(memoryIndex, itControlSet, &diagnostics->itControlAfterRun);

    IOSleep(kDigi00xDuplexRunMilliseconds);
    pciDevice->MemoryRead32(memoryIndex, itControlSet, &diagnostics->itControlAfterWait);
    pciDevice->MemoryRead32(memoryIndex, irControlSet, &diagnostics->irControlAfterWait);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntEventSetOffset, &diagnostics->itEventAfterRun);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntEventSetOffset, &diagnostics->irEventAfterRun);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->itMaskAfterRun);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->irMaskAfterRun);

    diagnostics->syncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gDigiDuplexBuffer,
                                                                          kDigi00xDuplexBufferSize));
    volatile OHCIAsyncDescriptor * itLastDescriptor =
        itDescriptor + ((kDigi00xDuplexITPacketCount - 1) *
                        kDigi00xDuplexITDescriptorsPerPacket) + 3;
    diagnostics->itDescriptorResCount = itLastDescriptor[0].resCount;
    diagnostics->itDescriptorStatus = itLastDescriptor[0].transferStatus;
    diagnostics->itLastDescriptorResCount = itLastDescriptor[0].resCount;
    diagnostics->itLastDescriptorStatus = itLastDescriptor[0].transferStatus;
    volatile OHCIAsyncDescriptor * irPacket0 =
        irDescriptor + 0 * kDigi00xDuplexIRDescriptorsPerPacketStorage;
    volatile OHCIAsyncDescriptor * irPacket1 =
        irDescriptor + 1 * kDigi00xDuplexIRDescriptorsPerPacketStorage;
    volatile OHCIAsyncDescriptor * irPacket2 =
        irDescriptor + 2 * kDigi00xDuplexIRDescriptorsPerPacketStorage;
    volatile OHCIAsyncDescriptor * irPacket3 =
        irDescriptor + 3 * kDigi00xDuplexIRDescriptorsPerPacketStorage;
    diagnostics->irDescriptorResCount = irPacket0[1].resCount;
    diagnostics->irDescriptorStatus = irPacket0[1].transferStatus;
    diagnostics->irDescriptor1ResCount = irPacket1[1].resCount;
    diagnostics->irDescriptor1Status = irPacket1[1].transferStatus;
    diagnostics->irDescriptor2ResCount = irPacket2[1].resCount;
    diagnostics->irDescriptor2Status = irPacket2[1].transferStatus;
    diagnostics->irDescriptor3ResCount = irPacket3[1].resCount;
    diagnostics->irDescriptor3Status = irPacket3[1].transferStatus;
    diagnostics->irRxBytes = 0;
    diagnostics->irActiveDescriptorCount = 0;
    diagnostics->irDescriptorLastTouched = 0xffffffff;
    for (uint32_t i = 0; i < kDigi00xDuplexIRDescriptorCount; ++i) {
        volatile OHCIAsyncDescriptor * packetDescriptor =
            irDescriptor + i * kDigi00xDuplexIRDescriptorsPerPacketStorage;
        uint32_t descriptorBytes = kDigi00xDuplexIRDescriptorDataSize - packetDescriptor[1].resCount;
        if (descriptorBytes != 0 ||
            packetDescriptor[0].transferStatus != 0 ||
            packetDescriptor[1].transferStatus != 0) {
            diagnostics->irActiveDescriptorCount++;
            diagnostics->irDescriptorLastTouched = i;
        }
        diagnostics->irRxBytes += descriptorBytes;
    }
    diagnostics->irCaptureSummaryFrameCount = 0;
    diagnostics->irCaptureSummaryPacketCount = 0;
    diagnostics->irCaptureSummaryLabelMismatchCount = 0;
    diagnostics->irCapturePCMFrameCount = 0;
    diagnostics->irCapturePCMBytes = 0;
    diagnostics->irCapturePCMPeakAbs = 0;
    for (size_t channel = 0; channel < kDigi00xDuplexIRCaptureSummaryChannelCount; ++channel) {
        diagnostics->irCaptureChannelNonzeroCount[channel] = 0;
        diagnostics->irCaptureChannelFirstNonzeroFrame[channel] = 0xffffffff;
        diagnostics->irCaptureChannelMinValue[channel] = 0;
        diagnostics->irCaptureChannelMaxValue[channel] = 0;
        diagnostics->irCaptureChannelPeakAbs[channel] = 0;
        diagnostics->irCaptureChannelLastValue[channel] = 0;
    }
    for (uint32_t i = 0; i < kDigi00xDuplexIRDescriptorCount; ++i) {
        volatile OHCIAsyncDescriptor * packetDescriptor =
            irDescriptor + i * kDigi00xDuplexIRDescriptorsPerPacketStorage;
        uint32_t descriptorBytes = kDigi00xDuplexIRDescriptorDataSize - packetDescriptor[1].resCount;
        uint32_t dataBlocks = descriptorBytes / kDigi00xDuplexDataBlockBytes;
        if (dataBlocks == 0) {
            continue;
        }
        diagnostics->irCaptureSummaryPacketCount++;
        volatile uint32_t * packetPayload =
            reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                                  kDigi00xDuplexIRDataOffset +
                                                  i * kDigi00xDuplexIRDescriptorDataSize);
        for (uint32_t block = 0; block < dataBlocks; ++block) {
            volatile uint32_t * dataBlock =
                packetPayload + block * kDigi00xDuplexDataBlockQuadlets;
            for (size_t channel = 0; channel < kDigi00xDuplexIRCaptureSummaryChannelCount; ++channel) {
                uint32_t wordBE = ToBigEndian32(dataBlock[1 + channel]);
                uint32_t label = (wordBE >> 24) & 0xffu;
                if (label != kDigi00xDuplexAM824AudioLabel) {
                    diagnostics->irCaptureSummaryLabelMismatchCount++;
                }
                int32_t sample = Signed24ToInt32(wordBE);
                if (diagnostics->irCaptureSummaryFrameCount == 0) {
                    diagnostics->irCaptureChannelMinValue[channel] = static_cast<uint32_t>(sample);
                    diagnostics->irCaptureChannelMaxValue[channel] = static_cast<uint32_t>(sample);
                } else {
                    int32_t minValue = static_cast<int32_t>(diagnostics->irCaptureChannelMinValue[channel]);
                    int32_t maxValue = static_cast<int32_t>(diagnostics->irCaptureChannelMaxValue[channel]);
                    if (sample < minValue) {
                        diagnostics->irCaptureChannelMinValue[channel] = static_cast<uint32_t>(sample);
                    }
                    if (sample > maxValue) {
                        diagnostics->irCaptureChannelMaxValue[channel] = static_cast<uint32_t>(sample);
                    }
                }
                if (sample != 0) {
                    diagnostics->irCaptureChannelNonzeroCount[channel]++;
                    if (diagnostics->irCaptureChannelFirstNonzeroFrame[channel] == 0xffffffff) {
                        diagnostics->irCaptureChannelFirstNonzeroFrame[channel] =
                            diagnostics->irCaptureSummaryFrameCount;
                    }
                }
                uint32_t peakAbs = AbsoluteInt32(sample);
                if (peakAbs > diagnostics->irCaptureChannelPeakAbs[channel]) {
                    diagnostics->irCaptureChannelPeakAbs[channel] = peakAbs;
                }
                diagnostics->irCaptureChannelLastValue[channel] = static_cast<uint32_t>(sample);
                if (diagnostics->irCapturePCMFrameCount < kDigi00xDuplexIRCapturePCMFrameLimit &&
                    channel < kDigi00xDuplexIRCapturePCMChannelCount) {
                    diagnostics->irCapturePCMS24[diagnostics->irCapturePCMFrameCount][channel] = sample;
                    if (peakAbs > diagnostics->irCapturePCMPeakAbs) {
                        diagnostics->irCapturePCMPeakAbs = peakAbs;
                    }
                }
            }
            if (diagnostics->irCapturePCMFrameCount < kDigi00xDuplexIRCapturePCMFrameLimit) {
                diagnostics->irCapturePCMFrameCount++;
                diagnostics->irCapturePCMBytes =
                    diagnostics->irCapturePCMFrameCount *
                    kDigi00xDuplexIRCapturePCMChannelCount *
                    sizeof(int32_t);
            }
            diagnostics->irCaptureSummaryFrameCount++;
        }
    }
    volatile uint32_t * irFirstPacketHeader =
        reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                              kDigi00xDuplexIRDescriptorOffset +
                                              kDigi00xDuplexIRDescriptorBranchCount *
                                                  sizeof(OHCIAsyncDescriptor));
    for (size_t i = 0; i < 8; ++i) {
        diagnostics->irHeader[i] = i < 4 ? irFirstPacketHeader[i] : irData[i - 4];
    }
    for (size_t packet = 0; packet < kDigi00xDuplexIRSamplePacketCount; ++packet) {
        volatile OHCIAsyncDescriptor * packetDescriptor =
            irDescriptor + packet * kDigi00xDuplexIRDescriptorsPerPacketStorage;
        volatile uint32_t * packetHeader =
            reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                                  kDigi00xDuplexIRDescriptorOffset +
                                                  packet *
                                                      kDigi00xDuplexIRDescriptorsPerPacketStorage *
                                                      sizeof(OHCIAsyncDescriptor) +
                                                  kDigi00xDuplexIRDescriptorBranchCount *
                                                      sizeof(OHCIAsyncDescriptor));
        volatile uint32_t * packetPayload =
            reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                                  kDigi00xDuplexIRDataOffset +
                                                  packet * kDigi00xDuplexIRDescriptorDataSize);

        diagnostics->irSamplePacketIndex[packet] = static_cast<uint32_t>(packet);
        diagnostics->irSampleBytes[packet] =
            kDigi00xDuplexIRDescriptorDataSize - packetDescriptor[1].resCount;
        diagnostics->irSampleDataBlocks[packet] =
            diagnostics->irSampleBytes[packet] / kDigi00xDuplexDataBlockBytes;
        diagnostics->irSampleRemainderBytes[packet] =
            diagnostics->irSampleBytes[packet] % kDigi00xDuplexDataBlockBytes;
        diagnostics->irSampleHeaderStatus[packet] = packetDescriptor[0].transferStatus;
        diagnostics->irSamplePayloadStatus[packet] = packetDescriptor[1].transferStatus;
        for (size_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
            diagnostics->irSampleHeader[packet][word] = packetHeader[word];
            diagnostics->irSampleHeaderBE[packet][word] = ToBigEndian32(packetHeader[word]);
        }
        for (size_t word = 0; word < kDigi00xDuplexIRSamplePayloadWordCount; ++word) {
            diagnostics->irSamplePayload[packet][word] = packetPayload[word];
            diagnostics->irSamplePayloadBE[packet][word] = ToBigEndian32(packetPayload[word]);
        }
        diagnostics->irSampleFirstWordTag[packet] =
            (diagnostics->irSamplePayloadBE[packet][0] >> 24) & 0xffu;
        diagnostics->irSampleFirstAudioTag[packet] =
            (diagnostics->irSamplePayloadBE[packet][1] >> 24) & 0xffu;
        diagnostics->irSampleFirstAudioValue24[packet] =
            diagnostics->irSamplePayloadBE[packet][1] & 0x00ffffffu;
    }
    size_t channelSamplePacket = kDigi00xDuplexIRSamplePacketCount;
    for (size_t packet = 0; packet < kDigi00xDuplexIRSamplePacketCount; ++packet) {
        if (diagnostics->irSampleBytes[packet] >= kDigi00xDuplexDataBlockBytes) {
            channelSamplePacket = packet;
        }
    }
    if (channelSamplePacket < kDigi00xDuplexIRSamplePacketCount) {
        volatile uint32_t * packetHeader =
            reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                                  kDigi00xDuplexIRDescriptorOffset +
                                                  channelSamplePacket *
                                                      kDigi00xDuplexIRDescriptorsPerPacketStorage *
                                                      sizeof(OHCIAsyncDescriptor) +
                                                  kDigi00xDuplexIRDescriptorBranchCount *
                                                      sizeof(OHCIAsyncDescriptor));
        volatile uint32_t * packetPayload =
            reinterpret_cast<volatile uint32_t *>(gDigiDuplexBuffer.cpuRange.address +
                                                  kDigi00xDuplexIRDataOffset +
                                                  channelSamplePacket *
                                                      kDigi00xDuplexIRDescriptorDataSize);
        uint32_t dataBlocks = diagnostics->irSampleDataBlocks[channelSamplePacket];
        uint32_t capturedBlocks =
            dataBlocks > kDigi00xDuplexIRChannelSampleMaxDataBlocks
                ? static_cast<uint32_t>(kDigi00xDuplexIRChannelSampleMaxDataBlocks)
                : dataBlocks;

        diagnostics->irChannelSamplePacketIndex = static_cast<uint32_t>(channelSamplePacket);
        diagnostics->irChannelSampleBytes = diagnostics->irSampleBytes[channelSamplePacket];
        diagnostics->irChannelSampleDataBlocks = dataBlocks;
        diagnostics->irChannelSampleCapturedBlocks = capturedBlocks;
        for (size_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
            diagnostics->irChannelSampleHeaderBE[word] = ToBigEndian32(packetHeader[word]);
        }
        for (uint32_t block = 0; block < capturedBlocks; ++block) {
            volatile uint32_t * dataBlock =
                packetPayload + block * kDigi00xDuplexDataBlockQuadlets;
            uint32_t blockTag = ToBigEndian32(dataBlock[0]);
            diagnostics->irChannelSampleBlockTag[block] = blockTag;
            for (size_t channel = 0; channel < kDigi00xDuplexIRChannelSampleChannelCount; ++channel) {
                uint32_t wordBE = ToBigEndian32(dataBlock[1 + channel]);
                diagnostics->irChannelSampleWordBE[block][channel] = wordBE;
                diagnostics->irChannelSampleLabel[block][channel] = (wordBE >> 24) & 0xffu;
                diagnostics->irChannelSampleValue24[block][channel] = wordBE & 0x00ffffffu;
            }
        }
    }

    StopContext(pciDevice,
                memoryIndex,
                itControlSet,
                itControlClear,
                &diagnostics->itStopLoops,
                &diagnostics->itControlAfterStop);
    StopContext(pciDevice,
                memoryIndex,
                irControlSet,
                irControlClear,
                &diagnostics->irStopLoops,
                &diagnostics->irControlAfterStop);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoXmitIntMaskClearOffset, contextBit);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIsoRecvIntMaskClearOffset, contextBit);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoXmitIntMaskSetOffset, &diagnostics->itMaskAfterClear);
    pciDevice->MemoryRead32(memoryIndex, kOhciIsoRecvIntMaskSetOffset, &diagnostics->irMaskAfterClear);
    diagnostics->completeRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gDigiDuplexBuffer));
}

uint32_t
BuildATQuadlet0(uint32_t speed, uint32_t tlabel, uint32_t retry, uint32_t tcode)
{
    return ((speed & 0x7u) << 16) |
           ((tlabel & 0x3fu) << 10) |
           ((retry & 0x3u) << 8) |
           ((tcode & 0xfu) << 4);
}

uint32_t
BuildATQuadlet1(uint32_t destinationID, uint64_t offset)
{
    return ((destinationID & 0xffffu) << 16) |
           (static_cast<uint32_t>(offset >> 32) & 0xffffu);
}

uint32_t
ByteSwap32(uint32_t value)
{
    return ((value & 0x000000ffu) << 24) |
           ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) |
           ((value & 0xff000000u) >> 24);
}

uint32_t
ResponseTCode(uint32_t header0)
{
    return (header0 >> 4) & 0xfu;
}

uint32_t
ResponseTLabel(uint32_t header0)
{
    return (header0 >> 10) & 0x3fu;
}

uint32_t
ResponseSource(uint32_t header1)
{
    return (header1 >> 16) & 0xffffu;
}

uint32_t
ResponseRCode(uint32_t header1)
{
    return (header1 >> 12) & 0xfu;
}

void
ConfigureAsyncReceiveBuffer(DMABuffer * buffer,
                            uint32_t * commandPtr,
                            volatile OHCIAsyncDescriptor ** descriptorsOut,
                            volatile uint32_t ** data0Out,
                            volatile uint32_t ** data1Out)
{
    volatile OHCIAsyncDescriptor * rxDescriptors =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(buffer->cpuRange.address);
    uint32_t rxDescriptorDMA = static_cast<uint32_t>(buffer->dmaSegment.address);
    uint32_t rxDataDMA0 = rxDescriptorDMA + kAsyncRxDataOffset0;
    uint32_t rxDataDMA1 = rxDescriptorDMA + kAsyncRxDataOffset1;

    rxDescriptors[0].reqCount = kAsyncRxDataSize;
    rxDescriptors[0].control = kDescriptorInputMore | kDescriptorStatus | kDescriptorBranchAlways;
    rxDescriptors[0].dataAddress = rxDataDMA0;
    rxDescriptors[0].branchAddress = (rxDescriptorDMA + sizeof(OHCIAsyncDescriptor)) | 1u;
    rxDescriptors[0].resCount = kAsyncRxDataSize;
    rxDescriptors[0].transferStatus = 0;
    rxDescriptors[1].reqCount = kAsyncRxDataSize;
    rxDescriptors[1].control = kDescriptorInputMore | kDescriptorStatus | kDescriptorBranchAlways;
    rxDescriptors[1].dataAddress = rxDataDMA1;
    rxDescriptors[1].branchAddress = rxDescriptorDMA;
    rxDescriptors[1].resCount = kAsyncRxDataSize;
    rxDescriptors[1].transferStatus = 0;

    *commandPtr = rxDescriptorDMA | 1u;
    *descriptorsOut = rxDescriptors;
    *data0Out = reinterpret_cast<volatile uint32_t *>(buffer->cpuRange.address + kAsyncRxDataOffset0);
    *data1Out = reinterpret_cast<volatile uint32_t *>(buffer->cpuRange.address + kAsyncRxDataOffset1);
    for (size_t i = 0; i < 4; ++i) {
        (*data0Out)[i] = 0;
        (*data1Out)[i] = 0;
    }
}

bool
RunAsyncQuadletTransaction(IOPCIDevice * pciDevice,
                           uint8_t memoryIndex,
                           AsyncReadDiagnostics * diagnostics,
                           volatile OHCIAsyncDescriptor * txDescriptor,
                           volatile uint32_t * txHeader,
                           uint64_t offset,
                           uint32_t requestTCode,
                           uint32_t expectedResponseTCode,
                           uint32_t reqCount,
                           uint32_t txData,
                           uint32_t tlabelBase,
                           uint32_t * completedAttempts,
                           uint32_t * waitLoops,
                           uint32_t * header0,
                           uint32_t * header1,
                           uint32_t * header2,
                           uint32_t * header3,
                           uint32_t * txStatus,
                           uint32_t * rxBytes,
                           uint32_t * responseTCode,
                           uint32_t * responseTLabel,
                           uint32_t * responseSource,
                           uint32_t * responseRCode,
                           uint32_t * responseData,
                           uint32_t attemptCount = kAsyncReadAttemptCount,
                           uint32_t waitLoopsPerAttempt = kAsyncReadWaitLoopsPerAttempt,
                           uint32_t retrySettleMilliseconds = kAsyncReadRetrySettleMilliseconds)
{
    volatile OHCIAsyncDescriptor * rxDescriptors = nullptr;
    volatile uint32_t * rxData0 = nullptr;
    volatile uint32_t * rxData1 = nullptr;
    volatile OHCIAsyncDescriptor * reqRxDescriptors = nullptr;
    volatile uint32_t * reqRxData0 = nullptr;
    volatile uint32_t * reqRxData1 = nullptr;
    diagnostics->configuredAttempts = attemptCount;
    diagnostics->waitLoopsPerAttempt = waitLoopsPerAttempt;
    diagnostics->retrySettleMilliseconds = retrySettleMilliseconds;

    for (uint32_t attempt = 0; attempt < attemptCount; ++attempt) {
        StopContext(pciDevice,
                    memoryIndex,
                    kOhciAsReqTrContextControlSetOffset,
                    kOhciAsReqTrContextControlClearOffset,
                    &diagnostics->txContextStopLoops,
                    &diagnostics->txContextAfterClear);
        StopContext(pciDevice,
                    memoryIndex,
                    kOhciAsReqRcvContextControlSetOffset,
                    kOhciAsReqRcvContextControlClearOffset,
                    &diagnostics->reqRxContextStopLoops,
                    &diagnostics->reqRxContextAfterClear);
        StopContext(pciDevice,
                    memoryIndex,
                    kOhciAsRspRcvContextControlSetOffset,
                    kOhciAsRspRcvContextControlClearOffset,
                    &diagnostics->rxContextStopLoops,
                    &diagnostics->rxContextAfterClear);
        ConfigureAsyncReceiveBuffer(&gAsyncRxBuffer,
                                    &diagnostics->rxCommandPtr,
                                    &rxDescriptors,
                                    &rxData0,
                                    &rxData1);
        ConfigureAsyncReceiveBuffer(&gAsyncReqRxBuffer,
                                    &diagnostics->reqRxCommandPtr,
                                    &reqRxDescriptors,
                                    &reqRxData0,
                                    &reqRxData1);

        *completedAttempts += 1;
        diagnostics->rxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncRxBuffer,
                                                                                      kAsyncRxBufferSize));
        diagnostics->reqRxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncReqRxBuffer,
                                                                                         kAsyncRxBufferSize));

        txDescriptor[0].reqCount = static_cast<uint16_t>(reqCount);
        txDescriptor[0].control = kDescriptorKeyImmediate |
                                  kDescriptorOutputLast |
                                  kDescriptorIrqAlways |
                                  kDescriptorBranchAlways;
        txDescriptor[0].dataAddress = 0;
        txDescriptor[0].branchAddress = 0;
        txDescriptor[0].resCount = 0;
        txDescriptor[0].transferStatus = 0;
        uint32_t tlabel = (tlabelBase + attempt) & 0x3fu;
        txHeader[0] = BuildATQuadlet0(kSCode400, tlabel, kRetryX, requestTCode);
        txHeader[1] = BuildATQuadlet1(diagnostics->destinationID, offset);
        txHeader[2] = static_cast<uint32_t>(offset & 0xffffffffu);
        txHeader[3] = txData;

        *header0 = txHeader[0];
        *header1 = txHeader[1];
        *header2 = txHeader[2];
        *header3 = txHeader[3];
        diagnostics->txDescriptorControl = txDescriptor[0].control;
        diagnostics->txDescriptorReqCount = txDescriptor[0].reqCount;
        diagnostics->txHeader0 = txHeader[0];
        diagnostics->txHeader1 = txHeader[1];
        diagnostics->txHeader2 = txHeader[2];
        diagnostics->txHeader3 = txHeader[3];
        diagnostics->txSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncTxBuffer,
                                                                                      kAsyncTxBufferSize));
        __sync_synchronize();
        pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvCommandPtrOffset, diagnostics->reqRxCommandPtr);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvCommandPtrOffset, diagnostics->rxCommandPtr);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, kContextRun);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, kContextRun);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrCommandPtrOffset, diagnostics->txCommandPtr);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextRun);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextWake);

        bool responseReceived = false;
        bool waitingForResponse = false;
        for (uint32_t i = 0; i < waitLoopsPerAttempt; ++i) {
            *waitLoops += 1;
            diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
            diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
            diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
            responseReceived = diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ||
                               diagnostics->rxDescriptor1ResCount != kAsyncRxDataSize;
            bool transmitFinished = diagnostics->txDescriptorStatus != 0;
            waitingForResponse =
                (diagnostics->txDescriptorStatus & kDescriptorEventMask) == kDescriptorEventAckPending;
            if (responseReceived || (transmitFinished && !waitingForResponse)) {
                break;
            }
            IOSleep(kAsyncReadWaitMilliseconds);
        }

        diagnostics->rxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncRxBuffer,
                                                                                kAsyncRxBufferSize));
        diagnostics->reqRxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncReqRxBuffer,
                                                                                   kAsyncRxBufferSize));
        diagnostics->txSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncTxBuffer,
                                                                                kAsyncTxBufferSize));
        *txStatus = txDescriptor[0].transferStatus;
        diagnostics->txDescriptorStatus = *txStatus;
        diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
        diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
        uint32_t rxBytes0 = kAsyncRxDataSize - diagnostics->rxDescriptor0ResCount;
        uint32_t rxBytes1 = kAsyncRxDataSize - diagnostics->rxDescriptor1ResCount;
        volatile uint32_t * response = rxData0;
        *rxBytes = rxBytes0;
        if (rxBytes0 == 0 && rxBytes1 != 0) {
            response = rxData1;
            *rxBytes = rxBytes1;
        }
        diagnostics->rxHeader0 = response[0];
        diagnostics->rxHeader1 = response[1];
        diagnostics->rxHeader2 = response[2];
        diagnostics->rxHeader3 = response[3];
        *responseTCode = ResponseTCode(diagnostics->rxHeader0);
        *responseTLabel = ResponseTLabel(diagnostics->rxHeader0);
        *responseSource = ResponseSource(diagnostics->rxHeader1);
        *responseRCode = ResponseRCode(diagnostics->rxHeader1);
        *responseData = diagnostics->rxHeader3;

        if (waitingForResponse) {
            diagnostics->ackPendingAttempts += 1;
        }
        if (responseReceived &&
            *responseTCode == expectedResponseTCode &&
            *responseTLabel == tlabel &&
            *responseRCode == 0) {
            return true;
        }
        if (attempt + 1 < attemptCount) {
            IOSleep(retrySettleMilliseconds);
        }
    }

    return false;
}

bool
RunDigiDuplexTransaction(IOPCIDevice * pciDevice,
                         uint8_t memoryIndex,
                         AsyncReadDiagnostics * asyncDiagnostics,
                         DigiDuplexDiagnostics * duplexDiagnostics,
                         volatile OHCIAsyncDescriptor * txDescriptor,
                         volatile uint32_t * txHeader,
                         size_t stepIndex,
                         uint32_t op,
                         uint64_t offsetLo,
                         uint32_t busValue,
                         uint32_t tlabelBase,
                         uint32_t attemptCount = kAsyncReadAttemptCount,
                         uint32_t waitLoopsPerAttempt = kAsyncReadWaitLoopsPerAttempt,
                         uint32_t retrySettleMilliseconds = kAsyncReadRetrySettleMilliseconds)
{
    constexpr uint32_t kDigiDuplexOpRead = 0;
    constexpr uint32_t kDigiDuplexOpWrite = 1;
    constexpr uint32_t kDigiDuplexOpSkip = 2;

    duplexDiagnostics->stepOp[stepIndex] = op;
    duplexDiagnostics->stepOffsetLo[stepIndex] = static_cast<uint32_t>(offsetLo);
    duplexDiagnostics->stepBusValue[stepIndex] = busValue;
    duplexDiagnostics->completedSteps = static_cast<uint32_t>(stepIndex + 1);
    if (op == kDigiDuplexOpSkip) {
        duplexDiagnostics->stepSuccess[stepIndex] = 1;
        return true;
    }

    bool isWrite = op == kDigiDuplexOpWrite;
    uint32_t requestTCode = isWrite ? kTCodeWriteQuadletRequest : kTCodeReadQuadletRequest;
    uint32_t responseTCode = isWrite ? kTCodeWriteResponse : kTCodeReadQuadletResponse;
    uint32_t reqCount = isWrite ? 16u : 12u;
    uint32_t txData = isWrite ? ByteSwap32(busValue) : 0;
    duplexDiagnostics->stepTxData[stepIndex] = txData;

    bool ok = RunAsyncQuadletTransaction(pciDevice,
                                         memoryIndex,
                                         asyncDiagnostics,
                                         txDescriptor,
                                         txHeader,
                                         kDigi00xRegisterBase + offsetLo,
                                         requestTCode,
                                         responseTCode,
                                         reqCount,
                                         txData,
                                         tlabelBase,
                                         &duplexDiagnostics->stepCompletedAttempts[stepIndex],
                                         &duplexDiagnostics->stepWaitLoops[stepIndex],
                                         &asyncDiagnostics->txHeader0,
                                         &asyncDiagnostics->txHeader1,
                                         &asyncDiagnostics->txHeader2,
                                         &asyncDiagnostics->txHeader3,
                                         &duplexDiagnostics->stepTxStatus[stepIndex],
                                         &duplexDiagnostics->stepRxBytes[stepIndex],
                                         &duplexDiagnostics->stepResponseTCode[stepIndex],
                                         &duplexDiagnostics->stepResponseTLabel[stepIndex],
                                         &duplexDiagnostics->stepResponseSource[stepIndex],
                                         &duplexDiagnostics->stepResponseRCode[stepIndex],
                                         &duplexDiagnostics->stepResponseData[stepIndex],
                                         attemptCount,
                                         waitLoopsPerAttempt,
                                         retrySettleMilliseconds);
    duplexDiagnostics->stepSuccess[stepIndex] = ok ? 1 : 0;
    if (!isWrite && ok) {
        duplexDiagnostics->stepBusValue[stepIndex] =
            ByteSwap32(duplexDiagnostics->stepResponseData[stepIndex]);
    }
    return ok;
}

void
RunDigiDuplexProbe(IOPCIDevice * pciDevice,
                   uint8_t memoryIndex,
                   AsyncReadDiagnostics * asyncDiagnostics,
                   DigiDuplexDiagnostics * duplexDiagnostics,
                   volatile OHCIAsyncDescriptor * txDescriptor,
                   volatile uint32_t * txHeader)
{
    if (kDigi00xDuplexProbeEnabled == 0) {
        return;
    }

    constexpr uint32_t kDigiDuplexOpRead = 0;
    constexpr uint32_t kDigiDuplexOpWrite = 1;
    constexpr uint32_t kDigiDuplexOpSkip = 2;
    duplexDiagnostics->attempted = 1;
    duplexDiagnostics->destinationID = asyncDiagnostics->destinationID;
    duplexDiagnostics->stepCount = kDigi00xDuplexStepCount;

    uint32_t tlabelBase =
        (kAsyncReadTLabel +
         static_cast<uint32_t>((kConfigROMProbeReadCount +
                                kDigi00xRegisterProbeCount +
                                kDigi00xWritePlanCount +
                                kDigi00xStateSequenceStepCount +
                                20u) * kAsyncReadAttemptCount)) & 0x3fu;

    bool rateWritten =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 0,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetLocalRate,
                                 gDigi00xCurrentLocalRateIndex,
                                 tlabelBase);
    IOSleep(20);

    bool channelsWritten =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 1,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetIsocChannels,
                                 duplexDiagnostics->sessionChannelsBusValue,
                                 tlabelBase + kAsyncReadAttemptCount);
    bool stateRead =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 2,
                                 kDigiDuplexOpRead,
                                 kDigi00xOffsetStreamingState,
                                 0,
                                 tlabelBase + (2u * kAsyncReadAttemptCount));
    if (stateRead) {
        duplexDiagnostics->initialStreamingStateBusValue = duplexDiagnostics->stepBusValue[2];
    }

    uint32_t currentState = stateRead ? duplexDiagnostics->initialStreamingStateBusValue : 0;
    if (currentState == 0) {
        currentState = 2;
    }
    if (currentState > 0) {
        currentState -= 1;
    }
    if (currentState == 0) {
        currentState = 1;
    }
    duplexDiagnostics->beginSetFirstBusValue = currentState;
    bool firstSet =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 3,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetStreamingSet,
                                 duplexDiagnostics->beginSetFirstBusValue,
                                 tlabelBase + (3u * kAsyncReadAttemptCount));
    IOSleep(20);

    bool secondSet = true;
    if (duplexDiagnostics->beginSetFirstBusValue > 1) {
        duplexDiagnostics->beginSetSecondBusValue = duplexDiagnostics->beginSetFirstBusValue - 1;
        secondSet =
            RunDigiDuplexTransaction(pciDevice,
                                     memoryIndex,
                                     asyncDiagnostics,
                                     duplexDiagnostics,
                                     txDescriptor,
                                     txHeader,
                                     4,
                                     kDigiDuplexOpWrite,
                                     kDigi00xOffsetStreamingSet,
                                     duplexDiagnostics->beginSetSecondBusValue,
                                     tlabelBase + (4u * kAsyncReadAttemptCount));
        IOSleep(20);
    } else {
        duplexDiagnostics->beginSetSecondBusValue = 0;
        secondSet =
            RunDigiDuplexTransaction(pciDevice,
                                     memoryIndex,
                                     asyncDiagnostics,
                                     duplexDiagnostics,
                                     txDescriptor,
                                     txHeader,
                                     4,
                                     kDigiDuplexOpSkip,
                                     kDigi00xOffsetStreamingSet,
                                     0,
                                     tlabelBase + (4u * kAsyncReadAttemptCount));
    }

    if (rateWritten && channelsWritten && stateRead && firstSet && secondSet) {
        RunDigiDuplexIsoProbe(pciDevice, memoryIndex, duplexDiagnostics);
    }

    duplexDiagnostics->finishAttempted = 1;
    bool finishSet =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 5,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetStreamingSet,
                                 3,
                                 tlabelBase + (5u * kAsyncReadAttemptCount));
    bool finishChannels =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 6,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetIsocChannels,
                                 0,
                                 tlabelBase + (6u * kAsyncReadAttemptCount));
    IOSleep(50);
    bool finalState =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 7,
                                 kDigiDuplexOpRead,
                                 kDigi00xOffsetStreamingState,
                                 0,
                                 tlabelBase + (7u * kAsyncReadAttemptCount));
    bool finalChannels =
        RunDigiDuplexTransaction(pciDevice,
                                 memoryIndex,
                                 asyncDiagnostics,
                                 duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 8,
                                 kDigiDuplexOpRead,
                                 kDigi00xOffsetIsocChannels,
                                 0,
                                 tlabelBase + (8u * kAsyncReadAttemptCount));
    if (finalState) {
        duplexDiagnostics->finalStreamingStateBusValue = duplexDiagnostics->stepBusValue[7];
    }
    if (finalChannels) {
        duplexDiagnostics->finalIsocChannelsBusValue = duplexDiagnostics->stepBusValue[8];
    }

    duplexDiagnostics->success =
        (rateWritten && channelsWritten && stateRead && firstSet && secondSet &&
         finishSet && finishChannels && finalState && finalChannels) ? 1 : 0;
}

bool
PrepareDigiAsyncTransactionBuffers(IOPCIDevice * pciDevice,
                                   AsyncReadDiagnostics * asyncDiagnostics,
                                   volatile OHCIAsyncDescriptor ** txDescriptorOut,
                                   volatile uint32_t ** txHeaderOut)
{
    if (pciDevice == nullptr ||
        asyncDiagnostics == nullptr ||
        txDescriptorOut == nullptr ||
        txHeaderOut == nullptr ||
        gDigiDestinationID == 0xffffffff) {
        return false;
    }

    CreateDMABuffer(pciDevice, kAsyncRxBufferSize, &gAsyncRxBuffer);
    CreateDMABuffer(pciDevice, kAsyncRxBufferSize, &gAsyncReqRxBuffer);
    CreateDMABuffer(pciDevice, kAsyncTxBufferSize, &gAsyncTxBuffer);
    if (gAsyncRxBuffer.result != kIOReturnSuccess ||
        gAsyncReqRxBuffer.result != kIOReturnSuccess ||
        gAsyncTxBuffer.result != kIOReturnSuccess ||
        gAsyncRxBuffer.cpuRange.address == 0 ||
        gAsyncReqRxBuffer.cpuRange.address == 0 ||
        gAsyncTxBuffer.cpuRange.address == 0 ||
        gAsyncRxBuffer.segmentCount != 1 ||
        gAsyncReqRxBuffer.segmentCount != 1 ||
        gAsyncTxBuffer.segmentCount != 1 ||
        gAsyncRxBuffer.dmaSegment.length < kAsyncRxBufferSize ||
        gAsyncReqRxBuffer.dmaSegment.length < kAsyncRxBufferSize ||
        gAsyncTxBuffer.dmaSegment.length < kAsyncTxBufferSize ||
        gAsyncRxBuffer.dmaSegment.address > 0xffffffffull ||
        gAsyncReqRxBuffer.dmaSegment.address > 0xffffffffull ||
        gAsyncTxBuffer.dmaSegment.address > 0xffffffffull) {
        return false;
    }

    asyncDiagnostics->destinationID = gDigiDestinationID;
    asyncDiagnostics->ready = 1;
    asyncDiagnostics->configuredAttempts = kAsyncReadAttemptCount;
    asyncDiagnostics->waitLoopsPerAttempt = kAsyncReadWaitLoopsPerAttempt;
    asyncDiagnostics->retrySettleMilliseconds = kAsyncReadRetrySettleMilliseconds;
    *txDescriptorOut = reinterpret_cast<volatile OHCIAsyncDescriptor *>(gAsyncTxBuffer.cpuRange.address);
    *txHeaderOut =
        reinterpret_cast<volatile uint32_t *>(gAsyncTxBuffer.cpuRange.address + sizeof(OHCIAsyncDescriptor));
    uint32_t txDescriptorDMA = static_cast<uint32_t>(gAsyncTxBuffer.dmaSegment.address);
    asyncDiagnostics->txCommandPtr = txDescriptorDMA | 2u;
    return true;
}

void
CompleteDigiAsyncTransactionBuffers(IOPCIDevice * pciDevice,
                                    uint8_t memoryIndex,
                                    AsyncReadDiagnostics * asyncDiagnostics)
{
    if (pciDevice != nullptr && memoryIndex != 0xff && asyncDiagnostics != nullptr) {
        StopContext(pciDevice,
                    memoryIndex,
                    kOhciAsReqTrContextControlSetOffset,
                    kOhciAsReqTrContextControlClearOffset,
                    &asyncDiagnostics->txContextFinalStopLoops,
                    &asyncDiagnostics->txContextAfterClear);
        StopContext(pciDevice,
                    memoryIndex,
                    kOhciAsReqRcvContextControlSetOffset,
                    kOhciAsReqRcvContextControlClearOffset,
                    &asyncDiagnostics->reqRxContextFinalStopLoops,
                    &asyncDiagnostics->reqRxContextAfterClear);
        StopContext(pciDevice,
                    memoryIndex,
                    kOhciAsRspRcvContextControlSetOffset,
                    kOhciAsRspRcvContextControlClearOffset,
                    &asyncDiagnostics->rxContextFinalStopLoops,
                    &asyncDiagnostics->rxContextAfterClear);
    }
    if (asyncDiagnostics != nullptr) {
        asyncDiagnostics->rxCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gAsyncRxBuffer));
        asyncDiagnostics->reqRxCompleteRet =
            ReturnCodeToProperty(CompleteDMABufferMapping(&gAsyncReqRxBuffer));
        asyncDiagnostics->txCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gAsyncTxBuffer));
    } else {
        CompleteDMABufferMapping(&gAsyncRxBuffer);
        CompleteDMABufferMapping(&gAsyncReqRxBuffer);
        CompleteDMABufferMapping(&gAsyncTxBuffer);
    }
}

uint32_t
DigiLiveTLabelBase()
{
    return (kAsyncReadTLabel +
            static_cast<uint32_t>((kConfigROMProbeReadCount +
                                   kDigi00xRegisterProbeCount +
                                   kDigi00xWritePlanCount +
                                   kDigi00xStateSequenceStepCount +
                                   40u) * kAsyncReadAttemptCount)) & 0x3fu;
}

kern_return_t
RunDigiLiveBeginTransactions()
{
    if (gPCIDevice == nullptr ||
        gPCIMemoryIndex == 0xff ||
        gDigiDestinationID == 0xffffffff) {
        return kIOReturnNotReady;
    }

    constexpr uint32_t kDigiDuplexOpRead = 0;
    constexpr uint32_t kDigiDuplexOpWrite = 1;
    constexpr uint32_t kDigiDuplexOpSkip = 2;
    AsyncReadDiagnostics asyncDiagnostics = {};
    InitializeAsyncDiagnostics(&asyncDiagnostics);
    DigiDuplexDiagnostics duplexDiagnostics = {};
    InitializeDigiDuplexDiagnostics(&duplexDiagnostics);
    volatile OHCIAsyncDescriptor * txDescriptor = nullptr;
    volatile uint32_t * txHeader = nullptr;
    if (!PrepareDigiAsyncTransactionBuffers(gPCIDevice, &asyncDiagnostics, &txDescriptor, &txHeader)) {
        CompleteDigiAsyncTransactionBuffers(gPCIDevice, gPCIMemoryIndex, &asyncDiagnostics);
        return kIOReturnNoResources;
    }

    duplexDiagnostics.attempted = 1;
    duplexDiagnostics.destinationID = asyncDiagnostics.destinationID;
    duplexDiagnostics.stepCount = kDigi00xDuplexStepCount;
    uint32_t tlabelBase = DigiLiveTLabelBase();

    gDigiLiveBeginTransactionStage = 1;
    bool rateWritten =
        RunDigiDuplexTransaction(gPCIDevice,
                                 gPCIMemoryIndex,
                                 &asyncDiagnostics,
                                 &duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 0,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetLocalRate,
                                 gDigi00xCurrentLocalRateIndex,
                                 tlabelBase,
                                 kDigiLiveAsyncAttemptCount,
                                 kDigiLiveAsyncWaitLoopsPerAttempt,
                                 kDigiLiveAsyncRetrySettleMilliseconds);
    IOSleep(20);

    bool channelsWritten =
        RunDigiDuplexTransaction(gPCIDevice,
                                 gPCIMemoryIndex,
                                 &asyncDiagnostics,
                                 &duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 1,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetIsocChannels,
                                 duplexDiagnostics.sessionChannelsBusValue,
                                 tlabelBase + kAsyncReadAttemptCount,
                                 kDigiLiveAsyncAttemptCount,
                                 kDigiLiveAsyncWaitLoopsPerAttempt,
                                 kDigiLiveAsyncRetrySettleMilliseconds);
    gDigiLiveBeginTransactionStage = 2;
    bool stateRead =
        RunDigiDuplexTransaction(gPCIDevice,
                                 gPCIMemoryIndex,
                                 &asyncDiagnostics,
                                 &duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 2,
                                 kDigiDuplexOpRead,
                                 kDigi00xOffsetStreamingState,
                                 0,
                                 tlabelBase + (2u * kAsyncReadAttemptCount),
                                 kDigiLiveAsyncAttemptCount,
                                 kDigiLiveAsyncWaitLoopsPerAttempt,
                                 kDigiLiveAsyncRetrySettleMilliseconds);
    if (stateRead) {
        duplexDiagnostics.initialStreamingStateBusValue = duplexDiagnostics.stepBusValue[2];
    }

    gDigiLiveBeginTransactionStage = 3;
    uint32_t currentState = stateRead ? duplexDiagnostics.initialStreamingStateBusValue : 0;
    if (currentState == 0) {
        currentState = 2;
    }
    if (currentState > 0) {
        currentState -= 1;
    }
    if (currentState == 0) {
        currentState = 1;
    }
    duplexDiagnostics.beginSetFirstBusValue = currentState;
    bool firstSet =
        RunDigiDuplexTransaction(gPCIDevice,
                                 gPCIMemoryIndex,
                                 &asyncDiagnostics,
                                 &duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 3,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetStreamingSet,
                                 duplexDiagnostics.beginSetFirstBusValue,
                                 tlabelBase + (3u * kAsyncReadAttemptCount),
                                 kDigiLiveAsyncAttemptCount,
                                 kDigiLiveAsyncWaitLoopsPerAttempt,
                                 kDigiLiveAsyncRetrySettleMilliseconds);
    IOSleep(20);

    gDigiLiveBeginTransactionStage = 4;
    bool secondSet = true;
    if (duplexDiagnostics.beginSetFirstBusValue > 1) {
        duplexDiagnostics.beginSetSecondBusValue = duplexDiagnostics.beginSetFirstBusValue - 1;
        secondSet =
            RunDigiDuplexTransaction(gPCIDevice,
                                     gPCIMemoryIndex,
                                     &asyncDiagnostics,
                                     &duplexDiagnostics,
                                     txDescriptor,
                                     txHeader,
                                     4,
                                     kDigiDuplexOpWrite,
                                     kDigi00xOffsetStreamingSet,
                                     duplexDiagnostics.beginSetSecondBusValue,
                                     tlabelBase + (4u * kAsyncReadAttemptCount),
                                     kDigiLiveAsyncAttemptCount,
                                     kDigiLiveAsyncWaitLoopsPerAttempt,
                                     kDigiLiveAsyncRetrySettleMilliseconds);
        IOSleep(20);
    } else {
        duplexDiagnostics.beginSetSecondBusValue = 0;
        secondSet =
            RunDigiDuplexTransaction(gPCIDevice,
                                     gPCIMemoryIndex,
                                     &asyncDiagnostics,
                                     &duplexDiagnostics,
                                     txDescriptor,
                                     txHeader,
                                     4,
                                     kDigiDuplexOpSkip,
                                     kDigi00xOffsetStreamingSet,
                                     0,
                                     tlabelBase + (4u * kAsyncReadAttemptCount),
                                     kDigiLiveAsyncAttemptCount,
                                     kDigiLiveAsyncWaitLoopsPerAttempt,
                                     kDigiLiveAsyncRetrySettleMilliseconds);
    }

    CompleteDigiAsyncTransactionBuffers(gPCIDevice, gPCIMemoryIndex, &asyncDiagnostics);
    gDigiLiveBeginTransactionStage = 5;
    kern_return_t ret = (rateWritten && channelsWritten && stateRead && firstSet && secondSet) ?
        kIOReturnSuccess :
        kIOReturnIOError;
    gDigiLiveBeginTransactionRet = ReturnCodeToProperty(ret);
    return ret;
}

kern_return_t
RunDigiLiveFinishTransactions()
{
    if (gPCIDevice == nullptr ||
        gPCIMemoryIndex == 0xff ||
        gDigiDestinationID == 0xffffffff) {
        return kIOReturnNotReady;
    }

    constexpr uint32_t kDigiDuplexOpRead = 0;
    constexpr uint32_t kDigiDuplexOpWrite = 1;
    AsyncReadDiagnostics asyncDiagnostics = {};
    InitializeAsyncDiagnostics(&asyncDiagnostics);
    DigiDuplexDiagnostics duplexDiagnostics = {};
    InitializeDigiDuplexDiagnostics(&duplexDiagnostics);
    volatile OHCIAsyncDescriptor * txDescriptor = nullptr;
    volatile uint32_t * txHeader = nullptr;
    if (!PrepareDigiAsyncTransactionBuffers(gPCIDevice, &asyncDiagnostics, &txDescriptor, &txHeader)) {
        CompleteDigiAsyncTransactionBuffers(gPCIDevice, gPCIMemoryIndex, &asyncDiagnostics);
        return kIOReturnNoResources;
    }

    duplexDiagnostics.attempted = 1;
    duplexDiagnostics.destinationID = asyncDiagnostics.destinationID;
    duplexDiagnostics.stepCount = kDigi00xDuplexStepCount;
    duplexDiagnostics.finishAttempted = 1;
    uint32_t tlabelBase = DigiLiveTLabelBase();

    bool finishSet =
        RunDigiDuplexTransaction(gPCIDevice,
                                 gPCIMemoryIndex,
                                 &asyncDiagnostics,
                                 &duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 4,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetStreamingSet,
                                 3,
                                 tlabelBase + (4u * kAsyncReadAttemptCount),
                                 kDigiLiveAsyncAttemptCount,
                                 kDigiLiveAsyncWaitLoopsPerAttempt,
                                 kDigiLiveAsyncRetrySettleMilliseconds);
    bool finishChannels =
        RunDigiDuplexTransaction(gPCIDevice,
                                 gPCIMemoryIndex,
                                 &asyncDiagnostics,
                                 &duplexDiagnostics,
                                 txDescriptor,
                                 txHeader,
                                 5,
                                 kDigiDuplexOpWrite,
                                 kDigi00xOffsetIsocChannels,
                                 0,
                                 tlabelBase + (5u * kAsyncReadAttemptCount),
                                 kDigiLiveAsyncAttemptCount,
                                 kDigiLiveAsyncWaitLoopsPerAttempt,
                                 kDigiLiveAsyncRetrySettleMilliseconds);
    IOSleep(50);
    RunDigiDuplexTransaction(gPCIDevice,
                             gPCIMemoryIndex,
                             &asyncDiagnostics,
                             &duplexDiagnostics,
                             txDescriptor,
                             txHeader,
                             6,
                             kDigiDuplexOpRead,
                             kDigi00xOffsetStreamingState,
                             0,
                             tlabelBase + (6u * kAsyncReadAttemptCount),
                             kDigiLiveAsyncAttemptCount,
                             kDigiLiveAsyncWaitLoopsPerAttempt,
                             kDigiLiveAsyncRetrySettleMilliseconds);
    RunDigiDuplexTransaction(gPCIDevice,
                             gPCIMemoryIndex,
                             &asyncDiagnostics,
                             &duplexDiagnostics,
                             txDescriptor,
                             txHeader,
                             7,
                             kDigiDuplexOpRead,
                             kDigi00xOffsetIsocChannels,
                             0,
                             tlabelBase + (7u * kAsyncReadAttemptCount),
                             kDigiLiveAsyncAttemptCount,
                             kDigiLiveAsyncWaitLoopsPerAttempt,
                             kDigiLiveAsyncRetrySettleMilliseconds);

    CompleteDigiAsyncTransactionBuffers(gPCIDevice, gPCIMemoryIndex, &asyncDiagnostics);
    return (finishSet && finishChannels) ? kIOReturnSuccess : kIOReturnIOError;
}

void
ConfigureDigiLiveTransmitDescriptors(volatile OHCIAsyncDescriptor * itDescriptor,
                                     uint32_t itDescriptorDMA,
                                     uint32_t itHeaderStorageDMA,
                                     uint32_t itPayloadDMA,
                                     volatile uint32_t * itHeaderStorage,
                                     volatile uint32_t * itPayloadStorage,
                                     uint32_t sourceNodeIDField,
                                     const uint8_t * replayPeriod,
                                     uint32_t replayPeriodCount)
{
    gDigiLiveSourceNodeIDField = sourceNodeIDField;
    uint32_t dataBlockCounter = 0;
    for (uint32_t packet = 0; packet < kDigi00xDuplexITPacketCount; ++packet) {
        volatile OHCIAsyncDescriptor * packetDescriptor =
            itDescriptor + (packet * kDigi00xDuplexITDescriptorsPerPacket);
        uint32_t packetDescriptorDMA =
            itDescriptorDMA + packet * kDigi00xDuplexITDescriptorsPerPacket * sizeof(OHCIAsyncDescriptor);
        uint32_t dataBlocks =
            DigiLiveTransmitDataBlocksForPacket(packet, replayPeriod, replayPeriodCount);
        gDigiLiveTxDataBlocks[packet] = static_cast<uint8_t>(dataBlocks);
        uint32_t payloadLength = dataBlocks * kDigi00xDuplexDataBlockQuadlets * sizeof(uint32_t);
        uint32_t packetDataLength = 8u + payloadLength;
        uint32_t headerDMA = itHeaderStorageDMA + packet * 8u;
        uint32_t payloadDMA = itPayloadDMA + packet * kDigiLiveITPayloadStrideBytes;
        uint32_t cipHeader0 =
            sourceNodeIDField |
            (kDigi00xDuplexDataBlockQuadlets << 16) |
            (dataBlockCounter & 0xffu);
        uint32_t cipHeader1 =
            0x80000000u |
            (0x10u << 24) |
            (gDigi00xCurrentCIPSFC << 16) |
            0xffffu;

        packetDescriptor[0].reqCount = 8;
        packetDescriptor[0].control = kDescriptorKeyImmediate;
        packetDescriptor[0].dataAddress = 0;
        packetDescriptor[0].branchAddress = packetDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket;
        packetDescriptor[0].resCount = 8;
        packetDescriptor[0].transferStatus = 0;

        volatile uint32_t * immediateHeader = reinterpret_cast<volatile uint32_t *>(&packetDescriptor[1]);
        immediateHeader[0] = BuildIsoTransmitHeader0(kDigi00xDuplexDeviceReceiveChannel);
        immediateHeader[1] = BuildIsoTransmitHeader1(packetDataLength);

        packetDescriptor[2].reqCount = 8;
        packetDescriptor[2].control = 0;
        packetDescriptor[2].dataAddress = headerDMA;
        packetDescriptor[2].branchAddress = 0;
        packetDescriptor[2].resCount = 8;
        packetDescriptor[2].transferStatus = 0;

        uint32_t nextDescriptorDMA =
            packet == kDigi00xDuplexITPacketCount - 1
                ? itDescriptorDMA
                : (packetDescriptorDMA +
                   kDigi00xDuplexITDescriptorsPerPacket * sizeof(OHCIAsyncDescriptor));
        packetDescriptor[3].reqCount = static_cast<uint16_t>(payloadLength);
        packetDescriptor[3].control = kDescriptorOutputLast |
                                      kDescriptorStatus |
                                      kDescriptorBranchAlways |
                                      (packet == kDigi00xDuplexITPacketCount - 1 ? kDescriptorIrqAlways : 0);
        packetDescriptor[3].dataAddress = payloadDMA;
        packetDescriptor[3].branchAddress = nextDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket;
        packetDescriptor[3].resCount = static_cast<uint16_t>(payloadLength);
        packetDescriptor[3].transferStatus = 0;

        volatile uint32_t * cipHeader = itHeaderStorage + (packet * 2);
        cipHeader[0] = ToBigEndian32(cipHeader0);
        cipHeader[1] = ToBigEndian32(cipHeader1);

        volatile uint32_t * payload =
            itPayloadStorage + ((packet * kDigiLiveITPayloadStrideBytes) / sizeof(uint32_t));
        for (uint32_t block = 0; block < kDigiLiveITMaxDataBlocksPerPacket; ++block) {
            WriteDigiLiveSilentTransmitDataBlock(payload);
            payload += kDigi00xDuplexDataBlockQuadlets;
        }

        dataBlockCounter = (dataBlockCounter + dataBlocks) & 0xffu;
    }
}

void
ConfigureDigiLiveReceiveDescriptors(volatile OHCIAsyncDescriptor * irDescriptor,
                                    uint32_t irDescriptorDMA,
                                    uint32_t irDataDMA)
{
    for (uint32_t i = 0; i < kDigi00xDuplexIRDescriptorCount; ++i) {
        uint32_t descriptorDMA =
            irDescriptorDMA + i * kDigi00xDuplexIRDescriptorsPerPacketStorage * sizeof(OHCIAsyncDescriptor);
        uint32_t nextDescriptorDMA =
            i == kDigi00xDuplexIRDescriptorCount - 1
                ? irDescriptorDMA
                : (descriptorDMA +
                   kDigi00xDuplexIRDescriptorsPerPacketStorage * sizeof(OHCIAsyncDescriptor));
        volatile OHCIAsyncDescriptor * packetDescriptor =
            irDescriptor + i * kDigi00xDuplexIRDescriptorsPerPacketStorage;
        uint16_t receiveIRQ =
            kDigiLiveReceiveIRQInterval != 0 &&
                    ((i + 1) % kDigiLiveReceiveIRQInterval) == 0
                ? kDescriptorIrqAlways
                : 0;

        if (kDigiLiveSingleDescriptorReceiveEnabled != 0) {
            packetDescriptor[0].reqCount = kDigiLiveSingleIRDescriptorDataSize;
            packetDescriptor[0].control =
                kDescriptorInputLast | kDescriptorStatus | kDescriptorBranchAlways | receiveIRQ;
            packetDescriptor[0].dataAddress = irDataDMA + i * kDigiLiveIRDescriptorDataStride;
            packetDescriptor[0].branchAddress = nextDescriptorDMA | DigiLiveReceiveDescriptorBranchCount();
            packetDescriptor[0].resCount = kDigiLiveSingleIRDescriptorDataSize;
            packetDescriptor[0].transferStatus = 0;

            packetDescriptor[1].reqCount = 0;
            packetDescriptor[1].control = 0;
            packetDescriptor[1].dataAddress = 0;
            packetDescriptor[1].branchAddress = 0;
            packetDescriptor[1].resCount = 0;
            packetDescriptor[1].transferStatus = 0;
        } else {
            packetDescriptor[0].reqCount = kDigi00xDuplexIRHeaderSize;
            packetDescriptor[0].control = kDescriptorInputMore | kDescriptorStatus;
            packetDescriptor[0].dataAddress =
                descriptorDMA + kDigi00xDuplexIRDescriptorBranchCount * sizeof(OHCIAsyncDescriptor);
            packetDescriptor[0].branchAddress = 0;
            packetDescriptor[0].resCount = kDigi00xDuplexIRHeaderSize;
            packetDescriptor[0].transferStatus = 0;

            packetDescriptor[1].reqCount = kDigi00xDuplexIRDescriptorDataSize;
            packetDescriptor[1].control =
                kDescriptorInputLast | kDescriptorStatus | kDescriptorBranchAlways | receiveIRQ;
            packetDescriptor[1].dataAddress = irDataDMA + i * kDigiLiveIRDescriptorDataStride;
            packetDescriptor[1].branchAddress = nextDescriptorDMA | DigiLiveReceiveDescriptorBranchCount();
            packetDescriptor[1].resCount = kDigi00xDuplexIRDescriptorDataSize;
            packetDescriptor[1].transferStatus = 0;
        }

        packetDescriptor[2].reqCount = 0;
        packetDescriptor[2].control = 0;
        packetDescriptor[2].dataAddress = 0;
        packetDescriptor[2].branchAddress = 0;
        packetDescriptor[2].resCount = 0;
        packetDescriptor[2].transferStatus = 0;
    }
}

kern_return_t
StartDigiLiveIsoStream()
{
    gDigiLiveIsoStartStage = 1;
    gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnNotReady);
    if (gPCIDevice == nullptr || gPCIMemoryIndex == 0xff) {
        gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    gDigiLiveIsoStartStage = 2;
    CreateUncachedDMABuffer(gPCIDevice, kDigi00xDuplexBufferSize, &gDigiLiveBuffer);
    if (gDigiLiveBuffer.result != kIOReturnSuccess ||
        gDigiLiveBuffer.segmentCount == 0 ||
        gDigiLiveBuffer.cpuRange.address == 0 ||
        gDigiLiveBuffer.dmaSegment.address > 0xffffffffull ||
        gDigiLiveBuffer.dmaSegment.length < kDigi00xDuplexBufferSize) {
        gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnNoResources);
        return kIOReturnNoResources;
    }

    gDigiLiveIsoStartStage = 3;
    volatile OHCIAsyncDescriptor * itDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexITDescriptorOffset);
    volatile OHCIAsyncDescriptor * irDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexIRDescriptorOffset);
    uint32_t dmaBase = static_cast<uint32_t>(gDigiLiveBuffer.dmaSegment.address);
    uint32_t itDescriptorDMA = dmaBase + kDigi00xDuplexITDescriptorOffset;
    uint32_t irDescriptorDMA = dmaBase + kDigi00xDuplexIRDescriptorOffset;
    uint32_t itHeaderStorageDMA = dmaBase + kDigi00xDuplexITHeaderStorageOffset;
    uint32_t itPayloadDMA = dmaBase + kDigi00xDuplexITPayloadOffset;
    uint32_t irDataDMA = dmaBase + kDigi00xDuplexIRDataOffset;
    volatile uint32_t * itHeaderStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiLiveBuffer.cpuRange.address +
                                              kDigi00xDuplexITHeaderStorageOffset);
    volatile uint32_t * itPayloadStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiLiveBuffer.cpuRange.address +
                                              kDigi00xDuplexITPayloadOffset);

    uint32_t nodeID = 0;
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciNodeIdOffset, &nodeID);
    uint32_t sourceNodeIDField = (nodeID & 0x3fu) << 24;
    ConfigureDigiLiveTransmitDescriptors(itDescriptor,
                                         itDescriptorDMA,
                                         itHeaderStorageDMA,
                                         itPayloadDMA,
                                         itHeaderStorage,
                                         itPayloadStorage,
                                         sourceNodeIDField,
                                         nullptr,
                                         0);
    ConfigureDigiLiveReceiveDescriptors(irDescriptor, irDescriptorDMA, irDataDMA);

    gDigiLiveIsoStartStage = 4;
    gDigiLiveSyncForDeviceRet =
        ReturnCodeToProperty(SyncDMABufferForDevice(&gDigiLiveBuffer, kDigi00xDuplexBufferSize));
    if (gDigiLiveSyncForDeviceRet != ReturnCodeToProperty(kIOReturnSuccess)) {
        gDigiLiveIsoStartRet = gDigiLiveSyncForDeviceRet;
        return static_cast<kern_return_t>(gDigiLiveSyncForDeviceRet);
    }

    gDigiLiveIsoStartStage = 5;
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntMaskSetOffset, 0xffffffff);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoXmitIntMaskSetOffset, &gDigiLiveXmitMaskSupport);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntMaskClearOffset, 0xffffffff);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntMaskSetOffset, 0xffffffff);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoRecvIntMaskSetOffset, &gDigiLiveRecvMaskSupport);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntMaskClearOffset, 0xffffffff);

    uint32_t contextBit = 1u << kDigi00xDuplexContextIndex;
    gDigiLiveXmitContextSupported = (gDigiLiveXmitMaskSupport & contextBit) != 0 ? 1 : 0;
    gDigiLiveRecvContextSupported = (gDigiLiveRecvMaskSupport & contextBit) != 0 ? 1 : 0;
    if (gDigiLiveXmitContextSupported == 0 || gDigiLiveRecvContextSupported == 0) {
        gDigiLiveCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gDigiLiveBuffer));
        gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnUnsupported);
        return kIOReturnUnsupported;
    }
    if (kOHCIInterruptDispatchEnabled != 0 &&
        gOHCIInterruptReady == 0 &&
        gDriverInstance != nullptr) {
        (void)ConfigureOHCIInterruptDispatch(gDriverInstance, gPCIDevice);
    }

    gDigiLiveIsoStartStage = 6;
    uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
    uint64_t itControlClear = OhciIsoXmitContextControlClearOffset(kDigi00xDuplexContextIndex);
    uint64_t itCommandPtrOffset = OhciIsoXmitCommandPtrOffset(kDigi00xDuplexContextIndex);
    uint64_t irControlSet = OhciIsoRcvContextControlSetOffset(kDigi00xDuplexContextIndex);
    uint64_t irControlClear = OhciIsoRcvContextControlClearOffset(kDigi00xDuplexContextIndex);
    uint64_t irCommandPtrOffset = OhciIsoRcvCommandPtrOffset(kDigi00xDuplexContextIndex);
    uint64_t irContextMatchOffset = OhciIsoRcvContextMatchOffset(kDigi00xDuplexContextIndex);

    StopContext(gPCIDevice,
                gPCIMemoryIndex,
                itControlSet,
                itControlClear,
                &gDigiLiveITStopLoops,
                &gDigiLiveITControlAfterStop);
    StopContext(gPCIDevice,
                gPCIMemoryIndex,
                irControlSet,
                irControlClear,
                &gDigiLiveIRStopLoops,
                &gDigiLiveIRControlAfterStop);

    gDigiLiveIsoStartStage = 7;
    gDigiLiveITCommandPtr = itDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket;
    gDigiLiveIRCommandPtr = irDescriptorDMA | DigiLiveReceiveDescriptorBranchCount();
    gDigiLiveIRContextMatch = (0xfu << 28) | kDigi00xDuplexDeviceTransmitChannel;

    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itCommandPtrOffset, gDigiLiveITCommandPtr);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irCommandPtrOffset, gDigiLiveIRCommandPtr);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irContextMatchOffset, gDigiLiveIRContextMatch);

    if (kOHCIInterruptDispatchEnabled != 0) {
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIntMaskClearOffset, 0xffffffff);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIntEventClearOffset, 0x7fffffff);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex,
                                  kOhciIntMaskSetOffset,
                                  kOhciEventMasterIntEnable | kOhciIsochInterruptMask);
    }
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntEventClearOffset, contextBit);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntMaskSetOffset, contextBit);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irControlSet, kIrContextIsochHeader | kContextRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, irControlSet, &gDigiLiveIRControlAfterRun);

    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntEventClearOffset, contextBit);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntMaskSetOffset, contextBit);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlClear, 0xffffffff);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlSet, kContextRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, itControlSet, &gDigiLiveITControlAfterRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoXmitIntMaskSetOffset, &gDigiLiveITMaskAfterRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoRecvIntMaskSetOffset, &gDigiLiveIRMaskAfterRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoXmitIntEventSetOffset, &gDigiLiveITEventAfterRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoRecvIntEventSetOffset, &gDigiLiveIREventAfterRun);

    gDigiLiveIRReadIndex = 0;
    ResetDigiLiveIRCommandPtrCursor();
    gDigiLiveReady = 1;
    gDigiLiveRunning = 1;
    gDigiLiveState = kDigiLiveStateRunning;
    if (kOHCIInterruptDispatchEnabled != 0) {
        EnableOHCIInterruptDispatch();
    }
    gDigiLiveIsoStartStage = 8;
    gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnSuccess);
    return kIOReturnSuccess;
}

kern_return_t
StartDigiLiveStreamForAudio()
{
    if (gDigiLiveRunning != 0) {
        return kIOReturnSuccess;
    }
    if (gDigiLiveStarting != 0) {
        return kIOReturnBusy;
    }

    gDigiLiveStarting = 1;
    gDigiLiveState = kDigiLiveStateStarting;
    gDigiLiveReady = 0;
    gDigiLiveStartStage = 1;
    gDigiLiveBeginTransactionStage = 0;
    gDigiLiveBeginTransactionRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveIsoStartStage = 0;
    gDigiLiveIsoStartRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveStartAttemptCount++;
    gDigiLiveStartRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
    gDigiLiveIRReadIndex = 0;
    ResetDigiLiveIRCommandPtrCursor();
    gDigiLiveLastDescriptorIndex = 0xffffffff;
    gDigiLiveLastDescriptorBytes = 0;
    gDigiLiveLastDescriptorDataBlocks = 0;
    gDigiLiveLastHarvestPackets = 0;
    gDigiLiveLastHarvestFrames = 0;
    gDigiLiveLastHarvestBytes = 0;
    gDigiLiveLastHarvestPeakAbs = 0;
    gDigiLiveLastHarvestLabelMismatchCount = 0;
    ResetDigiLiveReceiveStreamDiagnostics();
    ResetDigiLiveSequenceReplayState();
    ResetDigiLiveOutputState();

    gDigiLiveStartStage = 2;
    kern_return_t ret = RunDigiLiveBeginTransactions();
    gDigiLiveBeginTransactionRet = ReturnCodeToProperty(ret);
    gDigiLiveStartStage = 3;
    if (ret == kIOReturnSuccess) {
        ret = StartDigiLiveIsoStream();
        gDigiLiveIsoStartRet = ReturnCodeToProperty(ret);
    }
    gDigiLiveStartStage = 4;
    if (ret == kIOReturnSuccess) {
        gDigiLiveStartSuccessCount++;
    } else {
        gDigiLiveRunning = 0;
        gDigiLiveReady = 0;
        gDigiLiveState = kDigiLiveStateStopped;
        (void)RunDigiLiveFinishTransactions();
        (void)CompleteDMABufferMapping(&gDigiLiveBuffer);
        ReleaseDMABuffer(&gDigiLiveBuffer);
    }
    gDigiLiveStartRet = ReturnCodeToProperty(ret);
    gDigiLiveStarting = 0;
    gDigiLiveStartStage = ret == kIOReturnSuccess ? 5 : 6;
    PublishAudioRuntimeDiagnostics();
    return ret;
}

void
ResetDigiLiveDescriptor(volatile OHCIAsyncDescriptor * packetDescriptor)
{
    volatile uint32_t * packetHeader = DigiLiveReceiveHeaderFor(packetDescriptor);
    for (uint32_t word = 0; word < kDigi00xDuplexIRSampleHeaderWordCount; ++word) {
        packetHeader[word] = 0;
    }
    packetDescriptor[0].resCount = kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? kDigiLiveSingleIRDescriptorDataSize
        : kDigi00xDuplexIRHeaderSize;
    packetDescriptor[0].transferStatus = 0;
    packetDescriptor[1].resCount = kDigiLiveSingleDescriptorReceiveEnabled != 0
        ? 0
        : kDigi00xDuplexIRDescriptorDataSize;
    packetDescriptor[1].transferStatus = 0;
    __sync_synchronize();
}

kern_return_t
HarvestDigiLiveIsoStream()
{
    if (gDigiLiveRunning == 0 ||
        gDigiLiveBuffer.cpuRange.address == 0 ||
        gDigiLiveBuffer.command == nullptr) {
        gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }

    if (__sync_lock_test_and_set(&gDigiLiveDrainBusy, 1) != 0) {
        gDigiLiveDrainBusyCount++;
        gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnBusy);
        return kIOReturnBusy;
    }

    if (kDigiLiveIREventPollingEnabled != 0 &&
        gPCIDevice != nullptr &&
        gPCIMemoryIndex != 0xff) {
        uint32_t contextBit = 1u << kDigi00xDuplexContextIndex;
        uint32_t itEvent = 0;
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoXmitIntEventSetOffset, &itEvent);
        gDigiLiveITEventPollCount++;
        gDigiLiveITEventLastBeforeHarvest = itEvent;
        if ((itEvent & contextBit) != 0) {
            gDigiLiveITEventHitCount++;
            gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntEventClearOffset, contextBit);
            gDigiLiveITEventClearCount++;
            gPCIDevice->MemoryRead32(gPCIMemoryIndex,
                                     kOhciIsoXmitIntEventSetOffset,
                                     &gDigiLiveITEventLastAfterClear);
        } else {
            gDigiLiveITEventMissCount++;
        }

        uint32_t irEvent = 0;
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoRecvIntEventSetOffset, &irEvent);
        gDigiLiveIREventPollCount++;
        gDigiLiveIREventLastBeforeHarvest = irEvent;
        if ((irEvent & contextBit) != 0) {
            gDigiLiveIREventHitCount++;
            gDigiLiveIREventConsecutiveMissCount = 0;
            gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntEventClearOffset, contextBit);
            gDigiLiveIREventClearCount++;
            gPCIDevice->MemoryRead32(gPCIMemoryIndex,
                                     kOhciIsoRecvIntEventSetOffset,
                                     &gDigiLiveIREventLastAfterClear);
        } else {
            gDigiLiveIREventMissCount++;
            gDigiLiveIREventConsecutiveMissCount++;
            bool lowWaterBypass =
                kDigiLiveIREventLowWaterBypassEnabled != 0 &&
                gAudioRingCurrentFillFrames < kDigiLiveIREventLowWaterBypassFrames;
            if (kDigiLiveRequireIREventBeforeSync != 0 &&
                !lowWaterBypass &&
                gDigiLiveLastHarvestPackets < kDigiLiveHarvestMaxDescriptorsPerPass &&
                gDigiLiveIREventConsecutiveMissCount < kDigiLiveIREventGateMissBypassCount) {
                gDigiLiveIREventGateSkipCount++;
                gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
                __sync_lock_release(&gDigiLiveDrainBusy);
                return kIOReturnNotReady;
            }
            if (kDigiLiveRequireIREventBeforeSync != 0) {
                gDigiLiveIREventGateBypassCount++;
            }
        }
    }

    uint64_t descriptorSyncSize =
        kDigi00xDuplexIRDescriptorOffset +
        kDigi00xDuplexIRDescriptorCount *
            kDigi00xDuplexIRDescriptorsPerPacketStorage *
            sizeof(OHCIAsyncDescriptor);

    gDigiLiveDrainAttemptCount++;
    gDigiLiveHarvestAttemptCount++;
    kern_return_t syncRet = kDigiLiveReceiveFullBufferSyncEnabled != 0
        ? SyncDMABufferForCPU(&gDigiLiveBuffer, kDigiLiveReceiveSyncSize)
        : SyncDMABufferForCPURange(&gDigiLiveBuffer, 0, descriptorSyncSize);
    gDigiLiveSyncForCPURet = ReturnCodeToProperty(syncRet);
    gDigiLiveRxDescriptorSyncForCPURet = ReturnCodeToProperty(syncRet);
    if (syncRet != kIOReturnSuccess) {
        gDigiLiveLastHarvestRet = ReturnCodeToProperty(syncRet);
        __sync_lock_release(&gDigiLiveDrainBusy);
        return static_cast<kern_return_t>(gDigiLiveSyncForCPURet);
    }
    gDigiLiveRxDescriptorSyncCount++;
    gDigiLiveRxDescriptorSyncBytes += kDigiLiveReceiveFullBufferSyncEnabled != 0
        ? kDigiLiveReceiveSyncSize
        : descriptorSyncSize;

    volatile OHCIAsyncDescriptor * irDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexIRDescriptorOffset);
    volatile OHCIAsyncDescriptor * itDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexITDescriptorOffset);
    uint32_t dmaBase = static_cast<uint32_t>(gDigiLiveBuffer.dmaSegment.address);
    uint32_t irDescriptorDMA = dmaBase + kDigi00xDuplexIRDescriptorOffset;
    gPCIDevice->MemoryRead32(gPCIMemoryIndex,
                             OhciIsoXmitCommandPtrOffset(kDigi00xDuplexContextIndex),
                             &gDigiLiveITCommandPtrLastRead);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex,
                             OhciIsoRcvCommandPtrOffset(kDigi00xDuplexContextIndex),
                             &gDigiLiveIRCommandPtrLastRead);
    UpdateDigiLiveIRCommandPtrCursor(gDigiLiveIRCommandPtrLastRead, irDescriptorDMA);
    volatile OHCIAsyncDescriptor * firstITPacket = itDescriptor;
    volatile OHCIAsyncDescriptor * lastITPacket =
        itDescriptor + ((kDigi00xDuplexITPacketCount - 1) * kDigi00xDuplexITDescriptorsPerPacket);
    gDigiLiveITFirstPayloadResCount = firstITPacket[3].resCount;
    gDigiLiveITFirstPayloadStatus = firstITPacket[3].transferStatus;
    gDigiLiveITLastPayloadResCount = lastITPacket[3].resCount;
    gDigiLiveITLastPayloadStatus = lastITPacket[3].transferStatus;
    uint32_t packetCount = 0;
    uint32_t frameCount = 0;
    uint32_t byteCount = 0;
    uint32_t peakAbs = 0;
    uint32_t labelMismatchCount = 0;
    kern_return_t harvestErrorRet = kIOReturnSuccess;
    gAudioRingLastAppendDroppedFrames = 0;

    for (uint32_t pass = 0; pass < kDigiLiveHarvestMaxDescriptorsPerPass; ++pass) {
        uint32_t index = gDigiLiveIRReadIndex;
        DigiLiveReceivePacket packet =
            DigiLiveReceivePacketAt(irDescriptor, gDigiLiveBuffer.cpuRange.address, index);
        volatile OHCIAsyncDescriptor * packetDescriptor = packet.descriptor;
        uint32_t headerResCount = kDigiLiveSingleDescriptorReceiveEnabled != 0
            ? 0
            : packetDescriptor[0].resCount;
        uint32_t payloadResCount = kDigiLiveSingleDescriptorReceiveEnabled != 0
            ? packetDescriptor[0].resCount
            : packetDescriptor[1].resCount;
        uint32_t headerStatus = kDigiLiveSingleDescriptorReceiveEnabled != 0
            ? 0
            : packetDescriptor[0].transferStatus;
        uint32_t payloadStatus = kDigiLiveSingleDescriptorReceiveEnabled != 0
            ? packetDescriptor[0].transferStatus
            : packetDescriptor[1].transferStatus;
        uint32_t descriptorBytes = DigiLiveReceiveDescriptorBytes(payloadResCount);
        uint32_t payloadBytes = DigiLiveReceivePayloadBytes(descriptorBytes);
        uint32_t dataBlocks = DigiLiveReceiveDataBlocks(payloadBytes);

        if (DigiLiveReceiveDescriptorIsEmpty(descriptorBytes, headerStatus, payloadStatus)) {
            if (CatchUpDigiLiveIRReadIndexAfterEmptyDescriptor(irDescriptor,
                                                               gDigiLiveBuffer.cpuRange.address)) {
                continue;
            }
            break;
        }

        gDigiLiveLastDescriptorIndex = index;
        gDigiLiveLastDescriptorBytes = payloadBytes;
        gDigiLiveLastDescriptorDataBlocks = dataBlocks;
        gDigiLiveLastDescriptorHeaderStatus = headerStatus;
        gDigiLiveLastDescriptorPayloadStatus = payloadStatus;
        gDigiLiveLastDescriptorHeaderResCount = headerResCount;
        gDigiLiveLastDescriptorPayloadResCount = payloadResCount;
        uint64_t dbcLostBefore = gDigiLiveRxDBCLostCount;
        uint64_t cycleLostBefore = gDigiLiveRxCycleLostCount;
        UpdateDigiLiveReceiveTimingDiagnostics(packet.header, payloadBytes, dataBlocks);
        bool continuousForReplay =
            gDigiLiveRxDBCLostCount == dbcLostBefore &&
            gDigiLiveRxCycleLostCount == cycleLostBefore;
        RecordDigiLiveRxCadencePacket(gDigiLiveRxEventCount, continuousForReplay);
        RecordDigiLiveSequenceReplayPacket(gDigiLiveRxEventCount, continuousForReplay);

        if (dataBlocks != 0) {
            if (kDigiLiveReceiveFullBufferSyncEnabled == 0 &&
                kDigiLiveReceivePayloadRangeSyncEnabled != 0) {
                uint64_t payloadOffset =
                    kDigi00xDuplexIRDataOffset +
                    static_cast<uint64_t>(index) * kDigiLiveIRDescriptorDataStride;
                kern_return_t payloadSyncRet =
                    SyncDMABufferForCPURange(&gDigiLiveBuffer, payloadOffset, payloadBytes);
                gDigiLiveRxPayloadSyncForCPURet = ReturnCodeToProperty(payloadSyncRet);
                if (payloadSyncRet != kIOReturnSuccess) {
                    harvestErrorRet = payloadSyncRet;
                    break;
                }
                gDigiLiveRxPayloadSyncCount++;
                gDigiLiveRxPayloadSyncBytes += payloadBytes;
            }
            volatile uint32_t * packetPayload = packet.payload;
            for (uint32_t block = 0; block < dataBlocks; ++block) {
                volatile uint32_t * dataBlock =
                    packetPayload + block * kDigi00xDuplexDataBlockQuadlets;
                int32_t samples[kDigi00xDuplexIRCapturePCMChannelCount] = {};
                uint32_t captureFrame = frameCount;
                uint32_t slot0WordBE = ToBigEndian32(dataBlock[0]);
                uint32_t slot0Value24 = slot0WordBE & 0x00ffffffu;
                if (slot0Value24 != 0) {
                    gDigiLiveSlot0LastLabel = (slot0WordBE >> 24) & 0xffu;
                    gDigiLiveSlot0LastValue24 = slot0Value24;
                    gDigiLiveSlot0NonzeroCount++;
                }
                ObserveDigiLiveMidiSlot0(slot0WordBE);
                for (uint32_t channel = 0; channel < kDigi00xDuplexIRCapturePCMChannelCount; ++channel) {
                    uint32_t wordBE = ToBigEndian32(dataBlock[1 + channel]);
                    uint32_t label = (wordBE >> 24) & 0xffu;
                    if (label != kDigi00xDuplexAM824AudioLabel) {
                        labelMismatchCount++;
                    }
                    int32_t sample = Signed24ToInt32(wordBE);
                    uint32_t sampleAbs = AbsoluteInt32(sample);
                    if (sampleAbs > peakAbs) {
                        peakAbs = sampleAbs;
                    }
                    samples[channel] = sample;
                    if (captureFrame < kDigi00xDuplexIRCapturePCMFrameLimit) {
                        gAudioCapturePCM[captureFrame][channel] = sample;
                    }
                }
                AppendPCMFrameToAudioRing(samples);
                frameCount++;
            }
            packetCount++;
            byteCount += payloadBytes;
        }

        ResetDigiLiveDescriptor(packetDescriptor);
        gDigiLiveDescriptorResetCount++;
        gDigiLiveIRSoftwarePacketCursor++;
        gDigiLiveIRReadIndex =
            static_cast<uint32_t>(gDigiLiveIRSoftwarePacketCursor %
                                  kDigi00xDuplexIRDescriptorCount);
        if (gDigiLiveIRHardwarePacketCursor > gDigiLiveIRSoftwarePacketCursor) {
            uint64_t backlog = gDigiLiveIRHardwarePacketCursor - gDigiLiveIRSoftwarePacketCursor;
            gDigiLiveIRBacklogPackets = backlog > 0xffffffffull
                ? 0xffffffffu
                : static_cast<uint32_t>(backlog);
        } else {
            gDigiLiveIRBacklogPackets = 0;
        }
    }

    if (harvestErrorRet != kIOReturnSuccess) {
        gDigiLiveLastHarvestRet = ReturnCodeToProperty(harvestErrorRet);
    } else if (packetCount != 0 || frameCount != 0) {
        gAudioCaptureFrameCount = frameCount;
        gAudioCapturePeakAbs = peakAbs;
        gAudioCaptureGeneration++;
        UpdateAudioRingFillAfterAppend(frameCount);
        gDigiLiveHarvestSuccessCount++;
        gDigiLiveHarvestPacketCount += packetCount;
        gDigiLiveHarvestFrameCount += frameCount;
        gDigiLiveHarvestRxBytes += byteCount;
        gDigiLiveLastHarvestPackets = packetCount;
        gDigiLiveLastHarvestFrames = frameCount;
        gDigiLiveLastHarvestBytes = byteCount;
        gDigiLiveLastHarvestPeakAbs = peakAbs;
        gDigiLiveLastHarvestLabelMismatchCount = labelMismatchCount;
        if (peakAbs > gDigiLiveHarvestPeakAbs) {
            gDigiLiveHarvestPeakAbs = peakAbs;
        }
        gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnSuccess);
    } else {
        gAudioRingLastAppendFrames = 0;
        gAudioRingLastAppendDroppedFrames = 0;
        gDigiLiveEmptyPollCount++;
        gDigiLiveLastHarvestPackets = 0;
        gDigiLiveLastHarvestFrames = 0;
        gDigiLiveLastHarvestBytes = 0;
        gDigiLiveLastHarvestPeakAbs = 0;
        gDigiLiveLastHarvestLabelMismatchCount = 0;
        gDigiLiveLastHarvestRet = ReturnCodeToProperty(kIOReturnNotReady);
    }

    if (packetCount != 0) {
        if (kDigiLiveSequenceReplayMovingEnabled != 0) {
            (void)RefreshDigiLiveMovingSequenceReplay();
        }
        gDigiLiveSyncForDeviceRet =
            ReturnCodeToProperty(SyncDMABufferForDevice(&gDigiLiveBuffer, descriptorSyncSize));
        uint64_t irControlSet = OhciIsoRcvContextControlSetOffset(kDigi00xDuplexContextIndex);
        uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irControlSet, kContextWake);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlSet, kContextWake);
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, irControlSet, &gDigiLiveIRControlAfterRun);
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, itControlSet, &gDigiLiveITControlAfterRun);
    }

    kern_return_t ret = static_cast<kern_return_t>(gDigiLiveLastHarvestRet);
    __sync_lock_release(&gDigiLiveDrainBusy);
    return ret;
}

kern_return_t
ApplyDigiLiveSequenceReplay()
{
    if (kDigiLiveSequenceReplayEnabled == 0 ||
        gDigiLiveSequenceReplayReady == 0 ||
        gDigiLiveSequenceReplayActive != 0 ||
        gDigiLiveRunning == 0 ||
        gDigiLiveBuffer.cpuRange.address == 0 ||
        gDigiLiveBuffer.command == nullptr ||
        gPCIDevice == nullptr ||
        gPCIMemoryIndex == 0xff) {
        return kIOReturnNotReady;
    }
    if (gDigiLiveSequenceReplayApplyAttemptCount != 0) {
        return kIOReturnBusy;
    }

    gDigiLiveSequenceReplayApplyAttemptCount++;
    gDigiLiveSequenceReplayApplyRet = ReturnCodeToProperty(kIOReturnNotReady);

    volatile OHCIAsyncDescriptor * itDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexITDescriptorOffset);
    volatile OHCIAsyncDescriptor * irDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gDigiLiveBuffer.cpuRange.address +
                                                        kDigi00xDuplexIRDescriptorOffset);
    uint32_t dmaBase = static_cast<uint32_t>(gDigiLiveBuffer.dmaSegment.address);
    uint32_t itDescriptorDMA = dmaBase + kDigi00xDuplexITDescriptorOffset;
    uint32_t irDescriptorDMA = dmaBase + kDigi00xDuplexIRDescriptorOffset;
    uint32_t itHeaderStorageDMA = dmaBase + kDigi00xDuplexITHeaderStorageOffset;
    uint32_t itPayloadDMA = dmaBase + kDigi00xDuplexITPayloadOffset;
    uint32_t irDataDMA = dmaBase + kDigi00xDuplexIRDataOffset;
    volatile uint32_t * itHeaderStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiLiveBuffer.cpuRange.address +
                                              kDigi00xDuplexITHeaderStorageOffset);
    volatile uint32_t * itPayloadStorage =
        reinterpret_cast<volatile uint32_t *>(gDigiLiveBuffer.cpuRange.address +
                                              kDigi00xDuplexITPayloadOffset);

    uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
    uint64_t itControlClear = OhciIsoXmitContextControlClearOffset(kDigi00xDuplexContextIndex);
    uint64_t itCommandPtrOffset = OhciIsoXmitCommandPtrOffset(kDigi00xDuplexContextIndex);
    uint64_t irControlSet = OhciIsoRcvContextControlSetOffset(kDigi00xDuplexContextIndex);
    uint64_t irControlClear = OhciIsoRcvContextControlClearOffset(kDigi00xDuplexContextIndex);
    uint64_t irCommandPtrOffset = OhciIsoRcvCommandPtrOffset(kDigi00xDuplexContextIndex);
    uint64_t irContextMatchOffset = OhciIsoRcvContextMatchOffset(kDigi00xDuplexContextIndex);
    uint32_t contextBit = 1u << kDigi00xDuplexContextIndex;

    StopContext(gPCIDevice,
                gPCIMemoryIndex,
                itControlSet,
                itControlClear,
                &gDigiLiveITStopLoops,
                &gDigiLiveITControlAfterStop);
    StopContext(gPCIDevice,
                gPCIMemoryIndex,
                irControlSet,
                irControlClear,
                &gDigiLiveIRStopLoops,
                &gDigiLiveIRControlAfterStop);

    uint32_t nodeID = 0;
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciNodeIdOffset, &nodeID);
    uint32_t sourceNodeIDField = (nodeID & 0x3fu) << 24;
    ConfigureDigiLiveTransmitDescriptors(itDescriptor,
                                         itDescriptorDMA,
                                         itHeaderStorageDMA,
                                         itPayloadDMA,
                                         itHeaderStorage,
                                         itPayloadStorage,
                                         sourceNodeIDField,
                                         gDigiLiveSequenceReplayPeriod,
                                         kDigiLiveSequenceReplayPeriodPackets);
    ConfigureDigiLiveReceiveDescriptors(irDescriptor, irDescriptorDMA, irDataDMA);
    gDigiLiveIRReadIndex = 0;
    ResetDigiLiveIRCommandPtrCursor();

    kern_return_t syncRet = SyncDMABufferForDevice(&gDigiLiveBuffer, kDigi00xDuplexBufferSize);
    gDigiLiveSyncForDeviceRet = ReturnCodeToProperty(syncRet);
    if (syncRet != kIOReturnSuccess) {
        gDigiLiveSequenceReplayApplyRet = ReturnCodeToProperty(syncRet);
        return syncRet;
    }

    gDigiLiveITCommandPtr = itDescriptorDMA | kDigi00xDuplexITDescriptorsPerPacket;
    gDigiLiveIRCommandPtr = irDescriptorDMA | DigiLiveReceiveDescriptorBranchCount();
    gDigiLiveIRContextMatch = (0xfu << 28) | kDigi00xDuplexDeviceTransmitChannel;
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itCommandPtrOffset, gDigiLiveITCommandPtr);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irCommandPtrOffset, gDigiLiveIRCommandPtr);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irContextMatchOffset, gDigiLiveIRContextMatch);

    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntEventClearOffset, contextBit);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntEventClearOffset, contextBit);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, irControlSet, kIrContextIsochHeader | kContextRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, irControlSet, &gDigiLiveIRControlAfterRun);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlClear, 0xffffffff);
    gPCIDevice->MemoryWrite32(gPCIMemoryIndex, itControlSet, kContextRun);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, itControlSet, &gDigiLiveITControlAfterRun);

    gDigiLiveSequenceReplayActive = 1;
    gDigiLiveSequenceReplayApplySuccessCount++;
    gDigiLiveSequenceReplayApplyRet = ReturnCodeToProperty(kIOReturnSuccess);
    return kIOReturnSuccess;
}

void
PrebufferDigiLiveAudio()
{
    if (gDigiLiveRunning == 0) {
        return;
    }

    for (uint32_t attempt = 0;
         attempt < kDigiLivePrebufferAttemptCount &&
         gDigiLiveRunning != 0;
         ++attempt) {
        bool replayApplyPending =
            kDigiLiveSequenceReplayEnabled != 0 &&
            gDigiLiveSequenceReplayActive == 0 &&
            gDigiLiveSequenceReplayApplyAttemptCount == 0;
        if (gAudioRingCurrentFillFrames >= kDigiLivePrebufferTargetFrames &&
            !replayApplyPending) {
            break;
        }

        kern_return_t ret = HarvestDigiLiveIsoStream();
        if (gDigiLiveSequenceReplayReady != 0 &&
            gDigiLiveSequenceReplayActive == 0 &&
            gDigiLiveSequenceReplayApplyAttemptCount == 0) {
            kern_return_t replayRet = ApplyDigiLiveSequenceReplay();
            if (replayRet == kIOReturnSuccess) {
                ResetAudioRingBuffer();
                ClearAudioInputBuffer();
            }
        }
        IOSleep(ret == kIOReturnSuccess ? 1 : 2);
    }
    PublishAudioRuntimeDiagnostics();
}

kern_return_t
StopDigiLiveStreamForAudio()
{
    gDigiLiveStopAttemptCount++;
    if (gDigiLiveStopping != 0) {
        gDigiLiveStopRet = ReturnCodeToProperty(kIOReturnBusy);
        PublishAudioRuntimeDiagnostics();
        return kIOReturnBusy;
    }

    gDigiLiveStopping = 1;
    gDigiLiveState = kDigiLiveStateStopping;
    kern_return_t ret = kIOReturnSuccess;
    gDigiLiveRunning = 0;
    gDigiLiveReady = 0;
    PublishAudioRuntimeDiagnostics();

    if (gPCIDevice != nullptr && gPCIMemoryIndex != 0xff) {
        uint32_t contextBit = 1u << kDigi00xDuplexContextIndex;
        uint64_t itControlSet = OhciIsoXmitContextControlSetOffset(kDigi00xDuplexContextIndex);
        uint64_t itControlClear = OhciIsoXmitContextControlClearOffset(kDigi00xDuplexContextIndex);
        uint64_t irControlSet = OhciIsoRcvContextControlSetOffset(kDigi00xDuplexContextIndex);
        uint64_t irControlClear = OhciIsoRcvContextControlClearOffset(kDigi00xDuplexContextIndex);
        StopContext(gPCIDevice,
                    gPCIMemoryIndex,
                    itControlSet,
                    itControlClear,
                    &gDigiLiveITStopLoops,
                    &gDigiLiveITControlAfterStop);
        StopContext(gPCIDevice,
                    gPCIMemoryIndex,
                    irControlSet,
                    irControlClear,
                    &gDigiLiveIRStopLoops,
                    &gDigiLiveIRControlAfterStop);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntMaskClearOffset, contextBit);
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntMaskClearOffset, contextBit);
        if (kOHCIInterruptDispatchEnabled != 0) {
            gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIntMaskClearOffset, 0xffffffff);
            gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIntEventClearOffset, 0x7fffffff);
        }
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoXmitIntMaskSetOffset, &gDigiLiveITMaskAfterClear);
        gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoRecvIntMaskSetOffset, &gDigiLiveIRMaskAfterClear);
    }

    gDigiLiveStopDrainWaitLoops = 0;
    for (uint32_t i = 0; i < 1000 && gDigiLiveDrainBusy != 0; ++i) {
        gDigiLiveStopDrainWaitLoops = i + 1;
        IODelay(10);
    }

    gDigiLiveCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gDigiLiveBuffer));
    ReleaseDMABuffer(&gDigiLiveBuffer);
    gDigiLiveRunning = 0;
    gDigiLiveReady = 0;
    if (ret == kIOReturnSuccess) {
        gDigiLiveStopSuccessCount++;
    }
    gDigiLiveStopRet = ReturnCodeToProperty(ret);
    gDigiLiveStopping = 0;
    gDigiLiveState = kDigiLiveStateStopped;
    PublishAudioRuntimeDiagnostics();
    return ret;
}

void
RunAsyncConfigROMRead(IOPCIDevice * pciDevice,
                      uint8_t memoryIndex,
                      uint32_t nodeID,
                      AsyncReadDiagnostics * diagnostics,
                      DigiDuplexDiagnostics * duplexDiagnostics)
{
    diagnostics->attempted = 1;
    diagnostics->localNodeID = nodeID & 0xffffu;
    diagnostics->destinationID = diagnostics->localNodeID & 0xffc0u;
    diagnostics->configuredAttempts = kAsyncReadAttemptCount;
    diagnostics->waitLoopsPerAttempt = kAsyncReadWaitLoopsPerAttempt;
    diagnostics->retrySettleMilliseconds = kAsyncReadRetrySettleMilliseconds;
    uint64_t offset = kCSRRegisterBase | kCSRConfigROM;
    diagnostics->offsetHi = static_cast<uint32_t>(offset >> 32);
    diagnostics->offsetLo = static_cast<uint32_t>(offset & 0xffffffffu);

    CreateDMABuffer(pciDevice, kAsyncRxBufferSize, &gAsyncRxBuffer);
    CreateDMABuffer(pciDevice, kAsyncRxBufferSize, &gAsyncReqRxBuffer);
    CreateDMABuffer(pciDevice, kAsyncTxBufferSize, &gAsyncTxBuffer);
    if (gAsyncRxBuffer.result != kIOReturnSuccess ||
        gAsyncReqRxBuffer.result != kIOReturnSuccess ||
        gAsyncTxBuffer.result != kIOReturnSuccess ||
        gAsyncRxBuffer.cpuRange.address == 0 ||
        gAsyncReqRxBuffer.cpuRange.address == 0 ||
        gAsyncTxBuffer.cpuRange.address == 0 ||
        gAsyncRxBuffer.segmentCount != 1 ||
        gAsyncReqRxBuffer.segmentCount != 1 ||
        gAsyncTxBuffer.segmentCount != 1 ||
        gAsyncRxBuffer.dmaSegment.length < kAsyncRxBufferSize ||
        gAsyncReqRxBuffer.dmaSegment.length < kAsyncRxBufferSize ||
        gAsyncTxBuffer.dmaSegment.length < kAsyncTxBufferSize ||
        gAsyncRxBuffer.dmaSegment.address > 0xffffffffull ||
        gAsyncReqRxBuffer.dmaSegment.address > 0xffffffffull ||
        gAsyncTxBuffer.dmaSegment.address > 0xffffffffull) {
        return;
    }
    diagnostics->ready = 1;

    StopContext(pciDevice,
                memoryIndex,
                kOhciAsReqTrContextControlSetOffset,
                kOhciAsReqTrContextControlClearOffset,
                &diagnostics->txContextStopLoops,
                &diagnostics->txContextAfterClear);
    StopContext(pciDevice,
                memoryIndex,
                kOhciAsReqRcvContextControlSetOffset,
                kOhciAsReqRcvContextControlClearOffset,
                &diagnostics->reqRxContextStopLoops,
                &diagnostics->reqRxContextAfterClear);
    StopContext(pciDevice,
                memoryIndex,
                kOhciAsRspRcvContextControlSetOffset,
                kOhciAsRspRcvContextControlClearOffset,
                &diagnostics->rxContextStopLoops,
                &diagnostics->rxContextAfterClear);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
    pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskSetOffset, kOhciDiagnosticInterruptMask);

    volatile OHCIAsyncDescriptor * rxDescriptors = nullptr;
    volatile uint32_t * rxData0 = nullptr;
    volatile uint32_t * rxData1 = nullptr;
    volatile OHCIAsyncDescriptor * reqRxDescriptors = nullptr;
    volatile uint32_t * reqRxData0 = nullptr;
    volatile uint32_t * reqRxData1 = nullptr;

    volatile OHCIAsyncDescriptor * txDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gAsyncTxBuffer.cpuRange.address);
    volatile uint32_t * txHeader =
        reinterpret_cast<volatile uint32_t *>(gAsyncTxBuffer.cpuRange.address + sizeof(OHCIAsyncDescriptor));
    uint32_t txDescriptorDMA = static_cast<uint32_t>(gAsyncTxBuffer.dmaSegment.address);

    diagnostics->txCommandPtr = txDescriptorDMA | 2u;
    diagnostics->configROMReadCount = kConfigROMProbeReadCount;

    for (size_t readIndex = 0; readIndex < kConfigROMProbeReadCount; ++readIndex) {
        bool readSucceeded = false;
        diagnostics->configROMLastIndex = static_cast<uint32_t>(readIndex);
        for (uint32_t attempt = 0; attempt < kAsyncReadAttemptCount; ++attempt) {
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqTrContextControlSetOffset,
                        kOhciAsReqTrContextControlClearOffset,
                        &diagnostics->txContextStopLoops,
                        &diagnostics->txContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqRcvContextControlSetOffset,
                        kOhciAsReqRcvContextControlClearOffset,
                        &diagnostics->reqRxContextStopLoops,
                        &diagnostics->reqRxContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsRspRcvContextControlSetOffset,
                        kOhciAsRspRcvContextControlClearOffset,
                        &diagnostics->rxContextStopLoops,
                        &diagnostics->rxContextAfterClear);
            ConfigureAsyncReceiveBuffer(&gAsyncRxBuffer,
                                        &diagnostics->rxCommandPtr,
                                        &rxDescriptors,
                                        &rxData0,
                                        &rxData1);
            ConfigureAsyncReceiveBuffer(&gAsyncReqRxBuffer,
                                        &diagnostics->reqRxCommandPtr,
                                        &reqRxDescriptors,
                                        &reqRxData0,
                                        &reqRxData1);

            diagnostics->lastAttempt = attempt;
            diagnostics->completedAttempts += 1;
            diagnostics->rxDescriptor0Control = rxDescriptors[0].control;
            diagnostics->rxDescriptor0DataAddress = rxDescriptors[0].dataAddress;
            diagnostics->rxDescriptor0BranchAddress = rxDescriptors[0].branchAddress;
            diagnostics->rxDescriptor1Control = rxDescriptors[1].control;
            diagnostics->rxDescriptor1DataAddress = rxDescriptors[1].dataAddress;
            diagnostics->rxDescriptor1BranchAddress = rxDescriptors[1].branchAddress;
            diagnostics->rxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncRxBuffer,
                                                                                          kAsyncRxBufferSize));
            diagnostics->reqRxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncReqRxBuffer,
                                                                                             kAsyncRxBufferSize));

            txDescriptor[0].reqCount = 12;
            txDescriptor[0].control = kDescriptorKeyImmediate |
                                      kDescriptorOutputLast |
                                      kDescriptorIrqAlways |
                                      kDescriptorBranchAlways;
            txDescriptor[0].dataAddress = 0;
            txDescriptor[0].branchAddress = 0;
            txDescriptor[0].resCount = 0;
            txDescriptor[0].transferStatus = 0;
            uint64_t readOffset = offset + (readIndex * sizeof(uint32_t));
            uint32_t tlabel =
                (kAsyncReadTLabel + static_cast<uint32_t>(readIndex * kAsyncReadAttemptCount) + attempt) & 0x3fu;
            txHeader[0] = BuildATQuadlet0(kSCode400, tlabel, kRetryX, kTCodeReadQuadletRequest);
            txHeader[1] = BuildATQuadlet1(diagnostics->destinationID, readOffset);
            txHeader[2] = static_cast<uint32_t>(readOffset & 0xffffffffu);
            txHeader[3] = 0;

            diagnostics->txDescriptorControl = txDescriptor[0].control;
            diagnostics->txDescriptorReqCount = txDescriptor[0].reqCount;
            diagnostics->txHeader0 = txHeader[0];
            diagnostics->txHeader1 = txHeader[1];
            diagnostics->txHeader2 = txHeader[2];
            diagnostics->txHeader3 = txHeader[3];
            diagnostics->txSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncTxBuffer,
                                                                                          kAsyncTxBufferSize));
            __sync_synchronize();
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvCommandPtrOffset, diagnostics->reqRxCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvCommandPtrOffset, diagnostics->rxCommandPtr);
            pciDevice->MemoryRead32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, &diagnostics->reqRxContextBefore);
            pciDevice->MemoryRead32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, &diagnostics->rxContextBefore);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrCommandPtrOffset, diagnostics->txCommandPtr);
            pciDevice->MemoryRead32(memoryIndex, kOhciAsReqTrContextControlSetOffset, &diagnostics->txContextBefore);
            pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, &diagnostics->intEventBefore);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextWake);

            bool responseReceived = false;
            bool waitingForResponse = false;
            for (uint32_t i = 0; i < kAsyncReadWaitLoopsPerAttempt; ++i) {
                diagnostics->waitLoops += 1;
                diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
                diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
                diagnostics->rxDescriptor0Status = rxDescriptors[0].transferStatus;
                diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
                diagnostics->rxDescriptor1Status = rxDescriptors[1].transferStatus;
                diagnostics->reqRxDescriptor0ResCount = reqRxDescriptors[0].resCount;
                diagnostics->reqRxDescriptor0Status = reqRxDescriptors[0].transferStatus;
                diagnostics->reqRxDescriptor1ResCount = reqRxDescriptors[1].resCount;
                diagnostics->reqRxDescriptor1Status = reqRxDescriptors[1].transferStatus;
                responseReceived = diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ||
                                   diagnostics->rxDescriptor1ResCount != kAsyncRxDataSize;
                bool transmitFinished = diagnostics->txDescriptorStatus != 0;
                waitingForResponse =
                    (diagnostics->txDescriptorStatus & kDescriptorEventMask) == kDescriptorEventAckPending;
                if (responseReceived || (transmitFinished && !waitingForResponse)) {
                    break;
                }
                IOSleep(kAsyncReadWaitMilliseconds);
            }

            diagnostics->rxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncRxBuffer,
                                                                                    kAsyncRxBufferSize));
            diagnostics->reqRxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncReqRxBuffer,
                                                                                       kAsyncRxBufferSize));
            diagnostics->txSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncTxBuffer,
                                                                                    kAsyncTxBufferSize));
            diagnostics->txDescriptorResCount = txDescriptor[0].resCount;
            diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
            diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
            diagnostics->rxDescriptor0Status = rxDescriptors[0].transferStatus;
            diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
            diagnostics->rxDescriptor1Status = rxDescriptors[1].transferStatus;
            diagnostics->reqRxDescriptor0ResCount = reqRxDescriptors[0].resCount;
            diagnostics->reqRxDescriptor0Status = reqRxDescriptors[0].transferStatus;
            diagnostics->reqRxDescriptor1ResCount = reqRxDescriptors[1].resCount;
            diagnostics->reqRxDescriptor1Status = reqRxDescriptors[1].transferStatus;

            volatile uint32_t * response = rxData0;
            diagnostics->rxBytes0 = kAsyncRxDataSize - diagnostics->rxDescriptor0ResCount;
            diagnostics->rxBytes1 = kAsyncRxDataSize - diagnostics->rxDescriptor1ResCount;
            if (diagnostics->rxBytes0 == 0 && diagnostics->rxBytes1 != 0) {
                response = rxData1;
            }
            diagnostics->rxHeader0 = response[0];
            diagnostics->rxHeader1 = response[1];
            diagnostics->rxHeader2 = response[2];
            diagnostics->rxHeader3 = response[3];
            diagnostics->responseTCode = ResponseTCode(diagnostics->rxHeader0);
            diagnostics->responseTLabel = ResponseTLabel(diagnostics->rxHeader0);
            diagnostics->responseSource = ResponseSource(diagnostics->rxHeader1);
            diagnostics->responseRCode = ResponseRCode(diagnostics->rxHeader1);
            diagnostics->responseData = diagnostics->rxHeader3;

            diagnostics->configROMTxStatus[readIndex] = diagnostics->txDescriptorStatus;
            diagnostics->configROMRxBytes[readIndex] = diagnostics->rxBytes0 != 0 ?
                                                       diagnostics->rxBytes0 :
                                                       diagnostics->rxBytes1;
            diagnostics->configROMTCode[readIndex] = diagnostics->responseTCode;
            diagnostics->configROMTLabel[readIndex] = diagnostics->responseTLabel;
            diagnostics->configROMRCode[readIndex] = diagnostics->responseRCode;
            diagnostics->configROMData[readIndex] = diagnostics->responseData;

            if (waitingForResponse) {
                diagnostics->ackPendingAttempts += 1;
            }
            if (responseReceived &&
                diagnostics->responseTCode == kTCodeReadQuadletResponse &&
                diagnostics->responseTLabel == tlabel &&
                diagnostics->responseRCode == 0) {
                diagnostics->responseAttempt = attempt;
                diagnostics->configROMReadSuccessCount += 1;
                readSucceeded = true;
                break;
            }
            // Hot-plug can produce an initial missing_ack; retry before declaring the bus unreadable.
            if (attempt + 1 < kAsyncReadAttemptCount) {
                IOSleep(kAsyncReadRetrySettleMilliseconds);
            }
        }
        if (!readSucceeded) {
            break;
        }
    }

    constexpr uint64_t digiRegisterOffsets[kDigi00xRegisterProbeCount] = {
        kDigi00xOffsetStreamingState,
        kDigi00xOffsetStreamingSet,
        kDigi00xOffsetMessageAddress,
        kDigi00xOffsetIsocChannels,
        kDigi00xOffsetLocalRate,
        kDigi00xOffsetExternalRate,
        kDigi00xOffsetClockSource,
        kDigi00xOffsetOpticalInterfaceMode,
        kDigi00xOffsetMonitorEnable,
        kDigi00xOffsetDetectExternal,
    };
    diagnostics->digiRegisterReadCount = kDigi00xRegisterProbeCount;
    for (size_t registerIndex = 0; registerIndex < kDigi00xRegisterProbeCount; ++registerIndex) {
        bool readSucceeded = false;
        diagnostics->digiRegisterLastIndex = static_cast<uint32_t>(registerIndex);
        diagnostics->digiRegisterOffsetLo[registerIndex] =
            static_cast<uint32_t>(digiRegisterOffsets[registerIndex]);
        for (uint32_t attempt = 0; attempt < kAsyncReadAttemptCount; ++attempt) {
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqTrContextControlSetOffset,
                        kOhciAsReqTrContextControlClearOffset,
                        &diagnostics->txContextStopLoops,
                        &diagnostics->txContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqRcvContextControlSetOffset,
                        kOhciAsReqRcvContextControlClearOffset,
                        &diagnostics->reqRxContextStopLoops,
                        &diagnostics->reqRxContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsRspRcvContextControlSetOffset,
                        kOhciAsRspRcvContextControlClearOffset,
                        &diagnostics->rxContextStopLoops,
                        &diagnostics->rxContextAfterClear);
            ConfigureAsyncReceiveBuffer(&gAsyncRxBuffer,
                                        &diagnostics->rxCommandPtr,
                                        &rxDescriptors,
                                        &rxData0,
                                        &rxData1);
            ConfigureAsyncReceiveBuffer(&gAsyncReqRxBuffer,
                                        &diagnostics->reqRxCommandPtr,
                                        &reqRxDescriptors,
                                        &reqRxData0,
                                        &reqRxData1);

            diagnostics->lastAttempt = attempt;
            diagnostics->completedAttempts += 1;
            diagnostics->rxDescriptor0Control = rxDescriptors[0].control;
            diagnostics->rxDescriptor0DataAddress = rxDescriptors[0].dataAddress;
            diagnostics->rxDescriptor0BranchAddress = rxDescriptors[0].branchAddress;
            diagnostics->rxDescriptor1Control = rxDescriptors[1].control;
            diagnostics->rxDescriptor1DataAddress = rxDescriptors[1].dataAddress;
            diagnostics->rxDescriptor1BranchAddress = rxDescriptors[1].branchAddress;
            diagnostics->rxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncRxBuffer,
                                                                                          kAsyncRxBufferSize));
            diagnostics->reqRxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncReqRxBuffer,
                                                                                             kAsyncRxBufferSize));

            txDescriptor[0].reqCount = 12;
            txDescriptor[0].control = kDescriptorKeyImmediate |
                                      kDescriptorOutputLast |
                                      kDescriptorIrqAlways |
                                      kDescriptorBranchAlways;
            txDescriptor[0].dataAddress = 0;
            txDescriptor[0].branchAddress = 0;
            txDescriptor[0].resCount = 0;
            txDescriptor[0].transferStatus = 0;
            uint64_t readOffset = kDigi00xRegisterBase + digiRegisterOffsets[registerIndex];
            uint32_t tlabel =
                (kAsyncReadTLabel +
                 static_cast<uint32_t>((kConfigROMProbeReadCount + registerIndex) * kAsyncReadAttemptCount) +
                 attempt) & 0x3fu;
            txHeader[0] = BuildATQuadlet0(kSCode400, tlabel, kRetryX, kTCodeReadQuadletRequest);
            txHeader[1] = BuildATQuadlet1(diagnostics->destinationID, readOffset);
            txHeader[2] = static_cast<uint32_t>(readOffset & 0xffffffffu);
            txHeader[3] = 0;

            diagnostics->txDescriptorControl = txDescriptor[0].control;
            diagnostics->txDescriptorReqCount = txDescriptor[0].reqCount;
            diagnostics->txHeader0 = txHeader[0];
            diagnostics->txHeader1 = txHeader[1];
            diagnostics->txHeader2 = txHeader[2];
            diagnostics->txHeader3 = txHeader[3];
            diagnostics->txSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncTxBuffer,
                                                                                          kAsyncTxBufferSize));
            __sync_synchronize();
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvCommandPtrOffset, diagnostics->reqRxCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvCommandPtrOffset, diagnostics->rxCommandPtr);
            pciDevice->MemoryRead32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, &diagnostics->reqRxContextBefore);
            pciDevice->MemoryRead32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, &diagnostics->rxContextBefore);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrCommandPtrOffset, diagnostics->txCommandPtr);
            pciDevice->MemoryRead32(memoryIndex, kOhciAsReqTrContextControlSetOffset, &diagnostics->txContextBefore);
            pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, &diagnostics->intEventBefore);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextWake);

            bool responseReceived = false;
            bool waitingForResponse = false;
            for (uint32_t i = 0; i < kAsyncReadWaitLoopsPerAttempt; ++i) {
                diagnostics->waitLoops += 1;
                diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
                diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
                diagnostics->rxDescriptor0Status = rxDescriptors[0].transferStatus;
                diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
                diagnostics->rxDescriptor1Status = rxDescriptors[1].transferStatus;
                diagnostics->reqRxDescriptor0ResCount = reqRxDescriptors[0].resCount;
                diagnostics->reqRxDescriptor0Status = reqRxDescriptors[0].transferStatus;
                diagnostics->reqRxDescriptor1ResCount = reqRxDescriptors[1].resCount;
                diagnostics->reqRxDescriptor1Status = reqRxDescriptors[1].transferStatus;
                responseReceived = diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ||
                                   diagnostics->rxDescriptor1ResCount != kAsyncRxDataSize;
                bool transmitFinished = diagnostics->txDescriptorStatus != 0;
                waitingForResponse =
                    (diagnostics->txDescriptorStatus & kDescriptorEventMask) == kDescriptorEventAckPending;
                if (responseReceived || (transmitFinished && !waitingForResponse)) {
                    break;
                }
                IOSleep(kAsyncReadWaitMilliseconds);
            }

            diagnostics->rxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncRxBuffer,
                                                                                    kAsyncRxBufferSize));
            diagnostics->reqRxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncReqRxBuffer,
                                                                                       kAsyncRxBufferSize));
            diagnostics->txSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncTxBuffer,
                                                                                    kAsyncTxBufferSize));
            diagnostics->txDescriptorResCount = txDescriptor[0].resCount;
            diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
            diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
            diagnostics->rxDescriptor0Status = rxDescriptors[0].transferStatus;
            diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
            diagnostics->rxDescriptor1Status = rxDescriptors[1].transferStatus;
            diagnostics->reqRxDescriptor0ResCount = reqRxDescriptors[0].resCount;
            diagnostics->reqRxDescriptor0Status = reqRxDescriptors[0].transferStatus;
            diagnostics->reqRxDescriptor1ResCount = reqRxDescriptors[1].resCount;
            diagnostics->reqRxDescriptor1Status = reqRxDescriptors[1].transferStatus;

            volatile uint32_t * response = rxData0;
            diagnostics->rxBytes0 = kAsyncRxDataSize - diagnostics->rxDescriptor0ResCount;
            diagnostics->rxBytes1 = kAsyncRxDataSize - diagnostics->rxDescriptor1ResCount;
            if (diagnostics->rxBytes0 == 0 && diagnostics->rxBytes1 != 0) {
                response = rxData1;
            }
            diagnostics->rxHeader0 = response[0];
            diagnostics->rxHeader1 = response[1];
            diagnostics->rxHeader2 = response[2];
            diagnostics->rxHeader3 = response[3];
            diagnostics->responseTCode = ResponseTCode(diagnostics->rxHeader0);
            diagnostics->responseTLabel = ResponseTLabel(diagnostics->rxHeader0);
            diagnostics->responseSource = ResponseSource(diagnostics->rxHeader1);
            diagnostics->responseRCode = ResponseRCode(diagnostics->rxHeader1);
            diagnostics->responseData = diagnostics->rxHeader3;

            diagnostics->digiRegisterTxStatus[registerIndex] = diagnostics->txDescriptorStatus;
            diagnostics->digiRegisterRxBytes[registerIndex] = diagnostics->rxBytes0 != 0 ?
                                                              diagnostics->rxBytes0 :
                                                              diagnostics->rxBytes1;
            diagnostics->digiRegisterTCode[registerIndex] = diagnostics->responseTCode;
            diagnostics->digiRegisterTLabel[registerIndex] = diagnostics->responseTLabel;
            diagnostics->digiRegisterRCode[registerIndex] = diagnostics->responseRCode;
            diagnostics->digiRegisterData[registerIndex] = diagnostics->responseData;

            if (waitingForResponse) {
                diagnostics->ackPendingAttempts += 1;
            }
            if (responseReceived &&
                diagnostics->responseTCode == kTCodeReadQuadletResponse &&
                diagnostics->responseTLabel == tlabel &&
                diagnostics->responseRCode == 0) {
                diagnostics->responseAttempt = attempt;
                diagnostics->digiRegisterReadSuccessCount += 1;
                readSucceeded = true;
                break;
            }
            if (attempt + 1 < kAsyncReadAttemptCount) {
                IOSleep(kAsyncReadRetrySettleMilliseconds);
            }
        }
        (void)readSucceeded;
    }

    constexpr uint64_t digiWritePlanOffsets[kDigi00xWritePlanCount] = {
        kDigi00xOffsetStreamingSet,
        kDigi00xOffsetIsocChannels,
        kDigi00xOffsetIsocChannels,
        kDigi00xOffsetStreamingSet,
        kDigi00xOffsetStreamingSet,
    };
    uint32_t observedIsocChannels = 0;
    if (diagnostics->digiRegisterRCode[3] == 0 &&
        diagnostics->digiRegisterTCode[3] == kTCodeReadQuadletResponse) {
        observedIsocChannels = ByteSwap32(diagnostics->digiRegisterData[3]);
    }
    uint32_t digiWritePlanBusValues[kDigi00xWritePlanCount] = {
        0x00000003u,
        0x00000000u,
        observedIsocChannels,
        0x00000002u,
        0x00000001u,
    };
    diagnostics->digiWritePlanEnabled = kDigi00xWritePlanEnabled;
    diagnostics->digiWritePlanExecuted = 0;
    diagnostics->digiWritePlanCount = kDigi00xWritePlanCount;
    for (size_t planIndex = 0; planIndex < kDigi00xWritePlanCount; ++planIndex) {
        uint64_t writeOffset = kDigi00xRegisterBase + digiWritePlanOffsets[planIndex];
        uint32_t tlabel =
            (kAsyncReadTLabel +
             static_cast<uint32_t>((kConfigROMProbeReadCount +
                                    kDigi00xRegisterProbeCount +
                                    planIndex) * kAsyncReadAttemptCount)) & 0x3fu;
        diagnostics->digiWritePlanOffsetLo[planIndex] =
            static_cast<uint32_t>(digiWritePlanOffsets[planIndex]);
        diagnostics->digiWritePlanBusValue[planIndex] = digiWritePlanBusValues[planIndex];
        diagnostics->digiWritePlanTxData[planIndex] = ByteSwap32(digiWritePlanBusValues[planIndex]);
        diagnostics->digiWritePlanReqCount[planIndex] = 16;
        diagnostics->digiWritePlanHeader0[planIndex] =
            BuildATQuadlet0(kSCode400, tlabel, kRetryX, kTCodeWriteQuadletRequest);
        diagnostics->digiWritePlanHeader1[planIndex] =
            BuildATQuadlet1(diagnostics->destinationID, writeOffset);
        diagnostics->digiWritePlanHeader2[planIndex] =
            static_cast<uint32_t>(writeOffset & 0xffffffffu);
        diagnostics->digiWritePlanHeader3[planIndex] =
            diagnostics->digiWritePlanTxData[planIndex];
    }

    if (kDigi00xNoopWriteEnabled != 0 &&
        diagnostics->digiRegisterRCode[3] == 0 &&
        diagnostics->digiRegisterTCode[3] == kTCodeReadQuadletResponse) {
        diagnostics->digiNoopWriteAttempted = 1;
        diagnostics->digiNoopWriteOffsetLo = static_cast<uint32_t>(kDigi00xOffsetIsocChannels);
        diagnostics->digiNoopWriteBusValue = observedIsocChannels;
        diagnostics->digiNoopWriteTxData = ByteSwap32(observedIsocChannels);
        diagnostics->digiNoopWriteReqCount = 16;
        uint64_t writeOffset = kDigi00xRegisterBase + kDigi00xOffsetIsocChannels;

        for (uint32_t attempt = 0; attempt < kAsyncReadAttemptCount; ++attempt) {
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqTrContextControlSetOffset,
                        kOhciAsReqTrContextControlClearOffset,
                        &diagnostics->txContextStopLoops,
                        &diagnostics->txContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqRcvContextControlSetOffset,
                        kOhciAsReqRcvContextControlClearOffset,
                        &diagnostics->reqRxContextStopLoops,
                        &diagnostics->reqRxContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsRspRcvContextControlSetOffset,
                        kOhciAsRspRcvContextControlClearOffset,
                        &diagnostics->rxContextStopLoops,
                        &diagnostics->rxContextAfterClear);
            ConfigureAsyncReceiveBuffer(&gAsyncRxBuffer,
                                        &diagnostics->rxCommandPtr,
                                        &rxDescriptors,
                                        &rxData0,
                                        &rxData1);
            ConfigureAsyncReceiveBuffer(&gAsyncReqRxBuffer,
                                        &diagnostics->reqRxCommandPtr,
                                        &reqRxDescriptors,
                                        &reqRxData0,
                                        &reqRxData1);

            diagnostics->digiNoopWriteCompletedAttempts += 1;
            diagnostics->rxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncRxBuffer,
                                                                                          kAsyncRxBufferSize));
            diagnostics->reqRxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncReqRxBuffer,
                                                                                             kAsyncRxBufferSize));

            txDescriptor[0].reqCount = static_cast<uint16_t>(diagnostics->digiNoopWriteReqCount);
            txDescriptor[0].control = kDescriptorKeyImmediate |
                                      kDescriptorOutputLast |
                                      kDescriptorIrqAlways |
                                      kDescriptorBranchAlways;
            txDescriptor[0].dataAddress = 0;
            txDescriptor[0].branchAddress = 0;
            txDescriptor[0].resCount = 0;
            txDescriptor[0].transferStatus = 0;
            uint32_t tlabel =
                (kAsyncReadTLabel +
                 static_cast<uint32_t>((kConfigROMProbeReadCount +
                                        kDigi00xRegisterProbeCount +
                                        kDigi00xWritePlanCount) * kAsyncReadAttemptCount) +
                 attempt) & 0x3fu;
            txHeader[0] = BuildATQuadlet0(kSCode400, tlabel, kRetryX, kTCodeWriteQuadletRequest);
            txHeader[1] = BuildATQuadlet1(diagnostics->destinationID, writeOffset);
            txHeader[2] = static_cast<uint32_t>(writeOffset & 0xffffffffu);
            txHeader[3] = diagnostics->digiNoopWriteTxData;

            diagnostics->digiNoopWriteHeader0 = txHeader[0];
            diagnostics->digiNoopWriteHeader1 = txHeader[1];
            diagnostics->digiNoopWriteHeader2 = txHeader[2];
            diagnostics->digiNoopWriteHeader3 = txHeader[3];
            diagnostics->txSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncTxBuffer,
                                                                                          kAsyncTxBufferSize));
            __sync_synchronize();
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvCommandPtrOffset, diagnostics->reqRxCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvCommandPtrOffset, diagnostics->rxCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrCommandPtrOffset, diagnostics->txCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextWake);
            diagnostics->digiNoopWriteExecuted = 1;

            bool responseReceived = false;
            bool waitingForResponse = false;
            for (uint32_t i = 0; i < kAsyncReadWaitLoopsPerAttempt; ++i) {
                diagnostics->digiNoopWriteWaitLoops += 1;
                diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
                diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
                diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
                responseReceived = diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ||
                                   diagnostics->rxDescriptor1ResCount != kAsyncRxDataSize;
                bool transmitFinished = diagnostics->txDescriptorStatus != 0;
                waitingForResponse =
                    (diagnostics->txDescriptorStatus & kDescriptorEventMask) == kDescriptorEventAckPending;
                if (responseReceived || (transmitFinished && !waitingForResponse)) {
                    break;
                }
                IOSleep(kAsyncReadWaitMilliseconds);
            }

            diagnostics->rxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncRxBuffer,
                                                                                    kAsyncRxBufferSize));
            diagnostics->reqRxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncReqRxBuffer,
                                                                                       kAsyncRxBufferSize));
            diagnostics->txSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncTxBuffer,
                                                                                    kAsyncTxBufferSize));
            diagnostics->digiNoopWriteTxStatus = txDescriptor[0].transferStatus;
            diagnostics->digiNoopWriteRxBytes =
                diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ?
                (kAsyncRxDataSize - diagnostics->rxDescriptor0ResCount) :
                (kAsyncRxDataSize - diagnostics->rxDescriptor1ResCount);
            volatile uint32_t * response =
                diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ? rxData0 : rxData1;
            diagnostics->rxHeader0 = response[0];
            diagnostics->rxHeader1 = response[1];
            diagnostics->rxHeader2 = response[2];
            diagnostics->rxHeader3 = response[3];
            diagnostics->digiNoopWriteResponseTCode = ResponseTCode(diagnostics->rxHeader0);
            diagnostics->digiNoopWriteResponseTLabel = ResponseTLabel(diagnostics->rxHeader0);
            diagnostics->digiNoopWriteResponseSource = ResponseSource(diagnostics->rxHeader1);
            diagnostics->digiNoopWriteResponseRCode = ResponseRCode(diagnostics->rxHeader1);

            if (waitingForResponse) {
                diagnostics->ackPendingAttempts += 1;
            }
            if (responseReceived &&
                diagnostics->digiNoopWriteResponseTCode == kTCodeWriteResponse &&
                diagnostics->digiNoopWriteResponseTLabel == tlabel &&
                diagnostics->digiNoopWriteResponseRCode == 0) {
                diagnostics->digiNoopWriteSuccess = 1;
                break;
            }
            if (attempt + 1 < kAsyncReadAttemptCount) {
                IOSleep(kAsyncReadRetrySettleMilliseconds);
            }
        }
    }

    if (kDigi00xStateWriteEnabled != 0) {
        diagnostics->digiStateWriteAttempted = 1;
        diagnostics->digiStateWritePrereqNoopSuccess = diagnostics->digiNoopWriteSuccess;
    }

    if (kDigi00xStateWriteEnabled != 0 &&
        diagnostics->digiNoopWriteSuccess != 0) {
        diagnostics->digiStateWriteOffsetLo = static_cast<uint32_t>(kDigi00xOffsetStreamingSet);
        diagnostics->digiStateWriteBusValue = 0x00000003u;
        diagnostics->digiStateWriteTxData = ByteSwap32(diagnostics->digiStateWriteBusValue);
        diagnostics->digiStateWriteReqCount = 16;
        uint64_t writeOffset = kDigi00xRegisterBase + kDigi00xOffsetStreamingSet;

        for (uint32_t attempt = 0; attempt < kAsyncReadAttemptCount; ++attempt) {
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqTrContextControlSetOffset,
                        kOhciAsReqTrContextControlClearOffset,
                        &diagnostics->txContextStopLoops,
                        &diagnostics->txContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsReqRcvContextControlSetOffset,
                        kOhciAsReqRcvContextControlClearOffset,
                        &diagnostics->reqRxContextStopLoops,
                        &diagnostics->reqRxContextAfterClear);
            StopContext(pciDevice,
                        memoryIndex,
                        kOhciAsRspRcvContextControlSetOffset,
                        kOhciAsRspRcvContextControlClearOffset,
                        &diagnostics->rxContextStopLoops,
                        &diagnostics->rxContextAfterClear);
            ConfigureAsyncReceiveBuffer(&gAsyncRxBuffer,
                                        &diagnostics->rxCommandPtr,
                                        &rxDescriptors,
                                        &rxData0,
                                        &rxData1);
            ConfigureAsyncReceiveBuffer(&gAsyncReqRxBuffer,
                                        &diagnostics->reqRxCommandPtr,
                                        &reqRxDescriptors,
                                        &reqRxData0,
                                        &reqRxData1);

            diagnostics->digiStateWriteCompletedAttempts += 1;
            diagnostics->rxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncRxBuffer,
                                                                                          kAsyncRxBufferSize));
            diagnostics->reqRxSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncReqRxBuffer,
                                                                                             kAsyncRxBufferSize));

            txDescriptor[0].reqCount = static_cast<uint16_t>(diagnostics->digiStateWriteReqCount);
            txDescriptor[0].control = kDescriptorKeyImmediate |
                                      kDescriptorOutputLast |
                                      kDescriptorIrqAlways |
                                      kDescriptorBranchAlways;
            txDescriptor[0].dataAddress = 0;
            txDescriptor[0].branchAddress = 0;
            txDescriptor[0].resCount = 0;
            txDescriptor[0].transferStatus = 0;
            uint32_t tlabel =
                (kAsyncReadTLabel +
                 static_cast<uint32_t>((kConfigROMProbeReadCount +
                                        kDigi00xRegisterProbeCount +
                                        kDigi00xWritePlanCount +
                                        1u) * kAsyncReadAttemptCount) +
                 attempt) & 0x3fu;
            txHeader[0] = BuildATQuadlet0(kSCode400, tlabel, kRetryX, kTCodeWriteQuadletRequest);
            txHeader[1] = BuildATQuadlet1(diagnostics->destinationID, writeOffset);
            txHeader[2] = static_cast<uint32_t>(writeOffset & 0xffffffffu);
            txHeader[3] = diagnostics->digiStateWriteTxData;

            diagnostics->digiStateWriteHeader0 = txHeader[0];
            diagnostics->digiStateWriteHeader1 = txHeader[1];
            diagnostics->digiStateWriteHeader2 = txHeader[2];
            diagnostics->digiStateWriteHeader3 = txHeader[3];
            diagnostics->txSyncForDeviceRet = ReturnCodeToProperty(SyncDMABufferForDevice(&gAsyncTxBuffer,
                                                                                          kAsyncTxBufferSize));
            __sync_synchronize();
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvCommandPtrOffset, diagnostics->reqRxCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvCommandPtrOffset, diagnostics->rxCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrCommandPtrOffset, diagnostics->txCommandPtr);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextRun);
            pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqTrContextControlSetOffset, kContextWake);
            diagnostics->digiStateWriteExecuted = 1;

            bool responseReceived = false;
            bool waitingForResponse = false;
            for (uint32_t i = 0; i < kAsyncReadWaitLoopsPerAttempt; ++i) {
                diagnostics->digiStateWriteWaitLoops += 1;
                diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
                diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
                diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
                responseReceived = diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ||
                                   diagnostics->rxDescriptor1ResCount != kAsyncRxDataSize;
                bool transmitFinished = diagnostics->txDescriptorStatus != 0;
                waitingForResponse =
                    (diagnostics->txDescriptorStatus & kDescriptorEventMask) == kDescriptorEventAckPending;
                if (responseReceived || (transmitFinished && !waitingForResponse)) {
                    break;
                }
                IOSleep(kAsyncReadWaitMilliseconds);
            }

            diagnostics->rxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncRxBuffer,
                                                                                    kAsyncRxBufferSize));
            diagnostics->reqRxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncReqRxBuffer,
                                                                                       kAsyncRxBufferSize));
            diagnostics->txSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncTxBuffer,
                                                                                    kAsyncTxBufferSize));
            diagnostics->digiStateWriteTxStatus = txDescriptor[0].transferStatus;
            diagnostics->digiStateWriteRxBytes =
                diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ?
                (kAsyncRxDataSize - diagnostics->rxDescriptor0ResCount) :
                (kAsyncRxDataSize - diagnostics->rxDescriptor1ResCount);
            volatile uint32_t * response =
                diagnostics->rxDescriptor0ResCount != kAsyncRxDataSize ? rxData0 : rxData1;
            diagnostics->rxHeader0 = response[0];
            diagnostics->rxHeader1 = response[1];
            diagnostics->rxHeader2 = response[2];
            diagnostics->rxHeader3 = response[3];
            diagnostics->digiStateWriteResponseTCode = ResponseTCode(diagnostics->rxHeader0);
            diagnostics->digiStateWriteResponseTLabel = ResponseTLabel(diagnostics->rxHeader0);
            diagnostics->digiStateWriteResponseSource = ResponseSource(diagnostics->rxHeader1);
            diagnostics->digiStateWriteResponseRCode = ResponseRCode(diagnostics->rxHeader1);

            if (waitingForResponse) {
                diagnostics->ackPendingAttempts += 1;
            }
            if (responseReceived &&
                diagnostics->digiStateWriteResponseTCode == kTCodeWriteResponse &&
                diagnostics->digiStateWriteResponseTLabel == tlabel &&
                diagnostics->digiStateWriteResponseRCode == 0) {
                diagnostics->digiStateWriteSuccess = 1;
                break;
            }
            if (attempt + 1 < kAsyncReadAttemptCount) {
                IOSleep(kAsyncReadRetrySettleMilliseconds);
            }
        }
    }

    if (kDigi00xStateSequenceEnabled != 0) {
        diagnostics->digiStateSequenceAttempted = 1;
        diagnostics->digiStateSequencePrereqStateWriteSuccess = diagnostics->digiStateWriteSuccess;
        diagnostics->digiStateSequenceStepCount = kDigi00xStateSequenceStepCount;
    }

    if (kDigi00xStateSequenceEnabled != 0 &&
        diagnostics->digiStateWriteSuccess != 0) {
        constexpr uint32_t kStateSequenceOpRead = 0;
        constexpr uint32_t kStateSequenceOpWrite = 1;
        constexpr uint32_t stateSequenceOps[kDigi00xStateSequenceStepCount] = {
            kStateSequenceOpRead,
            kStateSequenceOpWrite,
            kStateSequenceOpRead,
            kStateSequenceOpWrite,
            kStateSequenceOpRead,
            kStateSequenceOpWrite,
            kStateSequenceOpWrite,
            kStateSequenceOpRead,
        };
        constexpr uint64_t stateSequenceOffsets[kDigi00xStateSequenceStepCount] = {
            kDigi00xOffsetIsocChannels,
            kDigi00xOffsetIsocChannels,
            kDigi00xOffsetIsocChannels,
            kDigi00xOffsetStreamingSet,
            kDigi00xOffsetStreamingState,
            kDigi00xOffsetStreamingSet,
            kDigi00xOffsetStreamingSet,
            kDigi00xOffsetStreamingState,
        };
        const uint32_t stateSequenceBusValues[kDigi00xStateSequenceStepCount] = {
            0x00000000u,
            observedIsocChannels != 0 ? observedIsocChannels : 0x00020000u,
            0x00000000u,
            0x00000002u,
            0x00000000u,
            0x00000001u,
            0x00000003u,
            0x00000000u,
        };
        bool allStepsSucceeded = true;

        for (size_t stepIndex = 0; stepIndex < kDigi00xStateSequenceStepCount; ++stepIndex) {
            bool isWrite = stateSequenceOps[stepIndex] == kStateSequenceOpWrite;
            uint64_t transactionOffset = kDigi00xRegisterBase + stateSequenceOffsets[stepIndex];
            uint32_t requestTCode = isWrite ? kTCodeWriteQuadletRequest : kTCodeReadQuadletRequest;
            uint32_t expectedResponseTCode = isWrite ? kTCodeWriteResponse : kTCodeReadQuadletResponse;
            uint32_t reqCount = isWrite ? 16u : 12u;
            uint32_t txData = isWrite ? ByteSwap32(stateSequenceBusValues[stepIndex]) : 0;
            uint32_t tlabelBase =
                (kAsyncReadTLabel +
                 static_cast<uint32_t>((kConfigROMProbeReadCount +
                                        kDigi00xRegisterProbeCount +
                                        kDigi00xWritePlanCount +
                                        2u +
                                        stepIndex) * kAsyncReadAttemptCount)) & 0x3fu;

            diagnostics->digiStateSequenceOp[stepIndex] = stateSequenceOps[stepIndex];
            diagnostics->digiStateSequenceOffsetLo[stepIndex] =
                static_cast<uint32_t>(stateSequenceOffsets[stepIndex]);
            diagnostics->digiStateSequenceBusValue[stepIndex] = stateSequenceBusValues[stepIndex];
            diagnostics->digiStateSequenceTxData[stepIndex] = txData;
            diagnostics->digiStateSequenceReqCount[stepIndex] = reqCount;

            bool stepSucceeded =
                RunAsyncQuadletTransaction(pciDevice,
                                           memoryIndex,
                                           diagnostics,
                                           txDescriptor,
                                           txHeader,
                                           transactionOffset,
                                           requestTCode,
                                           expectedResponseTCode,
                                           reqCount,
                                           txData,
                                           tlabelBase,
                                           &diagnostics->digiStateSequenceCompletedAttempts[stepIndex],
                                           &diagnostics->digiStateSequenceWaitLoops[stepIndex],
                                           &diagnostics->digiStateSequenceHeader0[stepIndex],
                                           &diagnostics->digiStateSequenceHeader1[stepIndex],
                                           &diagnostics->digiStateSequenceHeader2[stepIndex],
                                           &diagnostics->digiStateSequenceHeader3[stepIndex],
                                           &diagnostics->digiStateSequenceTxStatus[stepIndex],
                                           &diagnostics->digiStateSequenceRxBytes[stepIndex],
                                           &diagnostics->digiStateSequenceResponseTCode[stepIndex],
                                           &diagnostics->digiStateSequenceResponseTLabel[stepIndex],
                                           &diagnostics->digiStateSequenceResponseSource[stepIndex],
                                           &diagnostics->digiStateSequenceResponseRCode[stepIndex],
                                           &diagnostics->digiStateSequenceResponseData[stepIndex]);

            diagnostics->digiStateSequenceSuccessByStep[stepIndex] = stepSucceeded ? 1 : 0;
            diagnostics->digiStateSequenceCompletedSteps = static_cast<uint32_t>(stepIndex + 1);
            if (!isWrite && stepSucceeded) {
                diagnostics->digiStateSequenceBusValue[stepIndex] =
                    ByteSwap32(diagnostics->digiStateSequenceResponseData[stepIndex]);
            }
            if (!stepSucceeded) {
                allStepsSucceeded = false;
            }
            if (isWrite && stepIndex != 5) {
                IOSleep(100);
            }
        }

        diagnostics->digiStateSequenceSuccess = allStepsSucceeded ? 1 : 0;
    }

    RunDigiDuplexProbe(pciDevice,
                       memoryIndex,
                       diagnostics,
                       duplexDiagnostics,
                       txDescriptor,
                       txHeader);

    pciDevice->MemoryRead32(memoryIndex, kOhciAsReqTrContextControlSetOffset, &diagnostics->txContextControl);
    pciDevice->MemoryRead32(memoryIndex, kOhciAsReqRcvContextControlSetOffset, &diagnostics->reqRxContextControl);
    pciDevice->MemoryRead32(memoryIndex, kOhciAsRspRcvContextControlSetOffset, &diagnostics->rxContextControl);
    diagnostics->rxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncRxBuffer,
                                                                            kAsyncRxBufferSize));
    diagnostics->reqRxSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncReqRxBuffer,
                                                                               kAsyncRxBufferSize));
    diagnostics->txSyncForCPURet = ReturnCodeToProperty(SyncDMABufferForCPU(&gAsyncTxBuffer,
                                                                            kAsyncTxBufferSize));
    diagnostics->txDescriptorResCount = txDescriptor[0].resCount;
    diagnostics->txDescriptorStatus = txDescriptor[0].transferStatus;
    diagnostics->rxDescriptor0ResCount = rxDescriptors[0].resCount;
    diagnostics->rxDescriptor0Status = rxDescriptors[0].transferStatus;
    diagnostics->rxDescriptor1ResCount = rxDescriptors[1].resCount;
    diagnostics->rxDescriptor1Status = rxDescriptors[1].transferStatus;
    diagnostics->reqRxDescriptor0ResCount = reqRxDescriptors[0].resCount;
    diagnostics->reqRxDescriptor0Status = reqRxDescriptors[0].transferStatus;
    diagnostics->reqRxDescriptor1ResCount = reqRxDescriptors[1].resCount;
    diagnostics->reqRxDescriptor1Status = reqRxDescriptors[1].transferStatus;

    volatile uint32_t * response = rxData0;
    diagnostics->rxBytes0 = kAsyncRxDataSize - diagnostics->rxDescriptor0ResCount;
    diagnostics->rxBytes1 = kAsyncRxDataSize - diagnostics->rxDescriptor1ResCount;
    if (diagnostics->rxBytes0 == 0 && diagnostics->rxBytes1 != 0) {
        response = rxData1;
    }
    diagnostics->rxHeader0 = response[0];
    diagnostics->rxHeader1 = response[1];
    diagnostics->rxHeader2 = response[2];
    diagnostics->rxHeader3 = response[3];
    volatile uint32_t * requestReceive = reqRxData0;
    diagnostics->reqRxBytes0 = kAsyncRxDataSize - diagnostics->reqRxDescriptor0ResCount;
    diagnostics->reqRxBytes1 = kAsyncRxDataSize - diagnostics->reqRxDescriptor1ResCount;
    if (diagnostics->reqRxBytes0 == 0 && diagnostics->reqRxBytes1 != 0) {
        requestReceive = reqRxData1;
    }
    diagnostics->reqRxHeader0 = requestReceive[0];
    diagnostics->reqRxHeader1 = requestReceive[1];
    diagnostics->reqRxHeader2 = requestReceive[2];
    diagnostics->reqRxHeader3 = requestReceive[3];
    diagnostics->responseTCode = ResponseTCode(diagnostics->rxHeader0);
    diagnostics->responseTLabel = ResponseTLabel(diagnostics->rxHeader0);
    diagnostics->responseSource = ResponseSource(diagnostics->rxHeader1);
    diagnostics->responseRCode = ResponseRCode(diagnostics->rxHeader1);
    diagnostics->responseData = diagnostics->rxHeader3;

    StopContext(pciDevice,
                memoryIndex,
                kOhciAsReqTrContextControlSetOffset,
                kOhciAsReqTrContextControlClearOffset,
                &diagnostics->txContextFinalStopLoops,
                &diagnostics->txContextAfterClear);
    StopContext(pciDevice,
                memoryIndex,
                kOhciAsReqRcvContextControlSetOffset,
                kOhciAsReqRcvContextControlClearOffset,
                &diagnostics->reqRxContextFinalStopLoops,
                &diagnostics->reqRxContextAfterClear);
    StopContext(pciDevice,
                memoryIndex,
                kOhciAsRspRcvContextControlSetOffset,
                kOhciAsRspRcvContextControlClearOffset,
                &diagnostics->rxContextFinalStopLoops,
                &diagnostics->rxContextAfterClear);
    diagnostics->rxCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gAsyncRxBuffer));
    diagnostics->reqRxCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gAsyncReqRxBuffer));
    diagnostics->txCompleteRet = ReturnCodeToProperty(CompleteDMABufferMapping(&gAsyncTxBuffer));
    pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, &diagnostics->intEventAfter);
    pciDevice->MemoryRead32(memoryIndex, kOhciIntMaskSetOffset, &diagnostics->intMaskAfter);
}

kern_return_t
RefreshDigiCaptureForAudio()
{
    if (gPCIDevice == nullptr ||
        gPCIMemoryIndex == 0xff ||
        gDigiDestinationID == 0xffffffff) {
        gAudioRefreshCaptureRet = ReturnCodeToProperty(kIOReturnNotReady);
        return kIOReturnNotReady;
    }
    if (gAudioRefreshCaptureInProgress != 0) {
        gAudioRefreshCaptureRet = ReturnCodeToProperty(kIOReturnBusy);
        return kIOReturnBusy;
    }

    gAudioRefreshCaptureInProgress = 1;
    gAudioRefreshCaptureAttemptCount++;
    gAudioRefreshCaptureRet = ReturnCodeToProperty(kIOReturnNotReady);
    PublishAudioRuntimeDiagnostics();

    AsyncReadDiagnostics asyncDiagnostics = {};
    InitializeAsyncDiagnostics(&asyncDiagnostics);
    DigiDuplexDiagnostics duplexDiagnostics = {};
    InitializeDigiDuplexDiagnostics(&duplexDiagnostics);
    asyncDiagnostics.destinationID = gDigiDestinationID;
    asyncDiagnostics.ready = 1;

    CreateDMABuffer(gPCIDevice, kAsyncRxBufferSize, &gAsyncRxBuffer);
    CreateDMABuffer(gPCIDevice, kAsyncRxBufferSize, &gAsyncReqRxBuffer);
    CreateDMABuffer(gPCIDevice, kAsyncTxBufferSize, &gAsyncTxBuffer);
    if (gAsyncRxBuffer.result != kIOReturnSuccess ||
        gAsyncReqRxBuffer.result != kIOReturnSuccess ||
        gAsyncTxBuffer.result != kIOReturnSuccess ||
        gAsyncRxBuffer.cpuRange.address == 0 ||
        gAsyncReqRxBuffer.cpuRange.address == 0 ||
        gAsyncTxBuffer.cpuRange.address == 0 ||
        gAsyncRxBuffer.segmentCount != 1 ||
        gAsyncReqRxBuffer.segmentCount != 1 ||
        gAsyncTxBuffer.segmentCount != 1 ||
        gAsyncRxBuffer.dmaSegment.length < kAsyncRxBufferSize ||
        gAsyncReqRxBuffer.dmaSegment.length < kAsyncRxBufferSize ||
        gAsyncTxBuffer.dmaSegment.length < kAsyncTxBufferSize ||
        gAsyncRxBuffer.dmaSegment.address > 0xffffffffull ||
        gAsyncReqRxBuffer.dmaSegment.address > 0xffffffffull ||
        gAsyncTxBuffer.dmaSegment.address > 0xffffffffull) {
        gAudioRefreshCaptureRet = ReturnCodeToProperty(kIOReturnNoResources);
        gAudioRefreshCaptureInProgress = 0;
        PublishAudioRuntimeDiagnostics();
        return kIOReturnNoResources;
    }

    volatile OHCIAsyncDescriptor * txDescriptor =
        reinterpret_cast<volatile OHCIAsyncDescriptor *>(gAsyncTxBuffer.cpuRange.address);
    volatile uint32_t * txHeader =
        reinterpret_cast<volatile uint32_t *>(gAsyncTxBuffer.cpuRange.address + sizeof(OHCIAsyncDescriptor));
    uint32_t txDescriptorDMA = static_cast<uint32_t>(gAsyncTxBuffer.dmaSegment.address);
    asyncDiagnostics.txCommandPtr = txDescriptorDMA | 2u;

    RunDigiDuplexProbe(gPCIDevice,
                       gPCIMemoryIndex,
                       &asyncDiagnostics,
                       &duplexDiagnostics,
                       txDescriptor,
                       txHeader);

    CopyDigiCaptureForAudio(&duplexDiagnostics);
    CompleteDMABufferMapping(&gAsyncRxBuffer);
    CompleteDMABufferMapping(&gAsyncReqRxBuffer);
    CompleteDMABufferMapping(&gAsyncTxBuffer);

    gAudioRefreshCaptureFrameCount = duplexDiagnostics.irCapturePCMFrameCount;
    gAudioRefreshCapturePeakAbs = duplexDiagnostics.irCapturePCMPeakAbs;
    gAudioRefreshCaptureRxBytes = duplexDiagnostics.irRxBytes;
    kern_return_t ret = duplexDiagnostics.irCapturePCMFrameCount > 0 ?
        kIOReturnSuccess :
        kIOReturnNotReady;
    gAudioRefreshCaptureRet = ReturnCodeToProperty(ret);
    if (ret == kIOReturnSuccess) {
        gAudioRefreshCaptureSuccessCount++;
    }
    gAudioRefreshCaptureInProgress = 0;
    PublishAudioRuntimeDiagnostics();
    return ret;
}

void
StartAudioRefreshWorker()
{
    if (gAudioRefreshQueue == nullptr || gAudioRefreshWorkerRunning != 0) {
        return;
    }

    gAudioRefreshWorkerRunning = 1;
    gAudioRefreshWorkerDispatchCount++;
    gAudioRefreshQueue->DispatchAsync(^{
        while (gAudioRefreshWorkerRunning != 0) {
            gAudioRefreshWorkerIterationCount++;
            kern_return_t ret = gDigiLiveRunning != 0 ?
                HarvestDigiLiveIsoStream() :
                RefreshDigiCaptureForAudio();
            gAudioRefreshWorkerLastRet = ReturnCodeToProperty(ret);
            gAudioRefreshWorkerLastGeneration = gAudioCaptureGeneration;

            if (gDigiLiveRunning != 0) {
                uint32_t outputFillFrames = AudioOutputRingFillFrames();
                if (outputFillFrames != 0 || gAudioOutputRingPrebuffered != 0) {
                    gDigiLiveOutputWorkerPushAudioCount++;
                } else {
                    gDigiLiveOutputWorkerPushSkippedAudioCount++;
                }
                (void)PushAudioOutputToDigiLiveTransmit();
                if ((gAudioRefreshWorkerIterationCount % kDigiLiveWorkerPublishInterval) == 0) {
                    gAudioRefreshWorkerLivePublishCount++;
                    PublishAudioRuntimeDiagnostics();
                } else {
                    gAudioRefreshWorkerLivePublishSkipCount++;
                }
                uint32_t sleepMilliseconds = ret == kIOReturnSuccess ?
                    kDigiLiveHarvestSleepMilliseconds :
                    kDigiLiveIdleSleepMilliseconds;
                if (ret == kIOReturnSuccess &&
                    gDigiLiveLastHarvestPackets >= kDigiLiveHarvestMaxDescriptorsPerPass) {
                    sleepMilliseconds = 0;
                    gAudioRefreshWorkerBacklogNoSleepCount++;
                }
                if (ret == kIOReturnSuccess &&
                    kDigiLiveWorkerLowWaterFrames != 0 &&
                    gAudioRingCurrentFillFrames < kDigiLiveWorkerLowWaterFrames) {
                    sleepMilliseconds = 0;
                    gAudioRefreshWorkerLowWaterNoSleepCount++;
                }
                if (sleepMilliseconds != 0) {
                    IOSleep(sleepMilliseconds);
                }
            } else {
                PublishAudioRuntimeDiagnostics();
                for (uint32_t i = 0; i < 10 && gAudioRefreshWorkerRunning != 0; ++i) {
                    IOSleep(25);
                }
            }
        }
        gAudioRefreshWorkerExitCount++;
        PublishAudioRuntimeDiagnostics();
    });
}

void
StopAudioRefreshWorker(bool waitForExit, bool publishDiagnostics = true)
{
    uint32_t expectedExitCount = gAudioRefreshWorkerDispatchCount;
    bool workerMayBeActive =
        gAudioRefreshWorkerRunning != 0 ||
        gAudioRefreshCaptureInProgress != 0 ||
        gAudioRefreshWorkerExitCount < expectedExitCount;
    gAudioRefreshWorkerRunning = 0;
    gAudioRefreshWorkerStopWaitLoops = 0;
    if (waitForExit && workerMayBeActive) {
        while ((gAudioRefreshWorkerExitCount < expectedExitCount ||
                gAudioRefreshCaptureInProgress != 0) &&
               gAudioRefreshWorkerStopWaitLoops < kAudioRefreshStopWaitLoopLimit) {
            gAudioRefreshWorkerStopWaitLoops++;
            IOSleep(10);
        }
    }
    if (publishDiagnostics && workerMayBeActive) {
        PublishAudioRuntimeDiagnostics();
    }
}

bool
DigiLiveStreamMayNeedStop()
{
    return gDigiLiveRunning != 0 ||
           gDigiLiveReady != 0 ||
           gDigiLiveStarting != 0 ||
           gDigiLiveStopping != 0 ||
           gDigiLiveBuffer.cpuRange.address != 0 ||
           gDigiLiveBuffer.command != nullptr;
}

void
RequestAudioRuntimeRestart(uint32_t reason)
{
    if (kAudioRuntimeCallbackRestartEnabled == 0) {
        return;
    }
    if (gDigiLiveRunning != 0 &&
        gDigiLiveReady != 0 &&
        gAudioRefreshWorkerRunning != 0) {
        gAudioRuntimeRestartSkippedCount++;
        return;
    }
    if (gAudioRefreshQueue == nullptr || gDigiLiveStarting != 0 || gDigiLiveStopping != 0) {
        gAudioRuntimeRestartSkippedCount++;
        return;
    }
    if (__sync_lock_test_and_set(&gAudioRuntimeRestartInProgress, 1) != 0) {
        gAudioRuntimeRestartBusyCount++;
        return;
    }

    gAudioRuntimeRestartRequestCount++;
    gAudioRuntimeRestartLastReason = reason;
    gAudioRuntimeRestartLastDigiRunning = gDigiLiveRunning;
    gAudioRuntimeRestartLastDigiReady = gDigiLiveReady;
    gAudioRuntimeRestartLastWorkerRunning = gAudioRefreshWorkerRunning;
    gAudioRuntimeRestartLastRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRuntimeRestartLastLiveRet = ReturnCodeToProperty(kIOReturnNotReady);

    gAudioRefreshQueue->DispatchAsync(^{
        gAudioRuntimeRestartDispatchCount++;
        kern_return_t ret = kIOReturnSuccess;
        kern_return_t liveRet = kIOReturnSuccess;

        if (gDigiLiveRunning == 0 || gDigiLiveReady == 0) {
            ResetAudioOutputRingBuffer();
            ResetDigiLiveOutputState();
            ClearAudioOutputBuffer();
            liveRet = StartDigiLiveStreamForAudio();
            gAudioRuntimeRestartLastLiveRet = ReturnCodeToProperty(liveRet);
            if (liveRet == kIOReturnSuccess) {
                PrebufferDigiLiveAudio();
            } else {
                ret = liveRet;
            }
        } else {
            gAudioRuntimeRestartLastLiveRet = ReturnCodeToProperty(kIOReturnSuccess);
        }

        if (ret == kIOReturnSuccess) {
            StartAudioRefreshWorker();
            if (gAudioRefreshWorkerRunning == 0) {
                ret = kIOReturnNotReady;
            }
        }

        gAudioRuntimeRestartLastRet = ReturnCodeToProperty(ret);
        if (ret == kIOReturnSuccess) {
            gAudioRuntimeRestartSuccessCount++;
        }
        __sync_lock_release(&gAudioRuntimeRestartInProgress);
        PublishAudioRuntimeDiagnostics();
    });
}
}

kern_return_t
IMPL(FireWireOHCIProbe, NewUserClient)
{
    if (type == kFireWireOHCIProbeDebugUserClientType) {
        IOService * service = nullptr;
        kern_return_t ret =
            Create(this, "FireWireOHCIProbeDebugUserClientProperties", &service);
        if (ret != kIOReturnSuccess) {
            return ret;
        }

        IOUserClient * client = OSDynamicCast(IOUserClient, service);
        if (client == nullptr) {
            if (service != nullptr) {
                service->release();
            }
            return kIOReturnUnsupported;
        }

        *userClient = client;
        return kIOReturnSuccess;
    }

    return NewUserClient(type, userClient, SUPERDISPATCH);
}

kern_return_t
IMPL(FireWireOHCIProbe, SetProperties)
{
    if (properties != nullptr &&
        properties->getObject("ProbeControlDebugCommand") != nullptr) {
        return ProcessDigiLiveControlDebugSetProperties(properties);
    }

    return SetProperties(properties, SUPERDISPATCH);
}

kern_return_t
IMPL(FireWireOHCIProbe, SetPowerState)
{
    gPowerStateChangeCount++;
    gPowerStateLastFlags = powerFlags;

    bool poweredOn = (powerFlags & kIOServicePowerCapabilityOn) != 0;
    if (poweredOn) {
        gPowerStateOnCount++;
    } else {
        if ((powerFlags & kIOServicePowerCapabilityLow) != 0) {
            gPowerStateLowCount++;
        } else {
            gPowerStateOffCount++;
        }
        StopAudioRefreshWorker(true);
        if (DigiLiveStreamMayNeedStop()) {
            gPowerStateLiveStopCount++;
            (void)StopDigiLiveStreamForAudio();
        }
        ResetAudioRingBuffer();
        ResetAudioOutputRingBuffer();
        ResetDigiLiveOutputState();
        ClearAudioInputBuffer();
        ClearAudioOutputBuffer();
    }

    kern_return_t ret = SetPowerState(powerFlags, SUPERDISPATCH);
    gPowerStateLastRet = ReturnCodeToProperty(ret);

    if (ret == kIOReturnSuccess && poweredOn && gAudioRuntimeDeviceStarted != 0) {
        gPowerStateWakeRestartRequestCount++;
        RequestAudioRuntimeRestart(kAudioRuntimeRestartReasonPowerOn);
    }

    PublishAudioRuntimeDiagnostics();
    return ret;
}

kern_return_t
IMPL(FireWireOHCIProbe, UserSetProperties)
{
    OSDictionary * dictionary = OSDynamicCast(OSDictionary, properties);
    if (dictionary != nullptr &&
        dictionary->getObject("ProbeControlDebugCommand") != nullptr) {
        return ProcessDigiLiveControlDebugSetProperties(dictionary);
    }

    return UserSetProperties(properties, SUPERDISPATCH);
}

kern_return_t
FireWireOHCIProbeDebugUserClient::ExternalMethod(uint64_t selector,
                                                 IOUserClientMethodArguments * arguments,
                                                 const IOUserClientMethodDispatch * dispatch,
                                                 OSObject * target,
                                                 void * reference)
{
    (void)dispatch;
    (void)target;
    (void)reference;

    if (arguments == nullptr || arguments->scalarInput == nullptr) {
        return kIOReturnBadArgument;
    }

    if (selector == kDigiLiveControlDebugSelectorMidiMessage) {
        if (arguments->scalarInputCount < 4) {
            return kIOReturnBadArgument;
        }
        return QueueDigiLiveControlDebugMidiCommand(
            static_cast<uint32_t>(arguments->scalarInput[0]),
            static_cast<uint32_t>(arguments->scalarInput[1]),
            static_cast<uint32_t>(arguments->scalarInput[2]),
            static_cast<uint32_t>(arguments->scalarInput[3]));
    }

    if (selector == kDigiLiveControlDebugSelectorFaderTarget) {
        if (arguments->scalarInputCount < 2) {
            return kIOReturnBadArgument;
        }
        return QueueDigiLiveControlDebugFaderTarget(
            static_cast<uint32_t>(arguments->scalarInput[0]),
            static_cast<uint32_t>(arguments->scalarInput[1]));
    }

    if (selector == kDigiLiveControlDebugSelectorMidiBytes) {
        if (arguments->scalarInputCount < 1) {
            return kIOReturnBadArgument;
        }

        uint32_t portNibble = static_cast<uint32_t>(arguments->scalarInput[0]);
        if (arguments->structureInput != nullptr) {
            size_t byteCount = arguments->structureInput->getLength();
            const void * bytes = arguments->structureInput->getBytesNoCopy();
            if (bytes != nullptr &&
                byteCount > 0 &&
                byteCount <= kDigiLiveControlDebugMaxByteMessageLength) {
                return QueueDigiLiveControlDebugMidiBytes(
                    portNibble,
                    reinterpret_cast<const uint8_t *>(bytes),
                    static_cast<uint32_t>(byteCount));
            }
        }

        if (arguments->scalarInputCount < 2) {
            return kIOReturnBadArgument;
        }

        uint32_t byteCount = static_cast<uint32_t>(arguments->scalarInput[1]);
        if (byteCount == 0 ||
            byteCount > kDigiLiveControlDebugMaxByteMessageLength ||
            arguments->scalarInputCount < 2u + byteCount) {
            return kIOReturnBadArgument;
        }

        uint8_t bytes[kDigiLiveControlDebugMaxByteMessageLength] = {};
        for (uint32_t i = 0; i < byteCount; ++i) {
            uint64_t byte = arguments->scalarInput[2u + i];
            if (byte > 0xffu) {
                return kIOReturnBadArgument;
            }
            bytes[i] = static_cast<uint8_t>(byte);
        }

        return QueueDigiLiveControlDebugMidiBytes(portNibble, bytes, byteCount);
    }

    return kIOReturnUnsupported;
}

kern_return_t
IMPL(FireWireOHCIProbe, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: super Start failed: 0x%x", ret);
        return ret;
    }
    gDriverInstance = this;
    PublishStartStage(1, ReturnCodeToProperty(kIOReturnSuccess));

    IOPCIDevice * pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (pciDevice == nullptr) {
        os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: provider is not IOPCIDevice");
        return kIOReturnUnsupported;
    }
    PublishStartStage(2, ReturnCodeToProperty(kIOReturnSuccess));

    ret = pciDevice->Open(this, 0);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: PCI open failed: 0x%x", ret);
        return ret;
    }
    PublishStartStage(3, ReturnCodeToProperty(ret));

    uint32_t vendorDevice = 0xffffffff;
    uint32_t classRevision = 0xffffffff;
    uint32_t commandStatusBefore = 0xffffffff;
    uint32_t commandStatusAfter = 0xffffffff;
    uint32_t bar0Raw = 0xffffffff;
    pciDevice->ConfigurationRead32(0x00, &vendorDevice);
    pciDevice->ConfigurationRead32(0x08, &classRevision);
    pciDevice->ConfigurationRead32(kIOPCIConfigurationOffsetCommand, &commandStatusBefore);
    pciDevice->ConfigurationRead32(kIOPCIConfigurationOffsetBaseAddress0, &bar0Raw);

    uint16_t command = commandStatusBefore & 0xffff;
    uint16_t wantedCommand = command | kIOPCICommandMemorySpace | kIOPCICommandBusLead;
    if (wantedCommand != command) {
        pciDevice->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand, wantedCommand);
    }
    pciDevice->ConfigurationRead32(kIOPCIConfigurationOffsetCommand, &commandStatusAfter);

    uint8_t memoryIndex = 0xff;
    uint64_t barSize = 0;
    uint8_t barType = 0xff;
    ret = pciDevice->GetBARInfo(kBAR0, &memoryIndex, &barSize, &barType);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT,
               "FireWireOHCIProbe: GetBARInfo(BAR0) failed: 0x%x vendorDevice=0x%08x classRevision=0x%08x",
               ret,
               vendorDevice,
               classRevision);
        pciDevice->Close(this, 0);
        return ret;
    }
    gPCIDevice = pciDevice;
    gPCIMemoryIndex = memoryIndex;
    PublishStartStage(4, memoryIndex);
    IOSleep(kInitialAdapterSettleMilliseconds);

    uint32_t softwareResetBefore = 0xffffffff;
    uint32_t softwareResetAfter = 0xffffffff;
    uint32_t softwareResetLoops = 0;
    kern_return_t softwareResetRet = SoftwareResetOHCI(pciDevice,
                                                       memoryIndex,
                                                       &softwareResetBefore,
                                                       &softwareResetAfter,
                                                       &softwareResetLoops);
    PublishStartStage(5, ReturnCodeToProperty(softwareResetRet));

    uint32_t bringUpAttempted = 1;
    uint32_t bringUpReady = 0;
    uint32_t bringUpHcControlBefore = 0xffffffff;
    uint32_t bringUpHcControlAfterSetup = 0xffffffff;
    uint32_t bringUpHcControlAfterLink = 0xffffffff;
    uint32_t bringUpBusOptions = 0xffffffff;
    uint32_t bringUpGeneratedBusOptions = 0xffffffff;
    uint32_t bringUpConfigROMHeader = 0xffffffff;
    uint32_t bringUpRootDirectoryHeader = 0xffffffff;
    uint32_t bringUpGuidHi = 0xffffffff;
    uint32_t bringUpGuidLo = 0xffffffff;
    uint32_t phyReg4Before = 0xffffffff;
    uint32_t phyReg4After = 0xffffffff;
    uint32_t phyReg5Before = 0xffffffff;
    uint32_t phyReg5After = 0xffffffff;
    kern_return_t phyReg4UpdateRet = kIOReturnNotReady;
    kern_return_t phyReg5UpdateRet = kIOReturnNotReady;
    uint32_t phy1394aHcControlBefore = 0xffffffff;
    uint32_t phy1394aHcControlAfter = 0xffffffff;
    uint32_t phy1394aReg2 = 0xffffffff;
    uint32_t phy1394aPage1Reg8 = 0xffffffff;
    uint32_t phy1394aPageSelectBefore = 0xffffffff;
    uint32_t phy1394aPageSelectAfter = 0xffffffff;
    uint32_t phy1394aSupported = 0xffffffff;
    uint32_t phy1394aReg5Before = 0xffffffff;
    uint32_t phy1394aReg5After = 0xffffffff;
    kern_return_t phy1394aRet = kIOReturnNotReady;
    kern_return_t phy1394aReg2ReadRet = kIOReturnNotReady;
    kern_return_t phy1394aPage1ReadRet = kIOReturnNotReady;
    kern_return_t phy1394aReg5UpdateRet = kIOReturnNotReady;
    uint32_t longResetAttempted = 0;
    uint32_t longResetReg1Before = 0xffffffff;
    uint32_t longResetReg1After = 0xffffffff;
    kern_return_t longResetRet = kIOReturnNotReady;
    uint32_t busResetAckAttempted = 0;
    uint32_t busResetAckLoops = 0;
    uint32_t busResetAckEventBefore = 0xffffffff;
    uint32_t busResetAckEventAfter = 0xffffffff;
    uint32_t busResetAckMaskAfter = 0xffffffff;
    uint32_t longBusResetAckAttempted = 0;
    uint32_t longBusResetAckLoops = 0;
    uint32_t longBusResetAckEventBefore = 0xffffffff;
    uint32_t longBusResetAckEventAfter = 0xffffffff;
    uint32_t longBusResetAckMaskAfter = 0xffffffff;
    uint32_t forceClearBeforeLongReset = 0;
    uint32_t forceClearEventAfter = 0xffffffff;
    uint32_t forceClearMaskAfter = 0xffffffff;
    uint32_t selfIDWords[kSelfIDWordCount] = {};
    for (size_t i = 0; i < kSelfIDWordCount; ++i) {
        selfIDWords[i] = 0xffffffff;
    }
    uint32_t phyRegisterValues[kPhyRegisterCount] = {};
    uint32_t phyRegisterReadRets[kPhyRegisterCount] = {};
    for (size_t i = 0; i < kPhyRegisterCount; ++i) {
        phyRegisterValues[i] = 0xffffffff;
        phyRegisterReadRets[i] = ReturnCodeToProperty(kIOReturnNotReady);
    }
    uint32_t phyPortRawStatus[kPhyPortCount] = {};
    uint32_t phyPortDecodedStatus[kPhyPortCount] = {};
    uint32_t phyPortReadRets[kPhyPortCount] = {};
    for (size_t i = 0; i < kPhyPortCount; ++i) {
        phyPortRawStatus[i] = 0xffffffff;
        phyPortDecodedStatus[i] = 0xffffffff;
        phyPortReadRets[i] = ReturnCodeToProperty(kIOReturnNotReady);
    }
    uint32_t waitLoops = 0;
    uint32_t waitNodeID = 0xffffffff;
    uint32_t waitSelfIDCount = 0xffffffff;
    uint32_t waitIntEvent = 0xffffffff;
    uint32_t shortResetAttemptCount = 0;
    uint32_t shortResetSuccessAttempt = 0xffffffff;
    uint32_t shortResetReg5Before[kShortResetAttemptCount] = {};
    uint32_t shortResetReg5After[kShortResetAttemptCount] = {};
    uint32_t shortResetUpdateRets[kShortResetAttemptCount] = {};
    uint32_t shortResetAckLoops[kShortResetAttemptCount] = {};
    uint32_t shortResetAckEventBefore[kShortResetAttemptCount] = {};
    uint32_t shortResetAckEventAfter[kShortResetAttemptCount] = {};
    uint32_t shortResetAckMaskAfter[kShortResetAttemptCount] = {};
    uint32_t shortResetWaitLoops[kShortResetAttemptCount] = {};
    uint32_t shortResetWaitNodeID[kShortResetAttemptCount] = {};
    uint32_t shortResetWaitSelfIDCount[kShortResetAttemptCount] = {};
    uint32_t shortResetWaitIntEvent[kShortResetAttemptCount] = {};
    for (size_t i = 0; i < kShortResetAttemptCount; ++i) {
        shortResetReg5Before[i] = 0xffffffff;
        shortResetReg5After[i] = 0xffffffff;
        shortResetUpdateRets[i] = ReturnCodeToProperty(kIOReturnNotReady);
        shortResetAckEventBefore[i] = 0xffffffff;
        shortResetAckEventAfter[i] = 0xffffffff;
        shortResetAckMaskAfter[i] = 0xffffffff;
        shortResetWaitNodeID[i] = 0xffffffff;
        shortResetWaitSelfIDCount[i] = 0xffffffff;
        shortResetWaitIntEvent[i] = 0xffffffff;
    }
    uint32_t secondWaitLoops = 0;
    uint32_t secondWaitNodeID = 0xffffffff;
    uint32_t secondWaitSelfIDCount = 0xffffffff;
    uint32_t secondWaitIntEvent = 0xffffffff;
    AsyncReadDiagnostics asyncRead = {};
    InitializeAsyncDiagnostics(&asyncRead);
    IsoTestDiagnostics isoTest = {};
    InitializeIsoTestDiagnostics(&isoTest);
    DigiDuplexDiagnostics digiDuplex = {};
    InitializeDigiDuplexDiagnostics(&digiDuplex);

    pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, &bringUpHcControlBefore);
    pciDevice->MemoryRead32(memoryIndex, kOhciBusOptionsOffset, &bringUpBusOptions);
    pciDevice->MemoryRead32(memoryIndex, kOhciGuidHiOffset, &bringUpGuidHi);
    pciDevice->MemoryRead32(memoryIndex, kOhciGuidLoOffset, &bringUpGuidLo);

    CreateDMABuffer(pciDevice, kSelfIDBufferSize, &gSelfIDBuffer);
    CreateDMABuffer(pciDevice, kConfigROMBufferSize, &gConfigROMBuffer);
    bringUpReady = (gSelfIDBuffer.result == kIOReturnSuccess &&
                    gConfigROMBuffer.result == kIOReturnSuccess &&
                    gSelfIDBuffer.segmentCount > 0 &&
                    gConfigROMBuffer.segmentCount > 0 &&
                    gSelfIDBuffer.dmaSegment.address <= 0xffffffffull &&
                    gConfigROMBuffer.dmaSegment.address <= 0xffffffffull) ? 1 : 0;
    PublishStartStage(6, bringUpReady);

    if (bringUpReady != 0) {
        PublishStartStage(7, bringUpReady);
        bringUpConfigROMHeader = WriteConfigROM(&gConfigROMBuffer,
                                                bringUpBusOptions,
                                                bringUpGuidHi,
                                                bringUpGuidLo,
                                                &bringUpGeneratedBusOptions,
                                                &bringUpRootDirectoryHeader);

        pciDevice->MemoryWrite32(memoryIndex,
                                 kOhciHcControlSetOffset,
                                 kOhciHcControlLPS | kOhciHcControlPostedWriteEnable);
        IOSleep(50);
        pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, &bringUpHcControlAfterSetup);
        phy1394aRet = Configure1394AEnhancements(pciDevice,
                                                 memoryIndex,
                                                 &phy1394aHcControlBefore,
                                                 &phy1394aHcControlAfter,
                                                 &phy1394aReg2,
                                                 &phy1394aPage1Reg8,
                                                 &phy1394aPageSelectBefore,
                                                 &phy1394aPageSelectAfter,
                                                 &phy1394aSupported,
                                                 &phy1394aReg5Before,
                                                 &phy1394aReg5After,
                                                 &phy1394aReg2ReadRet,
                                                 &phy1394aPage1ReadRet,
                                                 &phy1394aReg5UpdateRet);

        pciDevice->MemoryWrite32(memoryIndex, kOhciHcControlClearOffset, kOhciHcControlNoByteSwapData);
        pciDevice->MemoryWrite32(memoryIndex,
                                 kOhciSelfIdBufferOffset,
                                 static_cast<uint32_t>(gSelfIDBuffer.dmaSegment.address));
        pciDevice->MemoryWrite32(memoryIndex,
                                 kOhciLinkControlSetOffset,
                                 kOhciLinkControlCycleTimerEnable | kOhciLinkControlCycleMaster);
        pciDevice->MemoryWrite32(memoryIndex, kOhciAtRetriesOffset, kOhciATRetries);
        pciDevice->MemoryWrite32(memoryIndex, kOhciInitialChannelsAvailableHiOffset, 0xfffffffe);
        pciDevice->MemoryWrite32(memoryIndex, kOhciFairnessControlOffset, 0x3f);
        pciDevice->MemoryWrite32(memoryIndex, kOhciFairnessControlOffset, 0);
        pciDevice->MemoryWrite32(memoryIndex, kOhciPhyUpperBoundOffset, 0x00010000);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskClearOffset, 0xffffffff);
        pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskSetOffset, kOhciDiagnosticInterruptMask);
        pciDevice->MemoryWrite32(memoryIndex, kOhciConfigRomHeaderOffset, bringUpConfigROMHeader);
        pciDevice->MemoryWrite32(memoryIndex, kOhciBusOptionsOffset, bringUpGeneratedBusOptions);
        pciDevice->MemoryWrite32(memoryIndex,
                                 kOhciConfigRomMapOffset,
                                 static_cast<uint32_t>(gConfigROMBuffer.dmaSegment.address));
        pciDevice->MemoryWrite32(memoryIndex, kOhciAsReqFilterHiSetOffset, 0x80000000);

        phyReg4UpdateRet = UpdatePhyRegister(pciDevice,
                                             memoryIndex,
                                             4,
                                             0,
                                             kPhyLinkActive | kPhyContender,
                                             &phyReg4Before,
                                             &phyReg4After);

        pciDevice->MemoryWrite32(memoryIndex,
                                 kOhciHcControlSetOffset,
                                 kOhciHcControlLinkEnable | kOhciHcControlBIBImageValid);
        pciDevice->MemoryWrite32(memoryIndex,
                                 kOhciLinkControlSetOffset,
                                 kOhciLinkControlReceiveSelfID | kOhciLinkControlReceivePhyPacket);
        pciDevice->MemoryRead32(memoryIndex, kOhciHcControlSetOffset, &bringUpHcControlAfterLink);
        PublishStartStage(8, bringUpHcControlAfterLink);

        for (size_t attempt = 0; attempt < kShortResetAttemptCount; ++attempt) {
            PublishStartStage(9, static_cast<uint32_t>(attempt));
            if (attempt > 0) {
                pciDevice->MemoryWrite32(memoryIndex,
                                         kOhciLinkControlClearOffset,
                                         kOhciLinkControlReceiveSelfID | kOhciLinkControlReceivePhyPacket);
                pciDevice->MemoryWrite32(memoryIndex, kOhciHcControlClearOffset, kOhciHcControlLinkEnable);
                IOSleep(kShortResetSettleMilliseconds);
                pciDevice->MemoryWrite32(memoryIndex,
                                         kOhciSelfIdBufferOffset,
                                         static_cast<uint32_t>(gSelfIDBuffer.dmaSegment.address));
                phyReg4UpdateRet = UpdatePhyRegister(pciDevice,
                                                     memoryIndex,
                                                     4,
                                                     0,
                                                     kPhyLinkActive | kPhyContender,
                                                     &phyReg4Before,
                                                     &phyReg4After);
                pciDevice->MemoryWrite32(memoryIndex,
                                         kOhciHcControlSetOffset,
                                         kOhciHcControlLinkEnable | kOhciHcControlBIBImageValid);
                pciDevice->MemoryWrite32(memoryIndex,
                                         kOhciLinkControlSetOffset,
                                         kOhciLinkControlReceiveSelfID | kOhciLinkControlReceivePhyPacket);
                pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
                pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskSetOffset, kOhciDiagnosticInterruptMask);
                IOSleep(kShortResetSettleMilliseconds);
            }

            uint32_t candidateReg5Before = 0xffffffff;
            uint32_t candidateReg5After = 0xffffffff;
            kern_return_t candidateResetRet = UpdatePhyRegister(pciDevice,
                                                                memoryIndex,
                                                                5,
                                                                0,
                                                                kPhyBusShortReset,
                                                                &candidateReg5Before,
                                                                &candidateReg5After);
            shortResetAttemptCount = static_cast<uint32_t>(attempt + 1);
            shortResetReg5Before[attempt] = candidateReg5Before;
            shortResetReg5After[attempt] = candidateReg5After;
            shortResetUpdateRets[attempt] = ReturnCodeToProperty(candidateResetRet);
            phyReg5UpdateRet = candidateResetRet;
            phyReg5Before = candidateReg5Before;
            phyReg5After = candidateReg5After;

            IOSleep(kShortResetSettleMilliseconds);

            uint32_t candidateAckAttempted = 0;
            uint32_t candidateAckLoops = 0;
            uint32_t candidateAckEventBefore = 0xffffffff;
            uint32_t candidateAckEventAfter = 0xffffffff;
            uint32_t candidateAckMaskAfter = 0xffffffff;
            AcknowledgeBusResetLikeLinux(pciDevice,
                                         memoryIndex,
                                         20,
                                         10,
                                         &candidateAckAttempted,
                                         &candidateAckLoops,
                                         &candidateAckEventBefore,
                                         &candidateAckEventAfter,
                                         &candidateAckMaskAfter);
            busResetAckAttempted = candidateAckAttempted;
            busResetAckLoops = candidateAckLoops;
            busResetAckEventBefore = candidateAckEventBefore;
            busResetAckEventAfter = candidateAckEventAfter;
            busResetAckMaskAfter = candidateAckMaskAfter;
            shortResetAckLoops[attempt] = candidateAckLoops;
            shortResetAckEventBefore[attempt] = candidateAckEventBefore;
            shortResetAckEventAfter[attempt] = candidateAckEventAfter;
            shortResetAckMaskAfter[attempt] = candidateAckMaskAfter;

            uint32_t candidateWaitLoops = 0;
            uint32_t candidateWaitNodeID = 0xffffffff;
            uint32_t candidateWaitSelfIDCount = 0xffffffff;
            uint32_t candidateWaitIntEvent = 0xffffffff;
            WaitForSelfID(pciDevice,
                          memoryIndex,
                          kShortResetWaitAttempts,
                          kShortResetWaitMilliseconds,
                          &candidateWaitLoops,
                          &candidateWaitNodeID,
                          &candidateWaitSelfIDCount,
                          &candidateWaitIntEvent);
            waitLoops = candidateWaitLoops;
            waitNodeID = candidateWaitNodeID;
            waitSelfIDCount = candidateWaitSelfIDCount;
            waitIntEvent = candidateWaitIntEvent;
            shortResetWaitLoops[attempt] = candidateWaitLoops;
            shortResetWaitNodeID[attempt] = candidateWaitNodeID;
            shortResetWaitSelfIDCount[attempt] = candidateWaitSelfIDCount;
            shortResetWaitIntEvent[attempt] = candidateWaitIntEvent;

            if (IsSelfIDReady(waitNodeID, waitSelfIDCount, waitIntEvent)) {
                shortResetSuccessAttempt = static_cast<uint32_t>(attempt);
                PublishStartStage(10, waitSelfIDCount);
                break;
            }

            IOSleep(200);
        }
        if (!IsSelfIDReady(waitNodeID, waitSelfIDCount, waitIntEvent)) {
            PublishStartStage(11, waitSelfIDCount);
            longResetAttempted = 1;
            forceClearBeforeLongReset = 1;
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntEventClearOffset, 0xffffffff);
            pciDevice->MemoryRead32(memoryIndex, kOhciIntEventSetOffset, &forceClearEventAfter);
            pciDevice->MemoryWrite32(memoryIndex,
                                     kOhciIntMaskSetOffset,
                                     kOhciEventBusReset | kOhciEventMasterIntEnable);
            pciDevice->MemoryRead32(memoryIndex, kOhciIntMaskSetOffset, &forceClearMaskAfter);
            longResetRet = UpdatePhyRegister(pciDevice,
                                             memoryIndex,
                                             1,
                                             0,
                                             kPhyBusReset,
                                             &longResetReg1Before,
                                             &longResetReg1After);
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskSetOffset, kOhciEventBusReset);
            AcknowledgeBusResetLikeLinux(pciDevice,
                                         memoryIndex,
                                         20,
                                         10,
                                         &longBusResetAckAttempted,
                                         &longBusResetAckLoops,
                                         &longBusResetAckEventBefore,
                                         &longBusResetAckEventAfter,
                                         &longBusResetAckMaskAfter);
            WaitForSelfID(pciDevice,
                          memoryIndex,
                          80,
                          50,
                          &secondWaitLoops,
                          &secondWaitNodeID,
                          &secondWaitSelfIDCount,
                          &secondWaitIntEvent);
            PublishStartStage(12, secondWaitSelfIDCount);
        }
        for (size_t i = 0; i < kPhyRegisterCount; ++i) {
            uint32_t value = 0xffffffff;
            kern_return_t readRet = ReadPhyRegister(pciDevice,
                                                    memoryIndex,
                                                    static_cast<uint8_t>(i),
                                                    &value);
            phyRegisterReadRets[i] = ReturnCodeToProperty(readRet);
            phyRegisterValues[i] = value;
        }
        for (size_t i = 0; i < kPhyPortCount; ++i) {
            uint32_t rawStatus = 0xffffffff;
            uint32_t decodedStatus = 0xffffffff;
            kern_return_t readRet = ReadPhyPortStatus(pciDevice,
                                                      memoryIndex,
                                                      static_cast<uint8_t>(i),
                                                      &rawStatus,
                                                      &decodedStatus);
            phyPortReadRets[i] = ReturnCodeToProperty(readRet);
            phyPortRawStatus[i] = rawStatus;
            phyPortDecodedStatus[i] = decodedStatus;
        }
        ReadSelfIDWords(&gSelfIDBuffer, selfIDWords, kSelfIDWordCount);
        uint32_t selectedNodeID = IsSelfIDReady(waitNodeID, waitSelfIDCount, waitIntEvent) ?
                                  waitNodeID :
                                  secondWaitNodeID;
        if (IsSelfIDReady(selectedNodeID,
                          IsSelfIDReady(waitNodeID, waitSelfIDCount, waitIntEvent) ?
                              waitSelfIDCount :
                              secondWaitSelfIDCount,
                          IsSelfIDReady(waitNodeID, waitSelfIDCount, waitIntEvent) ?
                              waitIntEvent :
                              secondWaitIntEvent)) {
            IOSleep(kPostSelfIDSettleMilliseconds);
            PublishStartStage(13, selectedNodeID);
            if (kFastKnownDigi003InitEnabled != 0) {
                asyncRead.attempted = 0;
                asyncRead.localNodeID = selectedNodeID & 0xffffu;
                asyncRead.destinationID = asyncRead.localNodeID & 0xffc0u;
                asyncRead.ready = 1;
                gDigiDestinationID = asyncRead.destinationID;
            } else {
                RunAsyncConfigROMRead(pciDevice, memoryIndex, selectedNodeID, &asyncRead, &digiDuplex);
                gDigiDestinationID = asyncRead.destinationID;
            }
            PublishStartStage(14, asyncRead.ready);
        }
        PublishStartStage(15, 0);
        if (kFastKnownDigi003InitEnabled == 0) {
            RunIsoContextProbe(pciDevice, memoryIndex, &isoTest);
        }
        PublishStartStage(16, isoTest.attempted);
    }

    RegisterSnapshot ohciRegisters[] = {
        {"ProbeOHCIVersion", kOhciVersionOffset, 0xffffffff},
        {"ProbeOHCIGuidRom", kOhciGuidRomOffset, 0xffffffff},
        {"ProbeOHCIATRetries", kOhciAtRetriesOffset, 0xffffffff},
        {"ProbeOHCIConfigROMHeader", kOhciConfigRomHeaderOffset, 0xffffffff},
        {"ProbeOHCIBusID", kOhciBusIdOffset, 0xffffffff},
        {"ProbeOHCIBusOptions", kOhciBusOptionsOffset, 0xffffffff},
        {"ProbeOHCIGuidHi", kOhciGuidHiOffset, 0xffffffff},
        {"ProbeOHCIGuidLo", kOhciGuidLoOffset, 0xffffffff},
        {"ProbeOHCIConfigROMMap", kOhciConfigRomMapOffset, 0xffffffff},
        {"ProbeOHCIVendorID", kOhciVendorIdOffset, 0xffffffff},
        {"ProbeOHCIHCControlSet", kOhciHcControlSetOffset, 0xffffffff},
        {"ProbeOHCIHCControlClear", kOhciHcControlClearOffset, 0xffffffff},
        {"ProbeOHCISelfIDBuffer", kOhciSelfIdBufferOffset, 0xffffffff},
        {"ProbeOHCISelfIDCount", kOhciSelfIdCountOffset, 0xffffffff},
        {"ProbeOHCIIntEventSet", kOhciIntEventSetOffset, 0xffffffff},
        {"ProbeOHCIIntMaskSet", kOhciIntMaskSetOffset, 0xffffffff},
        {"ProbeOHCIIsoXmitIntEventSet", kOhciIsoXmitIntEventSetOffset, 0xffffffff},
        {"ProbeOHCIIsoXmitIntMaskSet", kOhciIsoXmitIntMaskSetOffset, 0xffffffff},
        {"ProbeOHCIIsoRecvIntEventSet", kOhciIsoRecvIntEventSetOffset, 0xffffffff},
        {"ProbeOHCIIsoRecvIntMaskSet", kOhciIsoRecvIntMaskSetOffset, 0xffffffff},
        {"ProbeOHCIInitialBandwidthAvailable", kOhciInitialBandwidthAvailableOffset, 0xffffffff},
        {"ProbeOHCIInitialChannelsAvailableHi", kOhciInitialChannelsAvailableHiOffset, 0xffffffff},
        {"ProbeOHCIInitialChannelsAvailableLo", kOhciInitialChannelsAvailableLoOffset, 0xffffffff},
        {"ProbeOHCIFairnessControl", kOhciFairnessControlOffset, 0xffffffff},
        {"ProbeOHCILinkControlSet", kOhciLinkControlSetOffset, 0xffffffff},
        {"ProbeOHCINodeID", kOhciNodeIdOffset, 0xffffffff},
        {"ProbeOHCIPhyControl", kOhciPhyControlOffset, 0xffffffff},
        {"ProbeOHCIIsochronousCycleTimer0", kOhciIsochronousCycleTimerOffset, 0xffffffff},
        {"ProbeOHCIIsochronousCycleTimer1", kOhciIsochronousCycleTimerOffset, 0xffffffff},
        {"ProbeOHCIIsochronousCycleTimer2", kOhciIsochronousCycleTimerOffset, 0xffffffff},
        {"ProbeOHCIAsReqFilterHiSet", kOhciAsReqFilterHiSetOffset, 0xffffffff},
        {"ProbeOHCIAsReqFilterLoSet", kOhciAsReqFilterLoSetOffset, 0xffffffff},
        {"ProbeOHCIPhyReqFilterHiSet", kOhciPhyReqFilterHiSetOffset, 0xffffffff},
        {"ProbeOHCIPhyReqFilterLoSet", kOhciPhyReqFilterLoSetOffset, 0xffffffff},
        {"ProbeOHCIPhyUpperBound", kOhciPhyUpperBoundOffset, 0xffffffff},
        {"ProbeOHCIAsReqTrContextControlSet", kOhciAsReqTrContextControlSetOffset, 0xffffffff},
        {"ProbeOHCIAsReqTrCommandPtr", kOhciAsReqTrCommandPtrOffset, 0xffffffff},
        {"ProbeOHCIAsReqRcvContextControlSet", kOhciAsReqRcvContextControlSetOffset, 0xffffffff},
        {"ProbeOHCIAsReqRcvCommandPtr", kOhciAsReqRcvCommandPtrOffset, 0xffffffff},
        {"ProbeOHCIAsRspRcvContextControlSet", kOhciAsRspRcvContextControlSetOffset, 0xffffffff},
        {"ProbeOHCIAsRspRcvCommandPtr", kOhciAsRspRcvCommandPtrOffset, 0xffffffff},
        {"ProbeOHCIIsoXmit0ContextControlSet", OhciIsoXmitContextControlSetOffset(0), 0xffffffff},
        {"ProbeOHCIIsoXmit0CommandPtr", OhciIsoXmitCommandPtrOffset(0), 0xffffffff},
        {"ProbeOHCIIsoRecv0ContextControlSet", OhciIsoRcvContextControlSetOffset(0), 0xffffffff},
        {"ProbeOHCIIsoRecv0CommandPtr", OhciIsoRcvCommandPtrOffset(0), 0xffffffff},
        {"ProbeOHCIIsoRecv0ContextMatch", OhciIsoRcvContextMatchOffset(0), 0xffffffff},
    };
    constexpr size_t kOhciRegisterCount = sizeof(ohciRegisters) / sizeof(ohciRegisters[0]);
    ReadRegisterSnapshot(pciDevice, memoryIndex, ohciRegisters, kOhciRegisterCount);
    PublishStartStage(17, 0);

    bool memoryReadSucceeded = false;
    for (size_t i = 0; i < kOhciRegisterCount; ++i) {
        memoryReadSucceeded = memoryReadSucceeded || (ohciRegisters[i].value != 0xffffffff);
    }
    if (!memoryReadSucceeded) {
        os_log(OS_LOG_DEFAULT,
               "FireWireOHCIProbe: BAR0 MemoryRead32 likely failed: BAR0 index=%u",
               memoryIndex);
    }

    CopyDigiCaptureForAudio(&digiDuplex);
    PublishStartStage(18, gAudioCaptureFrameCount);
    kern_return_t audioConfigureRet = ConfigureAudioDevice(this);
    PublishStartStage(19, ReturnCodeToProperty(audioConfigureRet));

    OSDictionary * diagnostics = OSDictionary::withCapacity(2048);
    if (diagnostics != nullptr) {
        AddNumberProperty(diagnostics, "ProbeVendorDevice", vendorDevice, 32);
        AddNumberProperty(diagnostics, "ProbeVendorID", vendorDevice & 0xffff, 16);
        AddNumberProperty(diagnostics, "ProbeDeviceID", (vendorDevice >> 16) & 0xffff, 16);
        AddNumberProperty(diagnostics, "ProbeClassRevision", classRevision, 32);
        AddNumberProperty(diagnostics, "ProbeClassCode", (classRevision >> 8) & 0xffffff, 32);
        AddNumberProperty(diagnostics, "ProbeRevisionID", classRevision & 0xff, 8);
        AddNumberProperty(diagnostics, "ProbeCommandStatusBefore", commandStatusBefore, 32);
        AddNumberProperty(diagnostics, "ProbeCommandBefore", commandStatusBefore & 0xffff, 16);
        AddNumberProperty(diagnostics, "ProbeCommandStatusAfter", commandStatusAfter, 32);
        AddNumberProperty(diagnostics, "ProbeCommandAfter", commandStatusAfter & 0xffff, 16);
        AddNumberProperty(diagnostics, "ProbeBAR0Raw", bar0Raw, 32);
        AddNumberProperty(diagnostics, "ProbeBAR0Index", memoryIndex, 8);
        AddNumberProperty(diagnostics, "ProbeBAR0Size", barSize, 64);
        AddNumberProperty(diagnostics, "ProbeBAR0Type", barType, 8);
        AddNumberProperty(diagnostics, "ProbeMemoryReadSucceeded", memoryReadSucceeded ? 1 : 0, 8);
        AddNumberProperty(diagnostics, "ProbeFastKnownDigi003InitEnabled", kFastKnownDigi003InitEnabled, 32);
        AddNumberProperty(diagnostics, "ProbeAudioConfigureRet", ReturnCodeToProperty(audioConfigureRet), 32);
        AddNumberProperty(diagnostics, "ProbeAudioDeviceCreateRet", gAudioDeviceCreateRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStreamCreateRet", gAudioStreamCreateRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioAddStreamRet", gAudioAddStreamRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputStreamCreateRet", gAudioOutputStreamCreateRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputAddStreamRet", gAudioOutputAddStreamRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioAddObjectRet", gAudioAddObjectRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioIOHandlerRet", gAudioIOHandlerRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStreamActiveRet", gAudioStreamActiveRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputStreamActiveRet", gAudioOutputStreamActiveRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioRegisterIOThreadRet", gAudioRegisterIOThreadRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStartIOThreadRet", gAudioStartIOThreadRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStartDeviceCount", gAudioStartDeviceCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStopDeviceCount", gAudioStopDeviceCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStartDeviceRet", gAudioStartDeviceRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStopDeviceRet", gAudioStopDeviceRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStartDeviceObjectID", gAudioStartDeviceObjectID, 32);
        AddNumberProperty(diagnostics, "ProbeAudioStopDeviceObjectID", gAudioStopDeviceObjectID, 32);
        AddNumberProperty(diagnostics, "ProbeAudioBufferCreateRet", gAudioBufferCreateRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioBufferSetLengthRet", gAudioBufferSetLengthRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioBufferRangeRet", gAudioBufferRangeRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputBufferCreateRet", gAudioOutputBufferCreateRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputBufferSetLengthRet", gAudioOutputBufferSetLengthRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputBufferRangeRet", gAudioOutputBufferRangeRet, 32);
        AddNumberProperty(diagnostics, "ProbeAudioBufferFrameCount", kAudioInputBufferFrameCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioBufferBytes", kAudioInputBufferBytes, 64);
        AddNumberProperty(diagnostics, "ProbeAudioOutputBufferFrameCount", kAudioOutputBufferFrameCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputBufferBytes", kAudioOutputBufferBytes, 64);
        AddNumberProperty(diagnostics, "ProbeAudioRingBufferFrameCount", kAudioRingBufferFrameCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputRingBufferFrameCount", kAudioOutputRingBufferFrameCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputBufferOffsetMode", kAudioOutputBufferOffsetMode, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputRingPrebufferFrames", kAudioOutputRingPrebufferFrames, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputRingKeepFrames", kAudioOutputRingKeepFrames, 32);
        AddNumberProperty(diagnostics, "ProbeDigiLiveOutputServiceAheadPackets", kDigiLiveOutputServiceAheadPackets, 32);
        AddNumberProperty(diagnostics, "ProbeDigiLiveOutputSilenceAheadPackets", kDigiLiveOutputSilenceAheadPackets, 32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiLiveOutputWorkerPushAudioCount",
                          gDigiLiveOutputWorkerPushAudioCount,
                          64);
        AddNumberProperty(diagnostics,
                          "ProbeDigiLiveOutputWorkerPushSkippedAudioCount",
                          gDigiLiveOutputWorkerPushSkippedAudioCount,
                          64);
        AddNumberProperty(diagnostics, "ProbeAudioCaptureFrameCount", gAudioCaptureFrameCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioCapturePeakAbs", gAudioCapturePeakAbs, 32);
        AddNumberProperty(diagnostics, "ProbeAudioInputCallbackCount", gAudioInputCallbackCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioInputLastBufferFrameSize", gAudioInputLastBufferFrameSize, 32);
        AddNumberProperty(diagnostics, "ProbeAudioInputLastSampleTime", gAudioInputLastSampleTime, 64);
        AddNumberProperty(diagnostics, "ProbeAudioOutputCallbackCount", gAudioOutputCallbackCount, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputLastBufferFrameSize", gAudioOutputLastBufferFrameSize, 32);
        AddNumberProperty(diagnostics, "ProbeAudioOutputLastSampleTime", gAudioOutputLastSampleTime, 64);
        AddNumberProperty(diagnostics, "ProbeAudioZeroTimestampHostTime", gAudioZeroTimestampHostTime, 64);
        AddNumberProperty(diagnostics, "ProbeSoftwareResetRet", ReturnCodeToProperty(softwareResetRet), 32);
        AddNumberProperty(diagnostics, "ProbeSoftwareResetBefore", softwareResetBefore, 32);
        AddNumberProperty(diagnostics, "ProbeSoftwareResetAfter", softwareResetAfter, 32);
        AddNumberProperty(diagnostics, "ProbeSoftwareResetLoops", softwareResetLoops, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpAttempted", bringUpAttempted, 8);
        AddNumberProperty(diagnostics, "ProbeBringUpReady", bringUpReady, 8);
        AddNumberProperty(diagnostics, "ProbeBringUpHCControlBefore", bringUpHcControlBefore, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpHCControlAfterSetup", bringUpHcControlAfterSetup, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpHCControlAfterLink", bringUpHcControlAfterLink, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpBusOptions", bringUpBusOptions, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpGeneratedBusOptions", bringUpGeneratedBusOptions, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpConfigROMHeader", bringUpConfigROMHeader, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpRootDirectoryHeader", bringUpRootDirectoryHeader, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpGuidHi", bringUpGuidHi, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpGuidLo", bringUpGuidLo, 32);
        AddNumberProperty(diagnostics,
                          "ProbeBringUpPhyReg4UpdateRet",
                          ReturnCodeToProperty(phyReg4UpdateRet),
                          32);
        AddNumberProperty(diagnostics, "ProbeBringUpPhyReg4Before", phyReg4Before, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpPhyReg4After", phyReg4After, 32);
        AddNumberProperty(diagnostics,
                          "ProbeBringUpPhyReg5UpdateRet",
                          ReturnCodeToProperty(phyReg5UpdateRet),
                          32);
        AddNumberProperty(diagnostics, "ProbeBringUpPhyReg5Before", phyReg5Before, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpPhyReg5After", phyReg5After, 32);
        AddNumberProperty(diagnostics, "Probe1394AResult", ReturnCodeToProperty(phy1394aRet), 32);
        AddNumberProperty(diagnostics, "Probe1394AHCControlBefore", phy1394aHcControlBefore, 32);
        AddNumberProperty(diagnostics, "Probe1394AHCControlAfter", phy1394aHcControlAfter, 32);
        AddNumberProperty(diagnostics, "Probe1394APhyReg2ReadRet", ReturnCodeToProperty(phy1394aReg2ReadRet), 32);
        AddNumberProperty(diagnostics, "Probe1394APhyReg2", phy1394aReg2, 32);
        AddNumberProperty(diagnostics, "Probe1394APage1ReadRet", ReturnCodeToProperty(phy1394aPage1ReadRet), 32);
        AddNumberProperty(diagnostics, "Probe1394APage1Reg8", phy1394aPage1Reg8, 32);
        AddNumberProperty(diagnostics, "Probe1394APageSelectBefore", phy1394aPageSelectBefore, 32);
        AddNumberProperty(diagnostics, "Probe1394APageSelectAfter", phy1394aPageSelectAfter, 32);
        AddNumberProperty(diagnostics, "Probe1394ASupported", phy1394aSupported, 32);
        AddNumberProperty(diagnostics,
                          "Probe1394APhyReg5UpdateRet",
                          ReturnCodeToProperty(phy1394aReg5UpdateRet),
                          32);
        AddNumberProperty(diagnostics, "Probe1394APhyReg5Before", phy1394aReg5Before, 32);
        AddNumberProperty(diagnostics, "Probe1394APhyReg5After", phy1394aReg5After, 32);
        AddNumberProperty(diagnostics, "ProbeBringUpDiagnosticInterruptMask", kOhciDiagnosticInterruptMask, 32);
        AddNumberProperty(diagnostics, "ProbeShortResetConfiguredAttempts", kShortResetAttemptCount, 32);
        AddNumberProperty(diagnostics, "ProbeInitialAdapterSettleMilliseconds", kInitialAdapterSettleMilliseconds, 32);
        AddNumberProperty(diagnostics, "ProbePostSelfIDSettleMilliseconds", kPostSelfIDSettleMilliseconds, 32);
        AddNumberProperty(diagnostics, "ProbeShortResetAttemptCount", shortResetAttemptCount, 32);
        AddNumberProperty(diagnostics, "ProbeShortResetSuccessAttempt", shortResetSuccessAttempt, 32);
        const char * shortResetUpdateRetNames[kShortResetAttemptCount] = {
            "ProbeShortReset0Reg5UpdateRet",
            "ProbeShortReset1Reg5UpdateRet",
            "ProbeShortReset2Reg5UpdateRet",
            "ProbeShortReset3Reg5UpdateRet",
        };
        const char * shortResetBeforeNames[kShortResetAttemptCount] = {
            "ProbeShortReset0Reg5Before",
            "ProbeShortReset1Reg5Before",
            "ProbeShortReset2Reg5Before",
            "ProbeShortReset3Reg5Before",
        };
        const char * shortResetAfterNames[kShortResetAttemptCount] = {
            "ProbeShortReset0Reg5After",
            "ProbeShortReset1Reg5After",
            "ProbeShortReset2Reg5After",
            "ProbeShortReset3Reg5After",
        };
        const char * shortResetAckLoopNames[kShortResetAttemptCount] = {
            "ProbeShortReset0AckLoops",
            "ProbeShortReset1AckLoops",
            "ProbeShortReset2AckLoops",
            "ProbeShortReset3AckLoops",
        };
        const char * shortResetAckBeforeNames[kShortResetAttemptCount] = {
            "ProbeShortReset0AckEventBefore",
            "ProbeShortReset1AckEventBefore",
            "ProbeShortReset2AckEventBefore",
            "ProbeShortReset3AckEventBefore",
        };
        const char * shortResetAckAfterNames[kShortResetAttemptCount] = {
            "ProbeShortReset0AckEventAfter",
            "ProbeShortReset1AckEventAfter",
            "ProbeShortReset2AckEventAfter",
            "ProbeShortReset3AckEventAfter",
        };
        const char * shortResetAckMaskNames[kShortResetAttemptCount] = {
            "ProbeShortReset0AckMaskAfter",
            "ProbeShortReset1AckMaskAfter",
            "ProbeShortReset2AckMaskAfter",
            "ProbeShortReset3AckMaskAfter",
        };
        const char * shortResetWaitLoopNames[kShortResetAttemptCount] = {
            "ProbeShortReset0WaitLoops",
            "ProbeShortReset1WaitLoops",
            "ProbeShortReset2WaitLoops",
            "ProbeShortReset3WaitLoops",
        };
        const char * shortResetWaitNodeNames[kShortResetAttemptCount] = {
            "ProbeShortReset0WaitNodeID",
            "ProbeShortReset1WaitNodeID",
            "ProbeShortReset2WaitNodeID",
            "ProbeShortReset3WaitNodeID",
        };
        const char * shortResetWaitSelfIDNames[kShortResetAttemptCount] = {
            "ProbeShortReset0WaitSelfIDCount",
            "ProbeShortReset1WaitSelfIDCount",
            "ProbeShortReset2WaitSelfIDCount",
            "ProbeShortReset3WaitSelfIDCount",
        };
        const char * shortResetWaitIntEventNames[kShortResetAttemptCount] = {
            "ProbeShortReset0WaitIntEvent",
            "ProbeShortReset1WaitIntEvent",
            "ProbeShortReset2WaitIntEvent",
            "ProbeShortReset3WaitIntEvent",
        };
        for (size_t i = 0; i < kShortResetAttemptCount; ++i) {
            AddNumberProperty(diagnostics, shortResetUpdateRetNames[i], shortResetUpdateRets[i], 32);
            AddNumberProperty(diagnostics, shortResetBeforeNames[i], shortResetReg5Before[i], 32);
            AddNumberProperty(diagnostics, shortResetAfterNames[i], shortResetReg5After[i], 32);
            AddNumberProperty(diagnostics, shortResetAckLoopNames[i], shortResetAckLoops[i], 32);
            AddNumberProperty(diagnostics, shortResetAckBeforeNames[i], shortResetAckEventBefore[i], 32);
            AddNumberProperty(diagnostics, shortResetAckAfterNames[i], shortResetAckEventAfter[i], 32);
            AddNumberProperty(diagnostics, shortResetAckMaskNames[i], shortResetAckMaskAfter[i], 32);
            AddNumberProperty(diagnostics, shortResetWaitLoopNames[i], shortResetWaitLoops[i], 32);
            AddNumberProperty(diagnostics, shortResetWaitNodeNames[i], shortResetWaitNodeID[i], 32);
            AddNumberProperty(diagnostics, shortResetWaitSelfIDNames[i], shortResetWaitSelfIDCount[i], 32);
            AddNumberProperty(diagnostics, shortResetWaitIntEventNames[i], shortResetWaitIntEvent[i], 32);
        }
        AddNumberProperty(diagnostics, "ProbeWaitLoops", waitLoops, 32);
        AddNumberProperty(diagnostics, "ProbeWaitNodeID", waitNodeID, 32);
        AddNumberProperty(diagnostics, "ProbeWaitSelfIDCount", waitSelfIDCount, 32);
        AddNumberProperty(diagnostics, "ProbeWaitIntEvent", waitIntEvent, 32);
        AddNumberProperty(diagnostics, "ProbeBusResetAckAttempted", busResetAckAttempted, 8);
        AddNumberProperty(diagnostics, "ProbeBusResetAckLoops", busResetAckLoops, 32);
        AddNumberProperty(diagnostics, "ProbeBusResetAckEventBefore", busResetAckEventBefore, 32);
        AddNumberProperty(diagnostics, "ProbeBusResetAckEventAfter", busResetAckEventAfter, 32);
        AddNumberProperty(diagnostics, "ProbeBusResetAckMaskAfter", busResetAckMaskAfter, 32);
        AddNumberProperty(diagnostics, "ProbeLongResetAttempted", longResetAttempted, 8);
        AddNumberProperty(diagnostics, "ProbeLongResetRet", ReturnCodeToProperty(longResetRet), 32);
        AddNumberProperty(diagnostics, "ProbeLongResetReg1Before", longResetReg1Before, 32);
        AddNumberProperty(diagnostics, "ProbeLongResetReg1After", longResetReg1After, 32);
        AddNumberProperty(diagnostics, "ProbeForceClearBeforeLongReset", forceClearBeforeLongReset, 8);
        AddNumberProperty(diagnostics, "ProbeForceClearEventAfter", forceClearEventAfter, 32);
        AddNumberProperty(diagnostics, "ProbeForceClearMaskAfter", forceClearMaskAfter, 32);
        AddNumberProperty(diagnostics, "ProbeLongBusResetAckAttempted", longBusResetAckAttempted, 8);
        AddNumberProperty(diagnostics, "ProbeLongBusResetAckLoops", longBusResetAckLoops, 32);
        AddNumberProperty(diagnostics, "ProbeLongBusResetAckEventBefore", longBusResetAckEventBefore, 32);
        AddNumberProperty(diagnostics, "ProbeLongBusResetAckEventAfter", longBusResetAckEventAfter, 32);
        AddNumberProperty(diagnostics, "ProbeLongBusResetAckMaskAfter", longBusResetAckMaskAfter, 32);
        AddNumberProperty(diagnostics, "ProbeSecondWaitLoops", secondWaitLoops, 32);
        AddNumberProperty(diagnostics, "ProbeSecondWaitNodeID", secondWaitNodeID, 32);
        AddNumberProperty(diagnostics, "ProbeSecondWaitSelfIDCount", secondWaitSelfIDCount, 32);
        AddNumberProperty(diagnostics, "ProbeSecondWaitIntEvent", secondWaitIntEvent, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMQuadletCount", kConfigROMQuadletCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadAttempted", asyncRead.attempted, 8);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReady", asyncRead.ready, 8);
        AddNumberProperty(diagnostics, "ProbeAsyncReadLocalNodeID", asyncRead.localNodeID, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadDestinationID", asyncRead.destinationID, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadOffsetHi", asyncRead.offsetHi, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadOffsetLo", asyncRead.offsetLo, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxCommandPtr", asyncRead.txCommandPtr, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxCommandPtr", asyncRead.rxCommandPtr, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxCommandPtr", asyncRead.reqRxCommandPtr, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadIntEventBefore", asyncRead.intEventBefore, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadIntEventAfter", asyncRead.intEventAfter, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadIntMaskAfter", asyncRead.intMaskAfter, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxContextBefore", asyncRead.txContextBefore, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxContextBefore", asyncRead.rxContextBefore, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxContextBefore", asyncRead.reqRxContextBefore, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxContextControl", asyncRead.txContextControl, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxContextControl", asyncRead.rxContextControl, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxContextControl", asyncRead.reqRxContextControl, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxContextAfterClear", asyncRead.txContextAfterClear, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxContextAfterClear", asyncRead.rxContextAfterClear, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxContextAfterClear", asyncRead.reqRxContextAfterClear, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxContextStopLoops", asyncRead.txContextStopLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxContextStopLoops", asyncRead.rxContextStopLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxContextStopLoops", asyncRead.reqRxContextStopLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxContextFinalStopLoops", asyncRead.txContextFinalStopLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxContextFinalStopLoops", asyncRead.rxContextFinalStopLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxContextFinalStopLoops", asyncRead.reqRxContextFinalStopLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxSyncForDeviceRet", asyncRead.txSyncForDeviceRet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxSyncForDeviceRet", asyncRead.rxSyncForDeviceRet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxSyncForDeviceRet", asyncRead.reqRxSyncForDeviceRet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxSyncForCPURet", asyncRead.txSyncForCPURet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxSyncForCPURet", asyncRead.rxSyncForCPURet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxSyncForCPURet", asyncRead.reqRxSyncForCPURet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxCompleteRet", asyncRead.txCompleteRet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxCompleteRet", asyncRead.rxCompleteRet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxCompleteRet", asyncRead.reqRxCompleteRet, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxDescriptorControl", asyncRead.txDescriptorControl, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxDescriptorReqCount", asyncRead.txDescriptorReqCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxDescriptorResCount", asyncRead.txDescriptorResCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxDescriptorStatus", asyncRead.txDescriptorStatus, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxHeader0", asyncRead.txHeader0, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxHeader1", asyncRead.txHeader1, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxHeader2", asyncRead.txHeader2, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxHeader3", asyncRead.txHeader3, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor0Control", asyncRead.rxDescriptor0Control, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor0DataAddress", asyncRead.rxDescriptor0DataAddress, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor0BranchAddress", asyncRead.rxDescriptor0BranchAddress, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor0ResCount", asyncRead.rxDescriptor0ResCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor0Status", asyncRead.rxDescriptor0Status, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor1Control", asyncRead.rxDescriptor1Control, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor1DataAddress", asyncRead.rxDescriptor1DataAddress, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor1BranchAddress", asyncRead.rxDescriptor1BranchAddress, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor1ResCount", asyncRead.rxDescriptor1ResCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxDescriptor1Status", asyncRead.rxDescriptor1Status, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxBytes0", asyncRead.rxBytes0, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxBytes1", asyncRead.rxBytes1, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadHeader0", asyncRead.rxHeader0, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadHeader1", asyncRead.rxHeader1, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadHeader2", asyncRead.rxHeader2, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadHeader3", asyncRead.rxHeader3, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadResponseTCode", asyncRead.responseTCode, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadResponseTLabel", asyncRead.responseTLabel, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadResponseSource", asyncRead.responseSource, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadResponseRCode", asyncRead.responseRCode, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadResponseData", asyncRead.responseData, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadConfiguredAttempts", asyncRead.configuredAttempts, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadCompletedAttempts", asyncRead.completedAttempts, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadLastAttempt", asyncRead.lastAttempt, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadResponseAttempt", asyncRead.responseAttempt, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadAckPendingAttempts", asyncRead.ackPendingAttempts, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadWaitLoopsPerAttempt", asyncRead.waitLoopsPerAttempt, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRetrySettleMilliseconds", asyncRead.retrySettleMilliseconds, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMProbeReadCount", asyncRead.configROMReadCount, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMProbeDetailedCount", kConfigROMProbeDetailedCount, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMProbeSuccessCount", asyncRead.configROMReadSuccessCount, 32);
        AddNumberProperty(diagnostics, "ProbeConfigROMProbeLastIndex", asyncRead.configROMLastIndex, 32);
        const char * configROMDataNames[kConfigROMProbeReadCount] = {
            "ProbeConfigROMProbeData0",
            "ProbeConfigROMProbeData1",
            "ProbeConfigROMProbeData2",
            "ProbeConfigROMProbeData3",
            "ProbeConfigROMProbeData4",
            "ProbeConfigROMProbeData5",
            "ProbeConfigROMProbeData6",
            "ProbeConfigROMProbeData7",
            "ProbeConfigROMProbeData8",
            "ProbeConfigROMProbeData9",
            "ProbeConfigROMProbeData10",
            "ProbeConfigROMProbeData11",
            "ProbeConfigROMProbeData12",
            "ProbeConfigROMProbeData13",
            "ProbeConfigROMProbeData14",
            "ProbeConfigROMProbeData15",
            "ProbeConfigROMProbeData16",
            "ProbeConfigROMProbeData17",
            "ProbeConfigROMProbeData18",
            "ProbeConfigROMProbeData19",
            "ProbeConfigROMProbeData20",
            "ProbeConfigROMProbeData21",
            "ProbeConfigROMProbeData22",
            "ProbeConfigROMProbeData23",
            "ProbeConfigROMProbeData24",
            "ProbeConfigROMProbeData25",
            "ProbeConfigROMProbeData26",
            "ProbeConfigROMProbeData27",
            "ProbeConfigROMProbeData28",
            "ProbeConfigROMProbeData29",
            "ProbeConfigROMProbeData30",
            "ProbeConfigROMProbeData31",
        };
        const char * configROMRCodeNames[kConfigROMProbeDetailedCount] = {
            "ProbeConfigROMProbeRCode0",
            "ProbeConfigROMProbeRCode1",
            "ProbeConfigROMProbeRCode2",
            "ProbeConfigROMProbeRCode3",
            "ProbeConfigROMProbeRCode4",
            "ProbeConfigROMProbeRCode5",
            "ProbeConfigROMProbeRCode6",
            "ProbeConfigROMProbeRCode7",
            "ProbeConfigROMProbeRCode8",
            "ProbeConfigROMProbeRCode9",
            "ProbeConfigROMProbeRCode10",
            "ProbeConfigROMProbeRCode11",
            "ProbeConfigROMProbeRCode12",
            "ProbeConfigROMProbeRCode13",
            "ProbeConfigROMProbeRCode14",
            "ProbeConfigROMProbeRCode15",
        };
        const char * configROMTCodeNames[kConfigROMProbeDetailedCount] = {
            "ProbeConfigROMProbeTCode0",
            "ProbeConfigROMProbeTCode1",
            "ProbeConfigROMProbeTCode2",
            "ProbeConfigROMProbeTCode3",
            "ProbeConfigROMProbeTCode4",
            "ProbeConfigROMProbeTCode5",
            "ProbeConfigROMProbeTCode6",
            "ProbeConfigROMProbeTCode7",
            "ProbeConfigROMProbeTCode8",
            "ProbeConfigROMProbeTCode9",
            "ProbeConfigROMProbeTCode10",
            "ProbeConfigROMProbeTCode11",
            "ProbeConfigROMProbeTCode12",
            "ProbeConfigROMProbeTCode13",
            "ProbeConfigROMProbeTCode14",
            "ProbeConfigROMProbeTCode15",
        };
        const char * configROMTLabelNames[kConfigROMProbeDetailedCount] = {
            "ProbeConfigROMProbeTLabel0",
            "ProbeConfigROMProbeTLabel1",
            "ProbeConfigROMProbeTLabel2",
            "ProbeConfigROMProbeTLabel3",
            "ProbeConfigROMProbeTLabel4",
            "ProbeConfigROMProbeTLabel5",
            "ProbeConfigROMProbeTLabel6",
            "ProbeConfigROMProbeTLabel7",
            "ProbeConfigROMProbeTLabel8",
            "ProbeConfigROMProbeTLabel9",
            "ProbeConfigROMProbeTLabel10",
            "ProbeConfigROMProbeTLabel11",
            "ProbeConfigROMProbeTLabel12",
            "ProbeConfigROMProbeTLabel13",
            "ProbeConfigROMProbeTLabel14",
            "ProbeConfigROMProbeTLabel15",
        };
        const char * configROMRxBytesNames[kConfigROMProbeDetailedCount] = {
            "ProbeConfigROMProbeRxBytes0",
            "ProbeConfigROMProbeRxBytes1",
            "ProbeConfigROMProbeRxBytes2",
            "ProbeConfigROMProbeRxBytes3",
            "ProbeConfigROMProbeRxBytes4",
            "ProbeConfigROMProbeRxBytes5",
            "ProbeConfigROMProbeRxBytes6",
            "ProbeConfigROMProbeRxBytes7",
            "ProbeConfigROMProbeRxBytes8",
            "ProbeConfigROMProbeRxBytes9",
            "ProbeConfigROMProbeRxBytes10",
            "ProbeConfigROMProbeRxBytes11",
            "ProbeConfigROMProbeRxBytes12",
            "ProbeConfigROMProbeRxBytes13",
            "ProbeConfigROMProbeRxBytes14",
            "ProbeConfigROMProbeRxBytes15",
        };
        const char * configROMTxStatusNames[kConfigROMProbeDetailedCount] = {
            "ProbeConfigROMProbeTxStatus0",
            "ProbeConfigROMProbeTxStatus1",
            "ProbeConfigROMProbeTxStatus2",
            "ProbeConfigROMProbeTxStatus3",
            "ProbeConfigROMProbeTxStatus4",
            "ProbeConfigROMProbeTxStatus5",
            "ProbeConfigROMProbeTxStatus6",
            "ProbeConfigROMProbeTxStatus7",
            "ProbeConfigROMProbeTxStatus8",
            "ProbeConfigROMProbeTxStatus9",
            "ProbeConfigROMProbeTxStatus10",
            "ProbeConfigROMProbeTxStatus11",
            "ProbeConfigROMProbeTxStatus12",
            "ProbeConfigROMProbeTxStatus13",
            "ProbeConfigROMProbeTxStatus14",
            "ProbeConfigROMProbeTxStatus15",
        };
        for (size_t i = 0; i < kConfigROMProbeReadCount; ++i) {
            AddNumberProperty(diagnostics, configROMDataNames[i], asyncRead.configROMData[i], 32);
        }
        for (size_t i = 0; i < kConfigROMProbeDetailedCount; ++i) {
            AddNumberProperty(diagnostics, configROMRCodeNames[i], asyncRead.configROMRCode[i], 32);
            AddNumberProperty(diagnostics, configROMTCodeNames[i], asyncRead.configROMTCode[i], 32);
            AddNumberProperty(diagnostics, configROMTLabelNames[i], asyncRead.configROMTLabel[i], 32);
            AddNumberProperty(diagnostics, configROMRxBytesNames[i], asyncRead.configROMRxBytes[i], 32);
            AddNumberProperty(diagnostics, configROMTxStatusNames[i], asyncRead.configROMTxStatus[i], 32);
        }
        AddNumberProperty(diagnostics, "ProbeDigiRegisterReadCount", asyncRead.digiRegisterReadCount, 32);
        AddNumberProperty(diagnostics, "ProbeDigiRegisterSuccessCount", asyncRead.digiRegisterReadSuccessCount, 32);
        AddNumberProperty(diagnostics, "ProbeDigiRegisterLastIndex", asyncRead.digiRegisterLastIndex, 32);
        const char * digiRegisterOffsetNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateOffset",
            "ProbeDigiRegisterStreamingSetOffset",
            "ProbeDigiRegisterMessageAddressOffset",
            "ProbeDigiRegisterIsocChannelsOffset",
            "ProbeDigiRegisterLocalRateOffset",
            "ProbeDigiRegisterExternalRateOffset",
            "ProbeDigiRegisterClockSourceOffset",
            "ProbeDigiRegisterOpticalModeOffset",
            "ProbeDigiRegisterMonitorEnableOffset",
            "ProbeDigiRegisterExternalDetectOffset",
        };
        const char * digiRegisterDataNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateData",
            "ProbeDigiRegisterStreamingSetData",
            "ProbeDigiRegisterMessageAddressData",
            "ProbeDigiRegisterIsocChannelsData",
            "ProbeDigiRegisterLocalRateData",
            "ProbeDigiRegisterExternalRateData",
            "ProbeDigiRegisterClockSourceData",
            "ProbeDigiRegisterOpticalModeData",
            "ProbeDigiRegisterMonitorEnableData",
            "ProbeDigiRegisterExternalDetectData",
        };
        const char * digiRegisterRCodeNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateRCode",
            "ProbeDigiRegisterStreamingSetRCode",
            "ProbeDigiRegisterMessageAddressRCode",
            "ProbeDigiRegisterIsocChannelsRCode",
            "ProbeDigiRegisterLocalRateRCode",
            "ProbeDigiRegisterExternalRateRCode",
            "ProbeDigiRegisterClockSourceRCode",
            "ProbeDigiRegisterOpticalModeRCode",
            "ProbeDigiRegisterMonitorEnableRCode",
            "ProbeDigiRegisterExternalDetectRCode",
        };
        const char * digiRegisterTCodeNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateTCode",
            "ProbeDigiRegisterStreamingSetTCode",
            "ProbeDigiRegisterMessageAddressTCode",
            "ProbeDigiRegisterIsocChannelsTCode",
            "ProbeDigiRegisterLocalRateTCode",
            "ProbeDigiRegisterExternalRateTCode",
            "ProbeDigiRegisterClockSourceTCode",
            "ProbeDigiRegisterOpticalModeTCode",
            "ProbeDigiRegisterMonitorEnableTCode",
            "ProbeDigiRegisterExternalDetectTCode",
        };
        const char * digiRegisterTLabelNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateTLabel",
            "ProbeDigiRegisterStreamingSetTLabel",
            "ProbeDigiRegisterMessageAddressTLabel",
            "ProbeDigiRegisterIsocChannelsTLabel",
            "ProbeDigiRegisterLocalRateTLabel",
            "ProbeDigiRegisterExternalRateTLabel",
            "ProbeDigiRegisterClockSourceTLabel",
            "ProbeDigiRegisterOpticalModeTLabel",
            "ProbeDigiRegisterMonitorEnableTLabel",
            "ProbeDigiRegisterExternalDetectTLabel",
        };
        const char * digiRegisterRxBytesNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateRxBytes",
            "ProbeDigiRegisterStreamingSetRxBytes",
            "ProbeDigiRegisterMessageAddressRxBytes",
            "ProbeDigiRegisterIsocChannelsRxBytes",
            "ProbeDigiRegisterLocalRateRxBytes",
            "ProbeDigiRegisterExternalRateRxBytes",
            "ProbeDigiRegisterClockSourceRxBytes",
            "ProbeDigiRegisterOpticalModeRxBytes",
            "ProbeDigiRegisterMonitorEnableRxBytes",
            "ProbeDigiRegisterExternalDetectRxBytes",
        };
        const char * digiRegisterTxStatusNames[kDigi00xRegisterProbeCount] = {
            "ProbeDigiRegisterStreamingStateTxStatus",
            "ProbeDigiRegisterStreamingSetTxStatus",
            "ProbeDigiRegisterMessageAddressTxStatus",
            "ProbeDigiRegisterIsocChannelsTxStatus",
            "ProbeDigiRegisterLocalRateTxStatus",
            "ProbeDigiRegisterExternalRateTxStatus",
            "ProbeDigiRegisterClockSourceTxStatus",
            "ProbeDigiRegisterOpticalModeTxStatus",
            "ProbeDigiRegisterMonitorEnableTxStatus",
            "ProbeDigiRegisterExternalDetectTxStatus",
        };
        for (size_t i = 0; i < kDigi00xRegisterProbeCount; ++i) {
            AddNumberProperty(diagnostics, digiRegisterOffsetNames[i], asyncRead.digiRegisterOffsetLo[i], 32);
            AddNumberProperty(diagnostics, digiRegisterDataNames[i], asyncRead.digiRegisterData[i], 32);
            AddNumberProperty(diagnostics, digiRegisterRCodeNames[i], asyncRead.digiRegisterRCode[i], 32);
            AddNumberProperty(diagnostics, digiRegisterTCodeNames[i], asyncRead.digiRegisterTCode[i], 32);
            AddNumberProperty(diagnostics, digiRegisterTLabelNames[i], asyncRead.digiRegisterTLabel[i], 32);
            AddNumberProperty(diagnostics, digiRegisterRxBytesNames[i], asyncRead.digiRegisterRxBytes[i], 32);
            AddNumberProperty(diagnostics, digiRegisterTxStatusNames[i], asyncRead.digiRegisterTxStatus[i], 32);
        }
        AddNumberProperty(diagnostics, "ProbeDigiWritePlanEnabled", asyncRead.digiWritePlanEnabled, 32);
        AddNumberProperty(diagnostics, "ProbeDigiWritePlanExecuted", asyncRead.digiWritePlanExecuted, 32);
        AddNumberProperty(diagnostics, "ProbeDigiWritePlanCount", asyncRead.digiWritePlanCount, 32);
        const char * digiWritePlanOffsetNames[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetOffset",
            "ProbeDigiWritePlanFinishIsocChannelsOffset",
            "ProbeDigiWritePlanBeginIsocChannelsOffset",
            "ProbeDigiWritePlanBeginStreamingSet2Offset",
            "ProbeDigiWritePlanBeginStreamingSet1Offset",
        };
        const char * digiWritePlanBusValueNames[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetBusValue",
            "ProbeDigiWritePlanFinishIsocChannelsBusValue",
            "ProbeDigiWritePlanBeginIsocChannelsBusValue",
            "ProbeDigiWritePlanBeginStreamingSet2BusValue",
            "ProbeDigiWritePlanBeginStreamingSet1BusValue",
        };
        const char * digiWritePlanTxDataNames[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetTxData",
            "ProbeDigiWritePlanFinishIsocChannelsTxData",
            "ProbeDigiWritePlanBeginIsocChannelsTxData",
            "ProbeDigiWritePlanBeginStreamingSet2TxData",
            "ProbeDigiWritePlanBeginStreamingSet1TxData",
        };
        const char * digiWritePlanReqCountNames[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetReqCount",
            "ProbeDigiWritePlanFinishIsocChannelsReqCount",
            "ProbeDigiWritePlanBeginIsocChannelsReqCount",
            "ProbeDigiWritePlanBeginStreamingSet2ReqCount",
            "ProbeDigiWritePlanBeginStreamingSet1ReqCount",
        };
        const char * digiWritePlanHeader0Names[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetHeader0",
            "ProbeDigiWritePlanFinishIsocChannelsHeader0",
            "ProbeDigiWritePlanBeginIsocChannelsHeader0",
            "ProbeDigiWritePlanBeginStreamingSet2Header0",
            "ProbeDigiWritePlanBeginStreamingSet1Header0",
        };
        const char * digiWritePlanHeader1Names[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetHeader1",
            "ProbeDigiWritePlanFinishIsocChannelsHeader1",
            "ProbeDigiWritePlanBeginIsocChannelsHeader1",
            "ProbeDigiWritePlanBeginStreamingSet2Header1",
            "ProbeDigiWritePlanBeginStreamingSet1Header1",
        };
        const char * digiWritePlanHeader2Names[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetHeader2",
            "ProbeDigiWritePlanFinishIsocChannelsHeader2",
            "ProbeDigiWritePlanBeginIsocChannelsHeader2",
            "ProbeDigiWritePlanBeginStreamingSet2Header2",
            "ProbeDigiWritePlanBeginStreamingSet1Header2",
        };
        const char * digiWritePlanHeader3Names[kDigi00xWritePlanCount] = {
            "ProbeDigiWritePlanFinishStreamingSetHeader3",
            "ProbeDigiWritePlanFinishIsocChannelsHeader3",
            "ProbeDigiWritePlanBeginIsocChannelsHeader3",
            "ProbeDigiWritePlanBeginStreamingSet2Header3",
            "ProbeDigiWritePlanBeginStreamingSet1Header3",
        };
        for (size_t i = 0; i < kDigi00xWritePlanCount; ++i) {
            AddNumberProperty(diagnostics, digiWritePlanOffsetNames[i], asyncRead.digiWritePlanOffsetLo[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanBusValueNames[i], asyncRead.digiWritePlanBusValue[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanTxDataNames[i], asyncRead.digiWritePlanTxData[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanReqCountNames[i], asyncRead.digiWritePlanReqCount[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanHeader0Names[i], asyncRead.digiWritePlanHeader0[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanHeader1Names[i], asyncRead.digiWritePlanHeader1[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanHeader2Names[i], asyncRead.digiWritePlanHeader2[i], 32);
            AddNumberProperty(diagnostics, digiWritePlanHeader3Names[i], asyncRead.digiWritePlanHeader3[i], 32);
        }
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteEnabled", asyncRead.digiNoopWriteEnabled, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteAttempted", asyncRead.digiNoopWriteAttempted, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteExecuted", asyncRead.digiNoopWriteExecuted, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteSuccess", asyncRead.digiNoopWriteSuccess, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteCompletedAttempts", asyncRead.digiNoopWriteCompletedAttempts, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteWaitLoops", asyncRead.digiNoopWriteWaitLoops, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteOffset", asyncRead.digiNoopWriteOffsetLo, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteBusValue", asyncRead.digiNoopWriteBusValue, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteTxData", asyncRead.digiNoopWriteTxData, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteReqCount", asyncRead.digiNoopWriteReqCount, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteHeader0", asyncRead.digiNoopWriteHeader0, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteHeader1", asyncRead.digiNoopWriteHeader1, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteHeader2", asyncRead.digiNoopWriteHeader2, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteHeader3", asyncRead.digiNoopWriteHeader3, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteTxStatus", asyncRead.digiNoopWriteTxStatus, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteRxBytes", asyncRead.digiNoopWriteRxBytes, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteResponseTCode", asyncRead.digiNoopWriteResponseTCode, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteResponseTLabel", asyncRead.digiNoopWriteResponseTLabel, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteResponseSource", asyncRead.digiNoopWriteResponseSource, 32);
        AddNumberProperty(diagnostics, "ProbeDigiNoopWriteResponseRCode", asyncRead.digiNoopWriteResponseRCode, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteEnabled", asyncRead.digiStateWriteEnabled, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteAttempted", asyncRead.digiStateWriteAttempted, 32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateWritePrereqNoopSuccess",
                          asyncRead.digiStateWritePrereqNoopSuccess,
                          32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteExecuted", asyncRead.digiStateWriteExecuted, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteSuccess", asyncRead.digiStateWriteSuccess, 32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateWriteCompletedAttempts",
                          asyncRead.digiStateWriteCompletedAttempts,
                          32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteWaitLoops", asyncRead.digiStateWriteWaitLoops, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteOffset", asyncRead.digiStateWriteOffsetLo, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteBusValue", asyncRead.digiStateWriteBusValue, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteTxData", asyncRead.digiStateWriteTxData, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteReqCount", asyncRead.digiStateWriteReqCount, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteHeader0", asyncRead.digiStateWriteHeader0, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteHeader1", asyncRead.digiStateWriteHeader1, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteHeader2", asyncRead.digiStateWriteHeader2, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteHeader3", asyncRead.digiStateWriteHeader3, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteTxStatus", asyncRead.digiStateWriteTxStatus, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateWriteRxBytes", asyncRead.digiStateWriteRxBytes, 32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateWriteResponseTCode",
                          asyncRead.digiStateWriteResponseTCode,
                          32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateWriteResponseTLabel",
                          asyncRead.digiStateWriteResponseTLabel,
                          32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateWriteResponseSource",
                          asyncRead.digiStateWriteResponseSource,
                          32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateWriteResponseRCode",
                          asyncRead.digiStateWriteResponseRCode,
                          32);
        AddNumberProperty(diagnostics, "ProbeDigiStateSeqEnabled", asyncRead.digiStateSequenceEnabled, 32);
        AddNumberProperty(diagnostics, "ProbeDigiStateSeqAttempted", asyncRead.digiStateSequenceAttempted, 32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateSeqPrereqStateWriteSuccess",
                          asyncRead.digiStateSequencePrereqStateWriteSuccess,
                          32);
        AddNumberProperty(diagnostics, "ProbeDigiStateSeqStepCount", asyncRead.digiStateSequenceStepCount, 32);
        AddNumberProperty(diagnostics,
                          "ProbeDigiStateSeqCompletedSteps",
                          asyncRead.digiStateSequenceCompletedSteps,
                          32);
        AddNumberProperty(diagnostics, "ProbeDigiStateSeqSuccess", asyncRead.digiStateSequenceSuccess, 32);
        const char * digiStateSeqOpNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0Op",
            "ProbeDigiStateSeq1Op",
            "ProbeDigiStateSeq2Op",
            "ProbeDigiStateSeq3Op",
            "ProbeDigiStateSeq4Op",
            "ProbeDigiStateSeq5Op",
            "ProbeDigiStateSeq6Op",
            "ProbeDigiStateSeq7Op",
        };
        const char * digiStateSeqOffsetNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0Offset",
            "ProbeDigiStateSeq1Offset",
            "ProbeDigiStateSeq2Offset",
            "ProbeDigiStateSeq3Offset",
            "ProbeDigiStateSeq4Offset",
            "ProbeDigiStateSeq5Offset",
            "ProbeDigiStateSeq6Offset",
            "ProbeDigiStateSeq7Offset",
        };
        const char * digiStateSeqBusValueNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0BusValue",
            "ProbeDigiStateSeq1BusValue",
            "ProbeDigiStateSeq2BusValue",
            "ProbeDigiStateSeq3BusValue",
            "ProbeDigiStateSeq4BusValue",
            "ProbeDigiStateSeq5BusValue",
            "ProbeDigiStateSeq6BusValue",
            "ProbeDigiStateSeq7BusValue",
        };
        const char * digiStateSeqTxDataNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0TxData",
            "ProbeDigiStateSeq1TxData",
            "ProbeDigiStateSeq2TxData",
            "ProbeDigiStateSeq3TxData",
            "ProbeDigiStateSeq4TxData",
            "ProbeDigiStateSeq5TxData",
            "ProbeDigiStateSeq6TxData",
            "ProbeDigiStateSeq7TxData",
        };
        const char * digiStateSeqReqCountNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0ReqCount",
            "ProbeDigiStateSeq1ReqCount",
            "ProbeDigiStateSeq2ReqCount",
            "ProbeDigiStateSeq3ReqCount",
            "ProbeDigiStateSeq4ReqCount",
            "ProbeDigiStateSeq5ReqCount",
            "ProbeDigiStateSeq6ReqCount",
            "ProbeDigiStateSeq7ReqCount",
        };
        const char * digiStateSeqCompletedAttemptNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0CompletedAttempts",
            "ProbeDigiStateSeq1CompletedAttempts",
            "ProbeDigiStateSeq2CompletedAttempts",
            "ProbeDigiStateSeq3CompletedAttempts",
            "ProbeDigiStateSeq4CompletedAttempts",
            "ProbeDigiStateSeq5CompletedAttempts",
            "ProbeDigiStateSeq6CompletedAttempts",
            "ProbeDigiStateSeq7CompletedAttempts",
        };
        const char * digiStateSeqWaitLoopNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0WaitLoops",
            "ProbeDigiStateSeq1WaitLoops",
            "ProbeDigiStateSeq2WaitLoops",
            "ProbeDigiStateSeq3WaitLoops",
            "ProbeDigiStateSeq4WaitLoops",
            "ProbeDigiStateSeq5WaitLoops",
            "ProbeDigiStateSeq6WaitLoops",
            "ProbeDigiStateSeq7WaitLoops",
        };
        const char * digiStateSeqSuccessNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0Success",
            "ProbeDigiStateSeq1Success",
            "ProbeDigiStateSeq2Success",
            "ProbeDigiStateSeq3Success",
            "ProbeDigiStateSeq4Success",
            "ProbeDigiStateSeq5Success",
            "ProbeDigiStateSeq6Success",
            "ProbeDigiStateSeq7Success",
        };
        const char * digiStateSeqTxStatusNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0TxStatus",
            "ProbeDigiStateSeq1TxStatus",
            "ProbeDigiStateSeq2TxStatus",
            "ProbeDigiStateSeq3TxStatus",
            "ProbeDigiStateSeq4TxStatus",
            "ProbeDigiStateSeq5TxStatus",
            "ProbeDigiStateSeq6TxStatus",
            "ProbeDigiStateSeq7TxStatus",
        };
        const char * digiStateSeqRxBytesNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0RxBytes",
            "ProbeDigiStateSeq1RxBytes",
            "ProbeDigiStateSeq2RxBytes",
            "ProbeDigiStateSeq3RxBytes",
            "ProbeDigiStateSeq4RxBytes",
            "ProbeDigiStateSeq5RxBytes",
            "ProbeDigiStateSeq6RxBytes",
            "ProbeDigiStateSeq7RxBytes",
        };
        const char * digiStateSeqResponseTCodeNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0ResponseTCode",
            "ProbeDigiStateSeq1ResponseTCode",
            "ProbeDigiStateSeq2ResponseTCode",
            "ProbeDigiStateSeq3ResponseTCode",
            "ProbeDigiStateSeq4ResponseTCode",
            "ProbeDigiStateSeq5ResponseTCode",
            "ProbeDigiStateSeq6ResponseTCode",
            "ProbeDigiStateSeq7ResponseTCode",
        };
        const char * digiStateSeqResponseTLabelNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0ResponseTLabel",
            "ProbeDigiStateSeq1ResponseTLabel",
            "ProbeDigiStateSeq2ResponseTLabel",
            "ProbeDigiStateSeq3ResponseTLabel",
            "ProbeDigiStateSeq4ResponseTLabel",
            "ProbeDigiStateSeq5ResponseTLabel",
            "ProbeDigiStateSeq6ResponseTLabel",
            "ProbeDigiStateSeq7ResponseTLabel",
        };
        const char * digiStateSeqResponseSourceNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0ResponseSource",
            "ProbeDigiStateSeq1ResponseSource",
            "ProbeDigiStateSeq2ResponseSource",
            "ProbeDigiStateSeq3ResponseSource",
            "ProbeDigiStateSeq4ResponseSource",
            "ProbeDigiStateSeq5ResponseSource",
            "ProbeDigiStateSeq6ResponseSource",
            "ProbeDigiStateSeq7ResponseSource",
        };
        const char * digiStateSeqResponseRCodeNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0ResponseRCode",
            "ProbeDigiStateSeq1ResponseRCode",
            "ProbeDigiStateSeq2ResponseRCode",
            "ProbeDigiStateSeq3ResponseRCode",
            "ProbeDigiStateSeq4ResponseRCode",
            "ProbeDigiStateSeq5ResponseRCode",
            "ProbeDigiStateSeq6ResponseRCode",
            "ProbeDigiStateSeq7ResponseRCode",
        };
        const char * digiStateSeqResponseDataNames[kDigi00xStateSequenceStepCount] = {
            "ProbeDigiStateSeq0ResponseData",
            "ProbeDigiStateSeq1ResponseData",
            "ProbeDigiStateSeq2ResponseData",
            "ProbeDigiStateSeq3ResponseData",
            "ProbeDigiStateSeq4ResponseData",
            "ProbeDigiStateSeq5ResponseData",
            "ProbeDigiStateSeq6ResponseData",
            "ProbeDigiStateSeq7ResponseData",
        };
        for (size_t i = 0; i < kDigi00xStateSequenceStepCount; ++i) {
            AddNumberProperty(diagnostics, digiStateSeqOpNames[i], asyncRead.digiStateSequenceOp[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqOffsetNames[i], asyncRead.digiStateSequenceOffsetLo[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqBusValueNames[i], asyncRead.digiStateSequenceBusValue[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqTxDataNames[i], asyncRead.digiStateSequenceTxData[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqReqCountNames[i], asyncRead.digiStateSequenceReqCount[i], 32);
            AddNumberProperty(diagnostics,
                              digiStateSeqCompletedAttemptNames[i],
                              asyncRead.digiStateSequenceCompletedAttempts[i],
                              32);
            AddNumberProperty(diagnostics, digiStateSeqWaitLoopNames[i], asyncRead.digiStateSequenceWaitLoops[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqSuccessNames[i], asyncRead.digiStateSequenceSuccessByStep[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqTxStatusNames[i], asyncRead.digiStateSequenceTxStatus[i], 32);
            AddNumberProperty(diagnostics, digiStateSeqRxBytesNames[i], asyncRead.digiStateSequenceRxBytes[i], 32);
            AddNumberProperty(diagnostics,
                              digiStateSeqResponseTCodeNames[i],
                              asyncRead.digiStateSequenceResponseTCode[i],
                              32);
            AddNumberProperty(diagnostics,
                              digiStateSeqResponseTLabelNames[i],
                              asyncRead.digiStateSequenceResponseTLabel[i],
                              32);
            AddNumberProperty(diagnostics,
                              digiStateSeqResponseSourceNames[i],
                              asyncRead.digiStateSequenceResponseSource[i],
                              32);
            AddNumberProperty(diagnostics,
                              digiStateSeqResponseRCodeNames[i],
                              asyncRead.digiStateSequenceResponseRCode[i],
                              32);
            AddNumberProperty(diagnostics,
                              digiStateSeqResponseDataNames[i],
                              asyncRead.digiStateSequenceResponseData[i],
                              32);
        }
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxDescriptor0ResCount", asyncRead.reqRxDescriptor0ResCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxDescriptor0Status", asyncRead.reqRxDescriptor0Status, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxDescriptor1ResCount", asyncRead.reqRxDescriptor1ResCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxDescriptor1Status", asyncRead.reqRxDescriptor1Status, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxBytes0", asyncRead.reqRxBytes0, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxBytes1", asyncRead.reqRxBytes1, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxHeader0", asyncRead.reqRxHeader0, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxHeader1", asyncRead.reqRxHeader1, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxHeader2", asyncRead.reqRxHeader2, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxHeader3", asyncRead.reqRxHeader3, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadWaitLoops", asyncRead.waitLoops, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxBufferResult", ReturnCodeToProperty(gAsyncTxBuffer.result), 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxBufferSegmentCount", gAsyncTxBuffer.segmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxBufferDMAAddress", gAsyncTxBuffer.dmaSegment.address, 64);
        AddNumberProperty(diagnostics, "ProbeAsyncReadTxBufferDMALength", gAsyncTxBuffer.dmaSegment.length, 64);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxBufferResult", ReturnCodeToProperty(gAsyncRxBuffer.result), 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxBufferSegmentCount", gAsyncRxBuffer.segmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxBufferDMAAddress", gAsyncRxBuffer.dmaSegment.address, 64);
        AddNumberProperty(diagnostics, "ProbeAsyncReadRxBufferDMALength", gAsyncRxBuffer.dmaSegment.length, 64);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxBufferResult", ReturnCodeToProperty(gAsyncReqRxBuffer.result), 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxBufferSegmentCount", gAsyncReqRxBuffer.segmentCount, 32);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxBufferDMAAddress", gAsyncReqRxBuffer.dmaSegment.address, 64);
        AddNumberProperty(diagnostics, "ProbeAsyncReadReqRxBufferDMALength", gAsyncReqRxBuffer.dmaSegment.length, 64);
        AddIsoTestDiagnostics(diagnostics, &isoTest, &gIsoTestBuffer);
        AddDigiDuplexDiagnostics(diagnostics, &digiDuplex, &gDigiDuplexBuffer);
        AddDMADiagnostics(diagnostics, "SelfID", &gSelfIDBuffer);
        AddDMADiagnostics(diagnostics, "ConfigROM", &gConfigROMBuffer);
        const char * selfIDWordNames[kSelfIDWordCount] = {
            "ProbeSelfIDWord0",
            "ProbeSelfIDWord1",
            "ProbeSelfIDWord2",
            "ProbeSelfIDWord3",
            "ProbeSelfIDWord4",
            "ProbeSelfIDWord5",
            "ProbeSelfIDWord6",
            "ProbeSelfIDWord7",
            "ProbeSelfIDWord8",
            "ProbeSelfIDWord9",
            "ProbeSelfIDWord10",
            "ProbeSelfIDWord11",
            "ProbeSelfIDWord12",
            "ProbeSelfIDWord13",
            "ProbeSelfIDWord14",
            "ProbeSelfIDWord15",
        };
        for (size_t i = 0; i < kSelfIDWordCount; ++i) {
            AddNumberProperty(diagnostics, selfIDWordNames[i], selfIDWords[i], 32);
        }
        const char * phyRegisterNames[kPhyRegisterCount] = {
            "ProbePhyReg0",
            "ProbePhyReg1",
            "ProbePhyReg2",
            "ProbePhyReg3",
            "ProbePhyReg4",
            "ProbePhyReg5",
            "ProbePhyReg6",
            "ProbePhyReg7",
            "ProbePhyReg8",
            "ProbePhyReg9",
            "ProbePhyReg10",
            "ProbePhyReg11",
            "ProbePhyReg12",
            "ProbePhyReg13",
            "ProbePhyReg14",
            "ProbePhyReg15",
        };
        const char * phyRegisterRetNames[kPhyRegisterCount] = {
            "ProbePhyReg0ReadRet",
            "ProbePhyReg1ReadRet",
            "ProbePhyReg2ReadRet",
            "ProbePhyReg3ReadRet",
            "ProbePhyReg4ReadRet",
            "ProbePhyReg5ReadRet",
            "ProbePhyReg6ReadRet",
            "ProbePhyReg7ReadRet",
            "ProbePhyReg8ReadRet",
            "ProbePhyReg9ReadRet",
            "ProbePhyReg10ReadRet",
            "ProbePhyReg11ReadRet",
            "ProbePhyReg12ReadRet",
            "ProbePhyReg13ReadRet",
            "ProbePhyReg14ReadRet",
            "ProbePhyReg15ReadRet",
        };
        for (size_t i = 0; i < kPhyRegisterCount; ++i) {
            AddNumberProperty(diagnostics, phyRegisterNames[i], phyRegisterValues[i], 32);
            AddNumberProperty(diagnostics, phyRegisterRetNames[i], phyRegisterReadRets[i], 32);
        }
        const char * phyPortRawNames[kPhyPortCount] = {
            "ProbePhyPort0RawStatus",
            "ProbePhyPort1RawStatus",
            "ProbePhyPort2RawStatus",
        };
        const char * phyPortDecodedNames[kPhyPortCount] = {
            "ProbePhyPort0DecodedStatus",
            "ProbePhyPort1DecodedStatus",
            "ProbePhyPort2DecodedStatus",
        };
        const char * phyPortRetNames[kPhyPortCount] = {
            "ProbePhyPort0ReadRet",
            "ProbePhyPort1ReadRet",
            "ProbePhyPort2ReadRet",
        };
        for (size_t i = 0; i < kPhyPortCount; ++i) {
            AddNumberProperty(diagnostics, phyPortRawNames[i], phyPortRawStatus[i], 32);
            AddNumberProperty(diagnostics, phyPortDecodedNames[i], phyPortDecodedStatus[i], 32);
            AddNumberProperty(diagnostics, phyPortRetNames[i], phyPortReadRets[i], 32);
        }
        for (size_t i = 0; i < kOhciRegisterCount; ++i) {
            AddNumberProperty(diagnostics, ohciRegisters[i].propertyName, ohciRegisters[i].value, 32);
        }

        kern_return_t propertyRet = SetProperties(diagnostics, SUPERDISPATCH);
        if (propertyRet != kIOReturnSuccess) {
            os_log(OS_LOG_DEFAULT,
                   "FireWireOHCIProbe: SetProperties(diagnostics) failed: 0x%x",
                   propertyRet);
        }
        diagnostics->release();
    } else {
        os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: failed to allocate diagnostics dictionary");
    }

    os_log(OS_LOG_DEFAULT,
           "FireWireOHCIProbe: matched vendorDevice=0x%08x classRevision=0x%08x BAR0 index=%u size=%llu type=0x%02x OHCI_VERSION=0x%08x",
           vendorDevice,
           classRevision,
           memoryIndex,
           barSize,
           barType,
           ohciRegisters[0].value);

    ret = RegisterService();
    PublishStartStage(20, ReturnCodeToProperty(ret));
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: RegisterService failed: 0x%x", ret);
    }
    return ret;
}

kern_return_t
FireWireOHCIProbeAudioDevice::HandleChangeSampleRate(double in_sample_rate)
{
    gAudioRuntimeSampleRateChangeCount++;
    gAudioRuntimeSampleRateChangeStage = 1;
    gAudioRuntimeSampleRateChangeRestarted = 0;
    gAudioRuntimeSampleRateChangeRet = ReturnCodeToProperty(kIOReturnNotReady);

    uint32_t requestedSampleRate = 0;
    if (!Digi00xSampleRateFromDouble(in_sample_rate, &requestedSampleRate)) {
        gAudioRuntimeRequestedSampleRate = static_cast<uint32_t>(in_sample_rate);
        gAudioRuntimeSampleRateChangeRet = ReturnCodeToProperty(kIOReturnBadArgument);
        PublishAudioRuntimeDiagnostics();
        return kIOReturnBadArgument;
    }

    gAudioRuntimeRequestedSampleRate = requestedSampleRate;
    bool sampleRateChanged = requestedSampleRate != gDigi00xCurrentSampleRate;
    bool liveWasRunning = sampleRateChanged &&
        (gAudioRefreshWorkerRunning != 0 || DigiLiveStreamMayNeedStop());

    if (liveWasRunning) {
        gAudioRuntimeSampleRateChangeStage = 2;
        StopAudioRefreshWorker(true, false);
        if (DigiLiveStreamMayNeedStop()) {
            (void)StopDigiLiveStreamForAudio();
        }
        ResetAudioRingBuffer();
        ResetAudioOutputRingBuffer();
        ResetDigiLiveOutputState();
        ClearAudioInputBuffer();
        ClearAudioOutputBuffer();
    }

    gAudioRuntimeSampleRateChangeStage = 3;
    SetDigi00xRuntimeSampleRate(requestedSampleRate);
    kern_return_t ret =
        IOUserAudioDevice::HandleChangeSampleRate(static_cast<double>(requestedSampleRate));
    if (ret == kIOReturnSuccess && gAudioInputStream) {
        ret = gAudioInputStream->DeviceSampleRateChanged(static_cast<double>(requestedSampleRate));
    }
    if (ret == kIOReturnSuccess && gAudioOutputStream) {
        ret = gAudioOutputStream->DeviceSampleRateChanged(static_cast<double>(requestedSampleRate));
    }

    if (ret == kIOReturnSuccess && liveWasRunning) {
        gAudioRuntimeSampleRateChangeStage = 4;
        kern_return_t liveRet = StartDigiLiveStreamForAudio();
        if (liveRet == kIOReturnSuccess) {
            PrebufferDigiLiveAudio();
            StartAudioRefreshWorker();
            gAudioRuntimeSampleRateChangeRestarted = 1;
        } else {
            ret = liveRet;
        }
    }

    gAudioRuntimeSampleRateChangeStage = 5;
    gAudioRuntimeSampleRateChangeRet = ReturnCodeToProperty(ret);
    os_log(OS_LOG_DEFAULT,
           "FireWireOHCIProbe: sample rate change requested=%u ret=0x%x restarted=%u",
           requestedSampleRate,
           ret,
           gAudioRuntimeSampleRateChangeRestarted);
    PublishAudioRuntimeDiagnostics();
    return ret;
}

kern_return_t
FireWireOHCIProbe::StartDevice(IOUserAudioObjectID in_object_id,
                               IOUserAudioStartStopFlags in_flags)
{
    gAudioStartDeviceCount++;
    gAudioStartDeviceObjectID = in_object_id;
    gAudioStartDeviceStage = 1;
    gAudioStartDeviceRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioZeroTimestampHostTime = mach_absolute_time();
    gAudioStartDeviceStage = 2;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 2 stopping refresh worker");
    StopAudioRefreshWorker(true, false);
    gAudioStartDeviceStage = 3;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 3 stopping stale live stream if present");
    if (DigiLiveStreamMayNeedStop()) {
        (void)StopDigiLiveStreamForAudio();
    }
    gAudioStartDeviceStage = 4;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 4 resetting audio rings");
    ResetAudioRingBuffer();
    ResetAudioOutputRingBuffer();
    ResetDigiLiveOutputState();
    ClearAudioInputBuffer();
    ClearAudioOutputBuffer();
    gAudioStartDeviceStage = 5;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 5 starting live stream");
    kern_return_t liveRet = StartDigiLiveStreamForAudio();
    gAudioStartDeviceStage = 6;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 6 live stream ret=0x%x", liveRet);
    if (liveRet != kIOReturnSuccess) {
        RefreshDigiCaptureForAudio();
    } else {
        PrebufferDigiLiveAudio();
    }
    gAudioStartDeviceStage = 7;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 7 starting refresh worker");
    StartAudioRefreshWorker();
    gAudioStartDeviceStage = 8;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 8 activating audio streams");
    if (gAudioDevice) {
        gAudioDevice->UpdateCurrentZeroTimestamp(0, gAudioZeroTimestampHostTime);
        gAudioStartIOThreadRet =
            ReturnCodeToProperty(gAudioDevice->_StartIOThread(gAudioDevice->GetObjectID(),
                                                              static_cast<double>(gDigi00xCurrentSampleRate),
                                                              kAudioDeviceZeroTimestampPeriod));
    }
    if (gAudioInputStream) {
        gAudioStreamActiveRet = ReturnCodeToProperty(gAudioInputStream->SetStreamIsActive(true));
    }
    if (gAudioOutputStream) {
        gAudioOutputStreamActiveRet =
            ReturnCodeToProperty(gAudioOutputStream->SetStreamIsActive(true));
    }
    gAudioStartDeviceStage = 9;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 9 calling super");
    kern_return_t ret = IOUserAudioDriver::StartDevice(in_object_id, in_flags);
    gAudioStartDeviceRet = ReturnCodeToProperty(ret);
    if (ret == kIOReturnSuccess) {
        gAudioRuntimeDeviceStarted = 1;
    }
    gAudioStartDeviceStage = 10;
    os_log(OS_LOG_DEFAULT, "FireWireOHCIProbe: StartDevice stage 10 done ret=0x%x", ret);
    PublishAudioRuntimeDiagnostics();
    return ret;
}

kern_return_t
FireWireOHCIProbe::StopDevice(IOUserAudioObjectID in_object_id,
                              IOUserAudioStartStopFlags in_flags)
{
    gAudioStopDeviceCount++;
    gAudioStopDeviceObjectID = in_object_id;
    gAudioStopDeviceStage = 1;
    gAudioStopDeviceRet = ReturnCodeToProperty(kIOReturnNotReady);
    gAudioRuntimeDeviceStarted = 0;
    if (gAudioInputStream) {
        gAudioStreamActiveRet = ReturnCodeToProperty(gAudioInputStream->SetStreamIsActive(false));
    }
    if (gAudioOutputStream) {
        gAudioOutputStreamActiveRet =
            ReturnCodeToProperty(gAudioOutputStream->SetStreamIsActive(false));
    }
    gAudioStopDeviceStage = 2;
    kern_return_t ret = IOUserAudioDriver::StopDevice(in_object_id, in_flags);
    gAudioStopDeviceRet = ReturnCodeToProperty(ret);
    gAudioStopDeviceStage = 3;
    StopAudioRefreshWorker(true);
    gAudioStopDeviceStage = 4;
    gAudioOutputRingReadFrame = gAudioOutputRingWriteFrame;
    gAudioOutputRingPrebuffered = 0;
    UpdateAudioOutputRingFill();
    gAudioStopDeviceStage = 5;
    if (gDigiLiveRunning != 0) {
        for (uint32_t i = 0; i < kDigiLiveOutputStopSilencePushCount; ++i) {
            (void)PushAudioOutputToDigiLiveTransmit();
        }
    }
    gAudioStopDeviceStage = 6;
    if (DigiLiveStreamMayNeedStop()) {
        (void)StopDigiLiveStreamForAudio();
    }
    gAudioStopDeviceStage = 7;
    PublishAudioRuntimeDiagnostics();
    return ret;
}

void
FireWireOHCIProbe::InterruptOccurred_Impl(FireWireOHCIProbe_InterruptOccurred_Args)
{
    (void)action;
    gOHCIInterruptCount++;
    gOHCIInterruptLastCount = count;
    gOHCIInterruptLastTime = time;

    if (gPCIDevice == nullptr || gPCIMemoryIndex == 0xff) {
        gOHCIInterruptOtherCount++;
        return;
    }

    uint32_t intEvent = 0;
    uint32_t isoRecvEvent = 0;
    uint32_t isoXmitEvent = 0;
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIntEventSetOffset, &intEvent);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoRecvIntEventSetOffset, &isoRecvEvent);
    gPCIDevice->MemoryRead32(gPCIMemoryIndex, kOhciIsoXmitIntEventSetOffset, &isoXmitEvent);

    gOHCIInterruptLastIntEvent = intEvent;
    gOHCIInterruptLastIsoRecvEvent = isoRecvEvent;
    gOHCIInterruptLastIsoXmitEvent = isoXmitEvent;

    if ((intEvent & kOhciEventIsochRx) != 0 || isoRecvEvent != 0) {
        gOHCIInterruptIsoRecvCount++;
    }
    if ((intEvent & kOhciEventIsochTx) != 0 || isoXmitEvent != 0) {
        gOHCIInterruptIsoXmitCount++;
    }
    if ((intEvent & ~kOhciIsochInterruptMask) != 0) {
        gOHCIInterruptOtherCount++;
    }

    uint32_t intClear = intEvent & 0x7fffffffu;
    if (intClear != 0) {
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIntEventClearOffset, intClear);
    }
    gOHCIInterruptLastClearedIntEvent = intClear;

    uint32_t contextBit = 1u << kDigi00xDuplexContextIndex;
    bool digiRecvHit = (isoRecvEvent & contextBit) != 0;
    bool digiRunning =
        gDigiLiveRunning != 0 && gDigiLiveReady != 0 && gDigiLiveStopping == 0;
    uint32_t recvClear = digiRunning && digiRecvHit ? (isoRecvEvent & ~contextBit) : isoRecvEvent;
    uint32_t xmitClear = digiRunning && digiRecvHit ? (isoXmitEvent & ~contextBit) : isoXmitEvent;
    if (recvClear != 0) {
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoRecvIntEventClearOffset, recvClear);
    }
    if (xmitClear != 0) {
        gPCIDevice->MemoryWrite32(gPCIMemoryIndex, kOhciIsoXmitIntEventClearOffset, xmitClear);
    }
    gOHCIInterruptLastClearedIsoRecvEvent = recvClear;
    gOHCIInterruptLastClearedIsoXmitEvent = xmitClear;

    if (!digiRunning || !digiRecvHit) {
        return;
    }

    gOHCIInterruptDigiHarvestAttemptCount++;
    uint64_t busyBefore = gDigiLiveDrainBusyCount;
    kern_return_t harvestRet = HarvestDigiLiveIsoStream();
    gOHCIInterruptLastHarvestRet = ReturnCodeToProperty(harvestRet);
    if (harvestRet == kIOReturnSuccess) {
        gOHCIInterruptDigiHarvestSuccessCount++;
    } else if (harvestRet == kIOReturnBusy || gDigiLiveDrainBusyCount != busyBefore) {
        gOHCIInterruptDigiHarvestBusyCount++;
    } else {
        gOHCIInterruptDigiHarvestEmptyCount++;
    }
}

kern_return_t
IMPL(FireWireOHCIProbe, Stop)
{
    StopAudioRefreshWorker(true);
    if (DigiLiveStreamMayNeedStop()) {
        (void)StopDigiLiveStreamForAudio();
    }
    ReleaseOHCIInterruptDispatch();
    IOPCIDevice * pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (pciDevice != nullptr) {
        uint8_t memoryIndex = 0xff;
        uint64_t barSize = 0;
        uint8_t barType = 0xff;
        if (pciDevice->GetBARInfo(kBAR0, &memoryIndex, &barSize, &barType) == kIOReturnSuccess) {
            pciDevice->MemoryWrite32(memoryIndex, kOhciIntMaskClearOffset, 0xffffffff);
            pciDevice->MemoryWrite32(memoryIndex, kOhciHcControlClearOffset, kOhciHcControlLinkEnable);
        }
        pciDevice->Close(this, 0);
    }
    gPCIDevice = nullptr;
    gPCIMemoryIndex = 0xff;
    gDigiDestinationID = 0xffffffff;
    if (gAudioDevice) {
        RemoveObject(gAudioDevice.get());
    }
    gAudioOutputStream.reset();
    gAudioInputStream.reset();
    gAudioDevice.reset();
    OSSafeReleaseNULL(gAudioOutputBuffer);
    OSSafeReleaseNULL(gAudioInputBuffer);
    if (gAudioRefreshQueue != nullptr) {
        gAudioRefreshQueue->Cancel(nullptr);
        OSSafeReleaseNULL(gAudioRefreshQueue);
    }
    gAudioInputCPUAddress = {};
    gAudioOutputCPUAddress = {};
    gDriverInstance = nullptr;
    ReleaseDMABuffer(&gSelfIDBuffer);
    ReleaseDMABuffer(&gConfigROMBuffer);
    ReleaseDMABuffer(&gAsyncTxBuffer);
    ReleaseDMABuffer(&gAsyncRxBuffer);
    ReleaseDMABuffer(&gAsyncReqRxBuffer);
    ReleaseDMABuffer(&gIsoTestBuffer);
    ReleaseDMABuffer(&gDigiDuplexBuffer);
    ReleaseDMABuffer(&gDigiLiveBuffer);

    return Stop(provider, SUPERDISPATCH);
}
