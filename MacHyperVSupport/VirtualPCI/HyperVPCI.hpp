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

#if IOPCIB_IMPL
#define super IOPCIBridge
#else
#define super IOService
#endif

#define SYSLOG(str, ...) SYSLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)
#define DBGLOG(str, ...) DBGLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)

#if IOPCIB_IMPL
class HyperVPCI : public IOPCIBridge {
#else
class HyperVPCI : public IOService {
#endif
  OSDeclareDefaultStructors(HyperVPCI);
  
private:
  //
  // Parent VMBus device.
  //
  HyperVVMBusDevice        *hvDevice;
  IOInterruptEventSource   *interruptSource;
  
  HyperVPCIProtocolVersion  protocolVersion;
  
  //
  // OSArray for tracking PCI devices under the synthetic bus.
  // Contains: HyperVPCIDevice
  //
  OSArray                  *hvPciDevices;
  
  IODeviceMemory           *ioMemory;
  
  void onChannelCallback(OSObject *owner, IOInterruptEventSource *sender, int count);

  //
  // Completion functions
  //
  static void genericCompletion(void *ctx, HyperVPCIResponse *response, int responsePacketSize);
  static void queryResourceRequirements(void *ctx, HyperVPCIResponse *response, int responsePacketSize);
  
  IOReturn negotiateProtocol();
  
  IOReturn queryRelations();
  
  void pciDevicesPresent(HyperVPCIBusRelations *busRelations);
  
  HyperVPCIDevice* registerChildDevice(HyperVPCIFunctionDescription *funcDesc);
  void destroyChildDevice(HyperVPCIDevice *hvPciDevice);
  
  IOReturn enterD0();
  
    
public:
  //
  // IOService overrides.
  //
  virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

#if IOPCIB_IMPL
  //
  // IOPCIBridge overrides.
  //
  virtual bool configure(IOService *provider) APPLE_KEXT_OVERRIDE;
  virtual void probeBus(IOService *provider, UInt8 busNum) APPLE_KEXT_OVERRIDE;
  
  virtual IODeviceMemory* ioDeviceMemory(void) APPLE_KEXT_OVERRIDE {
    return ioMemory;
  };

  virtual UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
  virtual void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) APPLE_KEXT_OVERRIDE;
  virtual UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
  virtual void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) APPLE_KEXT_OVERRIDE;
  virtual UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
  virtual void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) APPLE_KEXT_OVERRIDE;

  virtual IOPCIAddressSpace getBridgeSpace() APPLE_KEXT_OVERRIDE {
    IOPCIAddressSpace space = { 0 };
    return space;
  }
  
  virtual UInt8 firstBusNum() APPLE_KEXT_OVERRIDE {
    return 0;
  }
  
  virtual UInt8 lastBusNum() APPLE_KEXT_OVERRIDE {
    return 0;
  }
#endif
};

#endif /* HyperVPCI_hpp */
