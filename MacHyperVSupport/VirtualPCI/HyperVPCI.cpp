//
//  HyperVPCI.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

#include <IOKit/acpi/IOACPIPlatformDevice.h>

typedef struct __attribute__((packed)) {
  UInt64 type;
  UInt64 reserved1;
  UInt64 reserved2;
  UInt64 min;
  UInt64 max;
  UInt64 reserved3;
  UInt64 length;
  UInt64 reserved4;
  UInt64 reserved5;
  UInt64 reserved6;
} AppleACPIRange;

OSDefineMetaClassAndStructors(HyperVPCI, super);

bool HyperVPCI::start(IOService *provider) {
  if (!super::start(provider)) {
    return false;
  }

  DBGLOG("Initializing Hyper-V Synthetic PCI Bus");
  
  //
  // Get parent VMBus device object.
  //
  hvDevice = OSDynamicCast(HyperVVMBusDevice, provider);
  if (hvDevice == NULL) {
    super::stop(provider);
    return false;
  }
  hvDevice->retain();
  
  //
  // Configure interrupt.
  //
  interruptSource =
    IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &HyperVPCI::handleInterrupt), provider, 0);
  getWorkLoop()->addEventSource(interruptSource);
  interruptSource->enable();
  
  //
  // Configure the channel.
  //
  if (!hvDevice->openChannel(kHyperVPCIRingBufferSize, kHyperVPCIRingBufferSize)) {
    super::stop(provider);
    return false;
  }

  negotiateProtocol(kHyperVPCIProtocolVersion11);
  
  queryRelations();
  
  SYSLOG("Initialized Hyper-V Synthetic PCI Bus");
  return true;

}

//bool HyperVPCI::configure(IOService *provider) {
//  //
//  // Add memory ranges from ACPI.
//  //
//  OSData *acpiAddressSpaces = OSDynamicCast(OSData, provider->getProperty("acpi-address-spaces"));
//  if (acpiAddressSpaces != NULL) {
//    AppleACPIRange *acpiRanges = (AppleACPIRange*) acpiAddressSpaces->getBytesNoCopy();
//    UInt32 acpiRangeCount = acpiAddressSpaces->getLength() / sizeof (AppleACPIRange);
//
//    for (int i = 0; i < acpiRangeCount; i++) {
//      DBGLOG("type %u, min %llX, max %llX, len %llX", acpiRanges[i].type, acpiRanges[i].min, acpiRanges[i].max, acpiRanges[i].length);
//      if (acpiRanges[i].type == 1) {
//        addBridgeIORange(acpiRanges[i].min, acpiRanges[i].length);
//      } else if (acpiRanges[i].type == 0) {
//        addBridgeMemoryRange(acpiRanges[i].min, acpiRanges[i].length, true);
//      }
//    }
//  }
//
//  return super::configure(provider);
//}
