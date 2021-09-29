//
//  HyperVPCIRegs.hpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#ifndef HyperVPCIRegs_h
#define HyperVPCIRegs_h

#include "HyperVPCI.hpp"

/*
 * Protocol versions. The low word is the minor version, the high word the
 * major version.
 */

#define HyperVPCIMakeVersion(major, minor) ((UInt32)(((major) << 16) | (minor)))
#define HyperVPCIGetMajorVersion(version) ((UInt32)(version) >> 16)
#define HyperVPCIGetMinorVersion(version) ((UInt32)(version) & 0xff)

typedef enum : UInt32 {
  kHyperVPCIProtocolVersion11 = HyperVPCIMakeVersion(1, 1),    /* TH1 */
  kHyperVPCIProtocolVersion12 = HyperVPCIMakeVersion(1, 2),    /* RS1 */
  kHyperVPCIProtocolVersion13 = HyperVPCIMakeVersion(1, 3),    /* Vibranium */
  kHyperVPCIProtocolVersion14 = HyperVPCIMakeVersion(1, 4)     /* Iron */
} HyperVPCIProtocolVersion;

/*
 * Message Types
 */
typedef enum : UInt32 {
  /*
   * Version 1.1
   */
  kHyperVPCIMessageBase                       = 0x42490000,
  kHyperVPCIMessageBusRelations               = kHyperVPCIMessageBase + 0,
  kHyperVPCIMessageQueryBusRelations          = kHyperVPCIMessageBase + 1,
  kHyperVPCIMessagePowerStateChange           = kHyperVPCIMessageBase + 4,
  kHyperVPCIMessageQueryResourceRequirements  = kHyperVPCIMessageBase + 5,
  kHyperVPCIMessageQueryResourceResources     = kHyperVPCIMessageBase + 6,
  kHyperVPCIMessageBusD0Entry                 = kHyperVPCIMessageBase + 7,
  kHyperVPCIMessageBusD0Exit                  = kHyperVPCIMessageBase + 8,
  kHyperVPCIMessageReadBlock                  = kHyperVPCIMessageBase + 9,
  kHyperVPCIMessageWriteBlock                 = kHyperVPCIMessageBase + 0xA,
  kHyperVPCIMessageEject                      = kHyperVPCIMessageBase + 0xB,
  kHyperVPCIMessageQueryStop                  = kHyperVPCIMessageBase + 0xC,
  kHyperVPCIMessageReenable                   = kHyperVPCIMessageBase + 0xD,
  kHyperVPCIMessageQueryStopFailed            = kHyperVPCIMessageBase + 0xE,
  kHyperVPCIMessageEjectionComplete           = kHyperVPCIMessageBase + 0xF,
  kHyperVPCIMessageResourcesAssigned          = kHyperVPCIMessageBase + 0x10,
  kHyperVPCIMessageResourcesReleased          = kHyperVPCIMessageBase + 0x11,
  kHyperVPCIMessageInvalidateBlock            = kHyperVPCIMessageBase + 0x12,
  kHyperVPCIMessageQueryProtocolVersion       = kHyperVPCIMessageBase + 0x13,
  kHyperVPCIMessageCreateInterruptMessage     = kHyperVPCIMessageBase + 0x14,
  kHyperVPCIMessageDeleteInterruptMessage     = kHyperVPCIMessageBase + 0x15,
  kHyperVPCIMessageResourcesAssigned2         = kHyperVPCIMessageBase + 0x16,
  kHyperVPCIMessageCreateInterruptMessage2    = kHyperVPCIMessageBase + 0x17,
  kHyperVPCIMessageDeleteInterruptMessage2    = kHyperVPCIMessageBase + 0x18, /* unused */
  kHyperVPCIMessageBusRelations2              = kHyperVPCIMessageBase + 0x19,
  kHyperVPCIMessageResourcesAssigned3         = kHyperVPCIMessageBase + 0x1A,
  kHyperVPCIMessageCreateInterruptMessage3    = kHyperVPCIMessageBase + 0x1B,
  kHyperVPCIMessageMaximum
} HyperVPCIMessageType;

/*
 * Structures defining the virtual PCI Express protocol.
 */

typedef union __attribute__((packed)) {
  struct {
    UInt16 minorVersion;
    UInt16 majorVersion;
  } parts;
  UInt32 version;
} HyperVPCIVersion;

/*
 * Function numbers are 8-bits wide on Express, as interpreted through ARI,
 * which is all this driver does.  This representation is the one used in
 * Windows, which is what is expected when sending this back and forth with
 * the Hyper-V parent partition.
 */
typedef union __attribute__((packed)) {
  struct {
    UInt32  dev:5;
    UInt32  func:3;
    UInt32  reserved:24;
  } bits;
  UInt32 slot;
} HyperVPCIWindowsSlotEncoding;

/*
 * A generic message format for virtual PCI.
 * Specific message formats are defined later in the file.
 */

typedef struct __attribute__((packed)) {
  UInt32 type;
} HyperVPCIMessage;

typedef struct __attribute__((packed)) {
  HyperVPCIMessage messageType;
  HyperVPCIWindowsSlotEncoding winSlot;
} HyperVPCIChildMessage;

typedef struct __attribute__((packed)) {
  VMBusPacketHeader hdr;
  HyperVPCIMessage messageType;
} HyperVPCIIncomingMessage;

typedef struct __attribute__((packed)) {
  VMBusPacketHeader hdr;
  SInt32 status;      /* negative values are failures */
} HyperVPCIResponse;

typedef struct {
  void (*completionFunc)(void *ctx, HyperVPCIResponse *response,
                         int responsePacketSize);
  void *completionCtx;

  HyperVPCIMessage message[];
} HyperVPCIPacket;

typedef struct {
  SInt32 status;
} HyperVPCICompletion;


/*
 * Specific message types supporting the PCI protocol
 */

/*
 * Version negotiation message. Sent from the guest to the host.
 * The guest is free to try different versions until the host
 * accepts the version.
 *
 * pci_version: The protocol version requested.
 * is_last_attempt: If TRUE, this is the last version guest will request.
 * reservedz: Reserved field, set to zero.
 */

typedef struct __attribute__((packed)) {
  HyperVPCIMessage messageType;
  UInt32 protocolVersion;
} HyperVPCIVersionRequest;

#define kHyperVPCIRingBufferSize (4 * PAGE_SIZE)

#endif /* HyperVPCIRegs_h */
