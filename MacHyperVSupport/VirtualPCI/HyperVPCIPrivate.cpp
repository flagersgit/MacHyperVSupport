//
//  HyperVPCIPrivate.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

void HyperVPCI::handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count) {
  VMBusPacketType type;
  UInt32 headersize;
  UInt32 totalsize;
  
  void *responseBuffer;
  UInt32 responseLength = 0;
  
  while (true) {
    if (!hvDevice->nextPacketAvailable(&type, &headersize, &totalsize)) {
      DBGLOG("last packet; none available next.");
      break;
    }
    
    DBGLOG("allocating buffer for packet");
    void *buf = IOMalloc(totalsize);
    DBGLOG("reading packet to buffer");
    hvDevice->readRawPacket(buf, totalsize);
    
    switch (type) {
      case kVMBusPacketTypeCompletion:
        HyperVPCIPacket *completionPacket;
        HyperVPCIIncomingMessage *newMessage;

        DBGLOG("Packet type: completion");
        if (hvDevice->getPendingTransaction(((VMBusPacketHeader*)buf)->transactionId, &responseBuffer, &responseLength)) {
          memcpy(responseBuffer, (UInt8*)buf + headersize, responseLength);
          hvDevice->wakeTransaction(((VMBusPacketHeader*)buf)->transactionId);
        } else {
          completionPacket = (HyperVPCIPacket*)((VMBusPacketHeader*)buf)->transactionId;
          responseBuffer = (HyperVPCIResponse*)buf;
          completionPacket->completionFunc(completionPacket->completionCtx, (HyperVPCIResponse*)responseBuffer, responseLength);
        };
        break;
      
      case kVMBusPacketTypeDataInband:
        DBGLOG("Packet type: inband");
        if (hvDevice->getPendingTransaction(((VMBusPacketHeader*)buf)->transactionId, &responseBuffer, &responseLength)) {
          memcpy(responseBuffer, (UInt8*)buf + headersize, responseLength);
          hvDevice->wakeTransaction(((VMBusPacketHeader*)buf)->transactionId);
        } else {
          newMessage = (HyperVPCIIncomingMessage*)responseBuffer;
          switch (newMessage->messageType.type) {
            case kHyperVPCIMessageBusRelations:
              HyperVPCIBusRelations *busRelations = (HyperVPCIBusRelations*)buf;
              
              if (busRelations->deviceCount == 0) {
                DBGLOG("no devices in bus relations");
                break;
              }
              
              if (responseLength < offsetof(HyperVPCIBusRelations, funcDesc) + (sizeof(HyperVPCIFunctionDescription) * (busRelations->deviceCount))) {
                DBGLOG("bus relations too small");
                break;
              }
              
              pciDevicesPresent(busRelations);
          }
        }
        break;

      default:
        SYSLOG("Unhandled packet type: %d, tid %llx len %d", type, ((VMBusPacketHeader*)buf)->transactionId, responseLength);
        break;
    }
  }
}

IOReturn HyperVPCI::negotiateProtocol() {
  DBGLOG("called");
  IOReturn ret;
  
  HyperVPCIVersionRequest *versionRequest;
  HyperVPCICompletion      completion;
  
  struct {
    HyperVPCIPacket packet;
    UInt8 buffer[sizeof(HyperVPCIVersionRequest)];
  } ctx;
  
  completion.completion = HyperVCompletion::create();
  ctx.packet.completionFunc = genericCompletion;
  ctx.packet.completionCtx = &completion;
  
  versionRequest = (HyperVPCIVersionRequest*)&ctx.packet.message;
  versionRequest->messageType.type = kHyperVPCIMessageQueryProtocolVersion;
  versionRequest->protocolVersion = kHyperVPCIProtocolVersion11;
  versionRequest->isLastAttempt = 1;
  
  DBGLOG("sending packet requesting version: %d", versionRequest->protocolVersion);
  ret = hvDevice->writeInbandPacketWithTransactionId(versionRequest, sizeof(*versionRequest), (UInt64)&ctx.packet, true);
  
  if (ret == kIOReturnSuccess) {
    completion.completion->waitForCompletion();
    completion.completion->release();
  } else {
    SYSLOG("failed to send packet requesting version: %d", versionRequest->protocolVersion);
  }
  
  if (completion.status >= 0) {
    protocolVersion = (HyperVPCIProtocolVersion)versionRequest->protocolVersion;
    DBGLOG("using version: %d", protocolVersion);
  } else if (completion.status == kStatusRevisionMismatch) {
    //DBGLOG("unsupported protocol version (%d), attempting older version", versionRequest->protocolVersion);
    DBGLOG("unsupported protocol version (%d)", versionRequest->protocolVersion);
    ret = kIOReturnError;
  } else if (completion.status != kStatusRevisionMismatch) {
    SYSLOG("failed version request with unknown error: %#x", completion.status);
    ret = kIOReturnError;
  }
  
  return ret;
}

