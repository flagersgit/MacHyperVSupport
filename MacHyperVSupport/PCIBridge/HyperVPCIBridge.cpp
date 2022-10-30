//
//  HyperVPCIBridge.cpp
//  Hyper-V PCI passthrough device support
//
//  Copyright © 2022 Goldfish64. All rights reserved.
//

#include "HyperVPCIBridge.hpp"
#include "HyperVPCIRoot.hpp"

OSDefineMetaClassAndStructors(HyperVPCIBridge, super);

bool HyperVPCIBridge::start(IOService *provider) {
  if (HVCheckOffArg()) {
    return false;
  }
  
  //
  // Get parent VMBus device object.
  //
  _hvDevice = OSDynamicCast(HyperVVMBusDevice, provider);
  if (_hvDevice == NULL) {
    return false;
  }
  _hvDevice->retain();
  HVCheckDebugArgs();
  HVDBGLOG("Initializing Hyper-V Synthetic PCI Bus");
  
  //
  // Configure interrupt.
  //
  interruptSource =
    IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &HyperVPCIBridge::handleInterrupt), provider, 0);
  getWorkLoop()->addEventSource(interruptSource);
  interruptSource->enable();
  
  //
  // Configure the channel.
  //
  if (_hvDevice->openVMBusChannel(kHyperVPCIBridgeRingBufferSize, kHyperVPCIBridgeRingBufferSize, 0xFFFFFFFF) != kIOReturnSuccess) {
    return false;
  }
  
  pciLock = IOSimpleLockAlloc();
  
  //
  // Locate root PCI bus instance and register ourselves.
  //
  busNum = HyperVPCIRoot::registerChildPCIBridge(this);
  if (!busNum) {
    HVSYSLOG("Failed to register with root PCI bus instance");
    _hvDevice->release();
    return false;
  }
  
  if (!HyperVPCIProvider::registerHotplugPCIBridge(this)) {
    _hvDevice->release();
    return false;
  }
  
  // Negoiate protocol version and send request for functions.
  if (!negotiateProtocolVersion() || !allocatePCIConfigWindow() || !queryBusRelations() || !enterPCID0() || !queryResourceRequirements() || !sendResourcesAllocated(0)) {
    _hvDevice->closeVMBusChannel();
    return false;
  }
  
  if (!super::start(provider)) {
    return false;
  }
  
  return true;
}

void HyperVPCIBridge::stop(IOService *provider) {
  HVDBGLOG("Stopping Hyper-V Synthetic PCI Bus");
  
  if (pciDeviceNub != nullptr) {
    kernelRequestProbe(pciDeviceNub, kIOPCIProbeOptionEject);
    OSSafeReleaseNULL(pciDeviceNub);
  }
  
  if (pciFunctions != nullptr) {
    bzero(pciFunctions, pciFunctionCount * sizeof (HyperVPCIFunctionDescription));
    IOFree(pciFunctions, pciFunctionCount * sizeof (HyperVPCIFunctionDescription));
    pciFunctionCount = 0;
  }
  
  sendResourcesReleased(0); // TODO: This needs to be variable.
  
  HyperVPCIBridgeMessageHeader exitD0Pkt;
  memset(&exitD0Pkt, 0, sizeof (exitD0Pkt));
  exitD0Pkt.type = kHyperVPCIBridgeMessageTypeBusD0Exit;
  _hvDevice->writeInbandPacket(&exitD0Pkt, sizeof (exitD0Pkt), false);
  
  // Free BAR allocations.
  for (int i = 0; i < arrsize(bars); i++) {
    if (bars[i] != 0) {
      hvModuleDevice->freeRange(bars[i], barSizes[i]);
      
      // Skip over next bar if 64-bit.
      if (bars[i] > UINT32_MAX) {
        i++;
      }
    }
  }
  
  // Free PCI bridge and function window.
  hvModuleDevice->freeRange(pciConfigSpace, kHyperVPCIBridgeWindowSize);
  
  

  if (_hvDevice != nullptr) {
    _hvDevice->closeVMBusChannel();
    _hvDevice->uninstallPacketActions();
    OSSafeReleaseNULL(_hvDevice);
  }

  super::stop(provider);
}

bool HyperVPCIBridge::configure(IOService *provider) {
  // Populate device memory with BARs. TODO: Verify if this is correct for multiple devices.
  for (int i = 0; i < arrsize(bars); i++) {
    if (bars[i] != 0) {
      addBridgeMemoryRange(bars[i], barSizes[i], true);
      
      // Skip over next bar if 64-bit.
      if (bars[i] > UINT32_MAX) {
        i++;
      }
    }
  }
  
  return super::configure(provider);
}

UInt32 HyperVPCIBridge::configRead32(IOPCIAddressSpace space, UInt8 offset) {
  HVDBGLOG("Bus: %u, device: %u, function: %u, offset %X", space.es.busNum, space.es.deviceNum, space.es.functionNum, offset);
  
  if (space.es.deviceNum != 0 || space.es.functionNum != 0) {
    return 0xFFFFFFFF;
  }
  
  return readPCIConfig(offset, sizeof (UInt32));
}

