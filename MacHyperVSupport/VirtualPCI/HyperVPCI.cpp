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
  
  deviceListLock = IOLockAlloc();
  deviceRelationsList = OSArray::withCapacity(1);
  hvPciDevices = OSArray::withCapacity(1);
  
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
    IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &HyperVPCI::handleInterrupt), provider, 0);
  getWorkLoop()->addEventSource(interruptSource);
  interruptSource->enable();
  
  if (NULL == (queryCompletion = HyperVCompletion::create())) {
    return false;
  }
  
  workLoop = IOWorkLoop::workLoop();
  commandGate = IOCommandGate::commandGate(this);
  if (!workLoop || !commandGate || (workLoop->addEventSource(commandGate) != kIOReturnSuccess)) {
    SYSLOG("failed to add commandGate");
    return false;
  }
  
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
  
  if (queryRelations() == kIOReturnSuccess) {
    queryCompletion->waitForCompletion();
  } else {
    super::stop(provider);
    return false;
  }
  
  //if (enterD0() != kIOReturnSuccess) {
  //  super::stop(provider);
  //  return false;
  //}
  
  SYSLOG("Initialized Hyper-V Synthetic PCI Bus");
  return true;
}

#if IOPCIB_IMPL
bool HyperVPCI::configure(IOService *provider) {
  return super::configure(provider);
}
#endif