void HyperVPCI::pciDevicesPresent(HyperVPCIBusRelations *busRelations) {
  DBGLOG("called");
  HyperVPCIDeviceRelationsState  *drStateBuffer;
  OSData *drState;
  UInt32 drSize;
  
  drSize = (offsetof(HyperVPCIDeviceRelationsState, funcDesc) + (sizeof(HyperVPCIFunctionDescription) * (busRelations->deviceCount)));
  drState = OSData::withCapacity(drSize);
  drStateBuffer = (HyperVPCIDeviceRelationsState*)drState->getBytesNoCopy();
  
  drStateBuffer->deviceCount = busRelations->deviceCount;
  if (drStateBuffer->deviceCount != 0)
    memcpy(&drStateBuffer->funcDesc, &busRelations->funcDesc, sizeof(HyperVPCIFunctionDescription) * drStateBuffer->deviceCount);
  
  IOLockLock(deviceListLock);
  deviceRelationsList->setObject(drState);
  IOLockUnlock(deviceListLock);
  drState->release();
  
  commandGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &HyperVPCI::dispatchPciDevicesPresent));
}

void HyperVPCI::dispatchPciDevicesPresent() {
  HyperVPCIDeviceRelationsState  *drStateBuffer = NULL;
  OSData *drState = NULL;
  OSCollectionIterator          *iterator;
  HyperVPCIDevice               *hvPciDevice;
  HyperVPCIFunctionDescription  *newFuncDesc;
  HyperVCompletion                    *queryCompletion;
  bool found;
  bool needRescan = false;

  IOLockLock(deviceListLock);
  while (deviceRelationsList->getCount() != 0) {
    drState = (OSData*)deviceRelationsList->getObject(0);
    if (drState) {
      drStateBuffer = (HyperVPCIDeviceRelationsState*)drState->getBytesNoCopy();
      deviceRelationsList->removeObject(0);
      if (deviceRelationsList->getCount() != 0) {
        continue;
      }
    };
  }
  IOLockUnlock(deviceListLock);
  
  if (!drState) {
    return;
  }
  
  if (NULL != (iterator = OSCollectionIterator::withCollection(hvPciDevices)))
  {
    IOLockLock(deviceListLock);
    /* mark all existing devices as reported missing */
    while (NULL != (hvPciDevice = (HyperVPCIDevice*)iterator->getNextObject()))
      hvPciDevice->reportedMissing = true;
    IOLockUnlock(deviceListLock);
    iterator->reset();

    /* add back any reported devices */
    for (UInt32 childNum = 0; childNum < drStateBuffer->deviceCount; childNum++) {
      found = false;
      newFuncDesc = &drStateBuffer->funcDesc[childNum];
      IOLockLock(deviceListLock);
      while (NULL != (hvPciDevice = (HyperVPCIDevice*)iterator->getNextObject())) {
        if ((hvPciDevice->funcDesc.winSlot.slot == newFuncDesc->winSlot.slot) &&
            (hvPciDevice->funcDesc.venId == newFuncDesc->devId) &&
            (hvPciDevice->funcDesc.devId == newFuncDesc->devId) &&
            (hvPciDevice->funcDesc.ser == newFuncDesc->ser)) {
          hvPciDevice->reportedMissing = false;
          found = true;
          break;
        }
      }
      IOLockUnlock(deviceListLock);
      iterator->reset();
      
      if (!found) {
        if (!needRescan)
          needRescan = true;
        
        if (!registerChildDevice(newFuncDesc)) {
          SYSLOG("failed to register child device");
        }
      }
    }
    
    
    while (NULL != (hvPciDevice = (HyperVPCIDevice*)iterator->getNextObject()))
      if (hvPciDevice->reportedMissing == true)
        destroyChildDevice(hvPciDevice);
    
    queryCompletion = this->queryCompletion;
    if (queryCompletion) {
      this->queryCompletion = NULL;
      queryCompletion->complete();
    }
  
    iterator->release();
  }
  
}