void HyperVPCIBridge::configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) {
  HVDBGLOG("Bus: %u, device: %u, function: %u, offset %X", space.es.busNum, space.es.deviceNum, space.es.functionNum, offset);
  
  if (space.es.deviceNum != 0 || space.es.functionNum != 0) {
    return;
  }
  
  writePCIConfig(offset, sizeof (UInt32), data);
}

UInt16 HyperVPCIBridge::configRead16(IOPCIAddressSpace space, UInt8 offset) {
  HVDBGLOG("Bus: %u, device: %u, function: %u, offset %X", space.es.busNum, space.es.deviceNum, space.es.functionNum, offset);
  
  if (space.es.deviceNum != 0 || space.es.functionNum != 0) {
    return 0xFFFF;
  }
  
  return (UInt16)readPCIConfig(offset, sizeof (UInt16));
}

void HyperVPCIBridge::configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) {
  HVDBGLOG("Bus: %u, device: %u, function: %u, offset %X", space.es.busNum, space.es.deviceNum, space.es.functionNum, offset);
  
  if (space.es.deviceNum != 0 || space.es.functionNum != 0) {
    return;
  }
  
  //
  // Hook writes to MSI or MSI-X control register.
  // This is the final step in configuring interrupts by IOPCIFamily.
  //
  if (!interruptConfigured && offset == msiCap + 0x2 && data & 0x1) {
    HVDBGLOG("Original value: 0x%X", data);
    
    if (isMsiX) {
      
    } else {
      // Determine number of vectors and starting vector.
      UInt16 msiControl = data & ~(0x1); // (UInt16) readPCIConfig(msiCap + 0x2, sizeof (UInt16));
      bool msi64Bit = msiControl & 0x80;
      
      UInt16 msiData = (UInt16) readPCIConfig(msiCap + (msi64Bit ? 0xC : 0x8), sizeof (UInt16));
      UInt8 vectorStart = msiData & 0xFF;
      UInt8 vectorCount = 1 << (0x7 & (msiControl >> 4));
      
      HVDBGLOG("MSI will use %u vectors starting at 0x%X", vectorCount, vectorStart);
      
      HyperVPCIBridgeMessageCreateInterrupt pktInt;
      pktInt.header.type = kHyperVPCIBridgeMessageTypeCreateInterruptMessage;
      pktInt.slot.slot = 0;
      pktInt.vector = vectorStart;
      pktInt.vectorCount = vectorCount;
      pktInt.deliveryMode = 0;
      pktInt.cpuMask = 1;
      
      HyperVPCIBridgeMessageCreateInterruptResponse pciStatus;
      if (_hvDevice->writeInbandPacket(&pktInt, sizeof (pktInt), true, &pciStatus, sizeof (pciStatus)) != kIOReturnSuccess) {
       // return false;
      }
      
      HVDBGLOG("PCI status %X %X %X %X", pciStatus.data, pciStatus.address, pciStatus.vectorCount, pciStatus.status);
      
      writePCIConfig(msiCap + 0x4, 4, pciStatus.address);
      writePCIConfig(msiCap + 0xC, 2, pciStatus.data);
      writePCIConfig(msiCap + 0x2, 2, msiControl | 0x1);
      interruptConfigured = true;
    }
  } else {
    writePCIConfig(offset, sizeof (UInt16), data);
  }
}

UInt8 HyperVPCIBridge::configRead8(IOPCIAddressSpace space, UInt8 offset) {
  HVDBGLOG("Bus: %u, device: %u, function: %u, offset %X", space.es.busNum, space.es.deviceNum, space.es.functionNum, offset);
  
  if (space.es.deviceNum != 0 || space.es.functionNum != 0) {
    return 0xFF;
  }
  
  return (UInt8)readPCIConfig(offset, sizeof (UInt8));
}

void HyperVPCIBridge::configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) {
  HVDBGLOG("Bus: %u, device: %u, function: %u, offset %X", space.es.busNum, space.es.deviceNum, space.es.functionNum, offset);
  
  if (space.es.deviceNum != 0 || space.es.functionNum != 0) {
    return;
  }
  
  writePCIConfig(offset, sizeof (UInt8), data);
}

bool HyperVPCIBridge::publishNub(IOPCIDevice *nub, UInt32 index) {
  // Get and store MSI or MSI-X capabilities pointer.
  // TODO: MSI-X and multiple devices
  IOByteCount cap;
  nub->extendedFindPCICapability(kIOPCIMSICapability, &cap);
  msiCap = (UInt32)cap;
  HVDBGLOG("Got MSI cap pointer at 0x%X", msiCap);
  
  OSArray *array = IODeviceMemory::arrayFromList(iodmRangeList, iodmRangeListIdx + 1);
  if (!array)
    return false;

  nub->setDeviceMemory(array);
  array->release();
  
  pciDeviceNub = nub;
  pciDeviceNub->retain();
  
  return super::publishNub(nub, index);
}
