//
//  HyperVPCI.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

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
  
  SYSLOG("Initialized Hyper-V Synthetic PCI Bus");
  return true;

}