void HyperVPCI::genericCompletion(void *ctx, HyperVPCIResponse *response, int responsePacketSize) {
  DBGLOG("called");
  HyperVPCICompletion* completion = (HyperVPCICompletion*)ctx;
  
  if (responsePacketSize >= (offsetof(HyperVPCIResponse, status)+sizeof(response->status))) {
    completion->status = response->status;
  } else {
    completion->status = -1;
  }
  
  completion->completion->complete();
}

IOReturn HyperVPCI::queryRelations() {
  DBGLOG("called");
  IOReturn          ret;
  HyperVPCIMessage  message;
  
  message.type = kHyperVPCIMessageQueryBusRelations;
  
  ret = hvDevice->writeInbandPacket(&message, sizeof(message), false);
  if (ret != kIOReturnSuccess) {
    SYSLOG("failed to send bus relations query");
  };
  
  return ret;
}

HyperVPCIDevice* HyperVPCI::registerChildDevice(HyperVPCIFunctionDescription *funcDesc) {
  DBGLOG("called");
  IOReturn ret;
  
  // Allocate and initialize HyperVPCIDevice object.
  HyperVPCIDevice *hvPciDevice = OSTypeAlloc(HyperVPCIDevice);
  
  HyperVPCIChildMessage *resourceRequest;
  HyperVPCIQueryResourceRequirementsCompletion completion;
  
  struct {
    HyperVPCIPacket packet;
    UInt8 buffer[sizeof(HyperVPCIChildMessage)];
  } ctx;
  
  completion.completion = HyperVCompletion::create();
  ctx.packet.completionFunc = genericCompletion;
  ctx.packet.completionCtx = &completion;
  
  resourceRequest = (HyperVPCIChildMessage*)&ctx.packet.message;
  resourceRequest->messageType.type = kHyperVPCIMessageQueryResourceRequirements;
  resourceRequest->winSlot.slot = funcDesc->winSlot.slot;
  
  ret = hvDevice->writeInbandPacketWithTransactionId(resourceRequest, sizeof(*resourceRequest), (UInt64)&ctx.packet, true);
  if (ret == kIOReturnSuccess) {
    completion.completion->waitForCompletion();
    completion.completion->release();
  } else {
    hvPciDevice->free();
    return NULL;
  }
  
  memcpy(&hvPciDevice->funcDesc, (void*)funcDesc, sizeof(*funcDesc));
  
  IOLockLock(deviceListLock);
  if (hvPciDevices->getCount() == 0) {
    pciDomain = funcDesc->ser;
  }
  hvPciDevices->setObject(hvPciDevice);
  IOLockUnlock(deviceListLock);

  return hvPciDevice;
}

void HyperVPCI::destroyChildDevice(HyperVPCIDevice *hvPciDevice) {
  DBGLOG("called");
  //
  // Notify IOPCIDevice nub of termination.
  //
  if (hvPciDevice->deviceNub != NULL) {
    hvPciDevice->deviceNub->terminate();
    hvPciDevice->deviceNub->release();
    hvPciDevice->deviceNub = NULL;
  }
  
  hvPciDevices->removeObject(hvPciDevices->getNextIndexOfObject((OSMetaClassBase*)hvPciDevice, 0));
  //hvPciDevice->free();
}

IOReturn HyperVPCI::enterD0() {
  DBGLOG("called");
  IOReturn ret;
  
  HyperVPCIBusD0Entry *d0Entry;
  struct {
    HyperVPCIPacket packet;
    UInt8 buffer[sizeof(HyperVPCIBusD0Entry)];
  } ctx;
  HyperVPCICompletion completion;
  
  completion.completion = HyperVCompletion::create();
  ctx.packet.completionFunc = genericCompletion;
  ctx.packet.completionCtx = &completion;
  
  d0Entry = (HyperVPCIBusD0Entry*)&ctx.packet.message;
  memset(d0Entry, 0, sizeof(*d0Entry));
  d0Entry->messageType.type = kHyperVPCIMessageBusD0Entry;
  d0Entry->mmioBase = ioMemory->getPhysicalAddress();
  
  ret = hvDevice->writeInbandPacketWithTransactionId(d0Entry, sizeof(*d0Entry), (UInt64)&ctx.packet, true);
  if (ret == kIOReturnSuccess) {
    completion.completion->waitForCompletion();
    completion.completion->release();
  }
  
  if (completion.status < 0) {
    SYSLOG("failed to enable D0 for bus");
    ret = kIOReturnError;
  }
  
  return ret;
}
