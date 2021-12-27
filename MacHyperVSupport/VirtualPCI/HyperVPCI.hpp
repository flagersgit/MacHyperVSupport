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

//#define IOPCIB_IMPL 0

#if IOPCIB_IMPL
#include <IOKit/pci/IOPCIBridge.h>

#define super IOPCIBridge
#else
#define super IOService
#endif

#define SYSLOG(str, ...) SYSLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)
#define DBGLOG(str, ...) DBGLOG_PRINT("HyperVPCI", str, ## __VA_ARGS__)

class HyperVPCIDevice : public OSObject {
  friend class HyperVPCI;
  
private:
  HyperVPCIFunctionDescription  funcDesc;
  bool                          reportedMissing;

  UInt32 probedBar[kHyperVPCIMaxNumBARs];
  
  //
  // I/O Kit nub for PCI device.
  //
  IOPCIDevice                  *deviceNub;
};

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
  
  //
  // Driver state tracking fields.
  //
  HyperVPCIProtocolVersion  protocolVersion;
  
  IODeviceMemory           *ioMemory;
  
  // Lock for hvPciDevices array.
  IOLock                   *deviceListLock = NULL;
  //
  // OSArray for tracking PCI devices under the synthetic bus.
  // Should only contain objects of type HyperVPCIDevice.
  //
  OSArray                  *hvPciDevices;
  
  void handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count);
  
  //
  // Completion functions
  //
  static void genericCompletion(void* ctx, HyperVPCIResponse *response, int responsePacketSize);
  
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
  virtual bool configure( IOService * provider );

  virtual IODeviceMemory * ioDeviceMemory();

  virtual UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset);
  virtual void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data);
  virtual UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset);
  virtual void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data);
  virtual UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset);
  virtual void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data);

  virtual IOPCIAddressSpace getBridgeSpace();

  virtual IOReturn setDevicePowerState(IOPCIDevice *device, unsigned long whatToDo);

  virtual void saveBridgeState();

  virtual void restoreBridgeState();
#endif
};

#endif /* HyperVPCI_hpp */
