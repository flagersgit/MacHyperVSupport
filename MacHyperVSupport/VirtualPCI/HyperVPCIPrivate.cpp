//
//  HyperVPCIPrivate.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

void HyperVPCI::onChannelCallback(OSObject *owner, IOInterruptEventSource *sender, int count) {
  DBGLOG("Interrupt");
  VMBusPacketType type;
  UInt32 headersize;
  UInt32 totalsize;
  
  HyperVPCIPacket *completionPacket;
  HyperVPCIIncomingMessage *newMessage;
  
  HyperVPCIBusRelations *busRelations;

  void *responseBuffer;
  UInt32 responseLength = 0;

  while (true) {
    if (!hvDevice->nextPacketAvailable(&type, &headersize, &totalsize)) {
      DBGLOG("Last packet");
      break;
    }
    
    UInt8 *buf = (UInt8*)IOMalloc(totalsize);
    DBGLOG("Reading packet to buffer");
    hvDevice->readRawPacket((void*)buf, totalsize);
    
    switch (type) {
      case kVMBusPacketTypeCompletion:
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
    
    IOFree(buf, totalsize);
  }
}


IOReturn HyperVPCI::negotiateProtocol() {
  IOReturn ret;
  
  HyperVPCIVersionRequest *versionRequest;
  HyperVPCIPacket *packet;
  HyperVPCICompletion *completionPacket;
  
  packet = (HyperVPCIPacket*)IOMalloc(sizeof(HyperVPCIPacket) + sizeof(HyperVPCIVersionRequest));
  if (!packet){ return kIOReturnNoMemory; }
  
  packet->completionFunc = genericCompletion;
  packet->completionCtx = &completionPacket;
  
  versionRequest = (HyperVPCIVersionRequest*)&packet->message;
  versionRequest->messageType.type = kHyperVPCIMessageQueryProtocolVersion;
  versionRequest->protocolVersion = kHyperVPCIProtocolVersion11;
  versionRequest->isLastAttempt = 1;
  
  DBGLOG("sending packet requesting version: %d", versionRequest->protocolVersion);
  ret = hvDevice->writeInbandPacketWithTransactionId(versionRequest, sizeof(*versionRequest), (UInt64)packet, true);
  if (ret != kIOReturnSuccess) {
    SYSLOG("failed to request version: %d", versionRequest->protocolVersion);
  };
  
  if (completionPacket->status >= 0) {
    protocolVersion = (HyperVPCIProtocolVersion)versionRequest->protocolVersion;
    DBGLOG("using version: %d", protocolVersion);
  }
  
  if (completionPacket->status == kStatusRevisionMismatch) {
    DBGLOG("unsupported protocol version (%d), attempting older version", versionRequest->protocolVersion);
  }
  
  if (completionPacket->status != kStatusRevisionMismatch) {
    SYSLOG("failed version request: %#x", completionPacket->status);
  }
  
  IOFree(packet, sizeof(HyperVPCIPacket) + sizeof(HyperVPCIVersionRequest));
  return ret;
}


void HyperVPCI::genericCompletion(void *ctx, HyperVPCIResponse *response, int responsePacketSize) {
  HyperVPCICompletion* completionPacket = (HyperVPCICompletion*)ctx;
  
  if (responsePacketSize >= (offsetof(HyperVPCIResponse, status)+sizeof(response->status))) {
    completionPacket->status = response->status;
  } else {
    completionPacket->status = -1;
  }
}

void HyperVPCI::queryResourceRequirements(void *ctx, HyperVPCIResponse *response, int responsePacketSize) {
  HyperVPCIQueryResourceRequirementsCompletion* completionPacket = (HyperVPCIQueryResourceRequirementsCompletion*)ctx;
  HyperVPCIQueryResourceRequirementsResponse* queryResourceResponse = (HyperVPCIQueryResourceRequirementsResponse*)response;
  
  if (queryResourceResponse->respHdr.status < 0) {
    SYSLOG("failed to query resource requirements");
  } else {
    for (int i = 0; i < kHyperVPCIMaxNumBARs; i++) {
      completionPacket->hvPciDevice->probedBar[i] = queryResourceResponse->probedBar[i];
    }
  }
};


IOReturn HyperVPCI::queryRelations() {
  IOReturn ret;
  HyperVPCIMessage message;
  
  message.type = kHyperVPCIMessageQueryBusRelations;
  
  ret = hvDevice->writeInbandPacket(&message, sizeof(message), false);
  if (ret != kIOReturnSuccess) {
    SYSLOG("failed to query relations");
  };
  
  return ret;
}


void HyperVPCI::pciDevicesPresent(HyperVPCIBusRelations *busRelations) {
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

HyperVPCIDevice* HyperVPCI::registerChildDevice(HyperVPCIFunctionDescription *funcDesc) {
  // Allocate and initialize HyperVPCIDevice object.
  HyperVPCIDevice *hvPciDevice = OSTypeAlloc(HyperVPCIDevice);
  
  HyperVPCIChildMessage *resourceRequest;
  HyperVPCIPacket *packet;
  HyperVPCIQueryResourceRequirementsCompletion *completionPacket;
  
  packet = (HyperVPCIPacket*)IOMalloc(sizeof(HyperVPCIPacket) + sizeof(HyperVPCIChildMessage));
  if (!packet) {
    hvPciDevice->free();
    return NULL;
  }
  
  packet->completionFunc = HyperVPCI::genericCompletion;
  packet->completionCtx = &completionPacket;
  
  resourceRequest = (HyperVPCIChildMessage*)&packet->message;
  resourceRequest->messageType.type = kHyperVPCIMessageQueryResourceRequirements;
  resourceRequest->winSlot.slot = funcDesc->winSlot.slot;
  
  if (hvDevice->writeInbandPacketWithTransactionId(resourceRequest, sizeof(*resourceRequest), (UInt64)packet, true) != kIOReturnSuccess) {
    hvPciDevice->free();
    return NULL;
  }
  
  memcpy(&hvPciDevice->funcDesc, (void*)funcDesc, sizeof(*funcDesc));

  hvPciDevice->free();
  IOFree(packet, sizeof(HyperVPCIPacket) + sizeof(HyperVPCIChildMessage));
  return NULL;
}

void HyperVPCI::destroyChildDevice(HyperVPCIDevice *hvPciDevice) {
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
  IOReturn ret;
  
  HyperVPCIBusD0Entry *d0Entry;
  HyperVPCIPacket *packet;
  HyperVPCICompletion *completionPacket;
  
  packet = (HyperVPCIPacket*)IOMalloc(sizeof(HyperVPCIPacket) + sizeof(HyperVPCIBusD0Entry));
  if (!packet){ return kIOReturnNoMemory; }
  
  packet->completionFunc = genericCompletion;
  packet->completionCtx = &completionPacket;
  
  d0Entry = (HyperVPCIBusD0Entry*)&packet->message;
  memset(d0Entry, 0, sizeof(*d0Entry));
  d0Entry->messageType.type = kHyperVPCIMessageBusD0Entry;
  d0Entry->mmioBase = ioMemory->getPhysicalAddress();
  
  ret = hvDevice->writeInbandPacketWithTransactionId(d0Entry, sizeof(*d0Entry), (UInt64)packet, true);
  
  if (completionPacket->status < 0) {
    SYSLOG("failed to enable D0 for bus");
    ret = kIOReturnError;
  }
  
  IOFree(packet, sizeof(HyperVPCIPacket) + sizeof(HyperVPCIBusD0Entry));
  return ret;
}
