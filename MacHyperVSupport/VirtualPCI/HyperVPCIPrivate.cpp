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
          DBGLOG("here");
          completionPacket = (HyperVPCIPacket*)((VMBusPacketHeader*)buf)->transactionId;
          DBGLOG("there");
          responseBuffer = (HyperVPCIResponse*)buf;
          DBGLOG("everywhere");
          completionPacket->completionFunc(completionPacket->completionCtx, (HyperVPCIResponse*)responseBuffer, responseLength);
          DBGLOG("but not here");
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
              HyperVPCIBusRelations *busRelations;
              
              busRelations = (HyperVPCIBusRelations*)buf;
              if (busRelations->deviceCount == 0)
                break;
              
              if (responseLength < offsetof(HyperVPCIBusRelations, funcDesc) + (sizeof(HyperVPCIFunctionDescription) * (busRelations->deviceCount))) {
                SYSLOG("bus relations too small");
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
  
  completion.completion = Completion::create();
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
  } else {
    SYSLOG("failed to request version: %d", versionRequest->protocolVersion);
  }
  
  if (completion.status >= 0) {
    protocolVersion = (HyperVPCIProtocolVersion)versionRequest->protocolVersion;
    DBGLOG("using version: %d", protocolVersion);
  }
  
  if (completion.status == kStatusRevisionMismatch) {
    DBGLOG("unsupported protocol version (%d), attempting older version", versionRequest->protocolVersion);
  }
  
  if (completion.status != kStatusRevisionMismatch) {
    SYSLOG("failed version request: %#x", completion.status);
  }
  
  return ret;
}

void HyperVPCI::pciDevicesPresent(HyperVPCIBusRelations *busRelations) {
  DBGLOG("called");
  OSCollectionIterator          *iterator;
  HyperVPCIDevice               *hvPciDevice;
  HyperVPCIFunctionDescription  *newFuncDesc;
  bool found;
  bool needRescan = false;
  
  if (NULL != (iterator = OSCollectionIterator::withCollection(hvPciDevices)))
  {
    /* mark all existing devices as reported missing */
    while (NULL != (hvPciDevice = (HyperVPCIDevice*)iterator->getNextObject()))
      hvPciDevice->reportedMissing = true;
    iterator->reset();

    /* add back any reported devices */
    for (UInt32 childNum = 0; childNum < busRelations->deviceCount; childNum++) {
      found = false;
      newFuncDesc = &busRelations->funcDesc[childNum];
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
      iterator->reset();
      
      if (!found) {
        if (!needRescan)
          needRescan = true;

//        hpdev = new_pcichild_device(hbus, new_desc);
//        if (!hpdev)
//          printf("vmbus_pcib: failed to add a child\n");
      }
    }
    
    
    while (NULL != (hvPciDevice = (HyperVPCIDevice*)iterator->getNextObject()))
      if (hvPciDevice->reportedMissing == true)
        destroyChildDevice(hvPciDevice);
  
    iterator->release();
  }
  return;
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
  IOLock           *lock;
  
  lock = IOLockAlloc();
  if (lock == NULL) {
    SYSLOG("failed to allocate lock for request");
    return kIOReturnCannotLock;
  };
  
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
  
  completion.completion = Completion::create();
  ctx.packet.completionFunc = genericCompletion;
  ctx.packet.completionCtx = &completion;
  
  resourceRequest = (HyperVPCIChildMessage*)&ctx.packet.message;
  resourceRequest->messageType.type = kHyperVPCIMessageQueryResourceRequirements;
  resourceRequest->winSlot.slot = funcDesc->winSlot.slot;
  
  ret = hvDevice->writeInbandPacketWithTransactionId(resourceRequest, sizeof(*resourceRequest), (UInt64)&ctx.packet, true);
  if (ret == kIOReturnSuccess) {
    completion.completion->waitForCompletion();
  } else {
    hvPciDevice->free();
    return NULL;
  }
  
  memcpy(&hvPciDevice->funcDesc, (void*)funcDesc, sizeof(*funcDesc));

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
  
  completion.completion = Completion::create();
  ctx.packet.completionFunc = genericCompletion;
  ctx.packet.completionCtx = &completion;
  
  d0Entry = (HyperVPCIBusD0Entry*)&ctx.packet.message;
  memset(d0Entry, 0, sizeof(*d0Entry));
  d0Entry->messageType.type = kHyperVPCIMessageBusD0Entry;
  d0Entry->mmioBase = ioMemory->getPhysicalAddress();
  
  ret = hvDevice->writeInbandPacketWithTransactionId(d0Entry, sizeof(*d0Entry), (UInt64)&ctx.packet, true);
  if (ret == kIOReturnSuccess) {
    completion.completion->waitForCompletion();
  }
  
  if (completion.status < 0) {
    SYSLOG("failed to enable D0 for bus");
    ret = kIOReturnError;
  }
  
  return ret;
}
