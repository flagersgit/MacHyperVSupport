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

//#define super IOPCIBridge
#define super IOService

#define SYSLOG(str, ...) SYSLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)
#define DBGLOG(str, ...) DBGLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)

//class HyperVPCI : public IOPCIBridge {
class HyperVPCI : public IOService {
  OSDeclareDefaultStructors(HyperVPCI);
  
private:
  //
  // Parent VMBus device.
  //
  HyperVVMBusDevice       *hvDevice;
  IOInterruptEventSource  *interruptSource;
  
  void handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count);
  
  IOReturn negotiateProtocol(HyperVPCIProtocolVersion version);
  
  static void genericCompletion(void *ctx, HyperVPCIResponse *response, int responsePacketSize);
  
  IOReturn queryRelations();
    
public:
  //
  // IOService overrides.
  //
  virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
  
  //
  // IOPCIBridge overrides.
  //
//  virtual bool configure(IOService *provider) APPLE_KEXT_OVERRIDE;
//  IODeviceMemory *ioDeviceMemory() APPLE_KEXT_OVERRIDE { return NULL; }
//
//  UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
//  void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) APPLE_KEXT_OVERRIDE;
//  UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
//  void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) APPLE_KEXT_OVERRIDE;
//  UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
//  void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) APPLE_KEXT_OVERRIDE;
//
//  IOPCIAddressSpace getBridgeSpace() APPLE_KEXT_OVERRIDE {
//    IOPCIAddressSpace space = { 0 };
//    return space;
//  }
};

#endif /* HyperVPCI_hpp */
