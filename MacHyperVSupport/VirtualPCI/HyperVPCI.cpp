//
//  HyperVPCI.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/acpi/IOACPITypes.h>

OSDefineMetaClassAndStructors(HyperVPCI, super);

bool HyperVPCI::start(IOService *provider) {
  DBGLOG("Initializing Hyper-V Synthetic PCI Bus");
  if (!super::start(provider)) {
    return false;
  }
  
  //
  // Get parent VMBus device object.
  //
  hvDevice = OSDynamicCast(HyperVVMBusDevice, provider);
  if (hvDevice == NULL) {
    super::stop(provider);
    return false;
  }
  hvDevice->retain();
  
  IODeviceMemory *devMem = hvDevice->allocateMmio(0, PCI_CONFIG_MMIO_LENGTH, PAGE_SIZE, false);
  OSArray *devMemArr = OSArray::withCapacity(1);
  devMemArr->setObject(0, devMem);
  
  provider->setDeviceMemory(devMemArr);
  devMem->release();
  devMemArr->release();
  
  ioMemory = (IODeviceMemory *)devMemArr->getObject(0);
  if (ioMemory == NULL) {
    SYSLOG("could not allocate memory for bridge");
    super::stop(provider);
    return false;
  }
  
  //
  // Configure interrupt.
  //
  interruptSource =
    IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &HyperVPCI::onChannelCallback), provider, 0);
  getWorkLoop()->addEventSource(interruptSource);
  interruptSource->enable();
  
  //
  // Configure the channel.
  //
  if (!hvDevice->openChannel(kHyperVPCIRingBufferSize, kHyperVPCIRingBufferSize)) {
    super::stop(provider);
    return false;
  }

  if (negotiateProtocol() != kIOReturnSuccess) {
    super::stop(provider);
    return false;
  }
  
  if (queryRelations() != kIOReturnSuccess) {
    super::stop(provider);
    return false;
  }
  
  if (enterD0() != kIOReturnSuccess) {
    super::stop(provider);
    return false;
  }
  
  SYSLOG("Initialized Hyper-V Synthetic PCI Bus");
  return true;
}

#if IOPCIB_IMPL
bool HyperVPCI::configure(IOService *provider) {
  return super::configure(provider);
}
#endif
