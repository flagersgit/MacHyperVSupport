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

//#define IOPCIB_IMPL 1

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
  UInt16                    pciDomain;
  
  IODeviceMemory           *ioMemory;
  
  IOWorkLoop               *workLoop;
  IOCommandGate            *commandGate;
  
  // Completion for bus/device relations query.
  HyperVCompletion               *queryCompletion;
  
  // Lock for deviceRelationsList and hvPciDevices OSArray.
  IOLock                   *deviceListLock;
  // OSArray for tracking bus/device relations.
  // Should only contain objects of type HyperVPCIDeviceRelationsState.
  OSArray                  *deviceRelationsList;
  // OSArray for tracking PCI devices under the synthetic bus.
  // Should only contain objects of type HyperVPCIDevice.
  OSArray                  *hvPciDevices;
  
  void handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count);
  
  //
  // Completion functions
  //
  static void genericCompletion(void* ctx, HyperVPCIResponse *response, int responsePacketSize);
  
  IOReturn negotiateProtocol();
  
  IOReturn queryRelations();
  
  void pciDevicesPresent(HyperVPCIBusRelations *busRelations);
  void dispatchPciDevicesPresent();
  
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
  virtual bool configure( IOService * provider ) override;

  virtual IODeviceMemory * ioDeviceMemory() override;

  virtual UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset) override;
  virtual void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) override;
  virtual UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset) override;
  virtual void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) override;
  virtual UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset) override;
  virtual void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) override;

  virtual IOPCIAddressSpace getBridgeSpace() override;

  virtual IOReturn setDevicePowerState(IOPCIDevice *device, unsigned long whatToDo) override;

  virtual void saveBridgeState();

  virtual void restoreBridgeState();
#endif
};

#endif /* HyperVPCI_hpp */
