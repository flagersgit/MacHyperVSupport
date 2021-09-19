//
//  HyperVPCI.hpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#ifndef HyperVPCI_hpp
#define HyperVPCI_hpp

#include "HyperVVMBusDevice.hpp"
#include "HyperVPCIRegs.hpp"

#include <IOKit/pci/IOPCIBridge.h>

#define super IOService

#define SYSLOG(str, ...) SYSLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)
#define DBGLOG(str, ...) DBGLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)

class HyperVPCI : public IOService {
  OSDeclareDefaultStructors(HyperVPCI);
  
private:
  //
  // Parent VMBus device.
  //
  HyperVVMBusDevice       *hvDevice;
  IOInterruptEventSource  *interruptSource;
  
  void handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count);
  
  bool negotiateProtocol(HyperVPCIProtocolVersion protocolVersion);
  
public:
  //
  // IOService overrides.
  //
  virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

#endif /* HyperVPCI_hpp */
