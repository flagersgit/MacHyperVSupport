//
//  HyperVNetwork.cpp
//  Hyper-V network driver
//
//  Copyright © 2021 Goldfish64. All rights reserved.
//

#include "HyperVNetwork.hpp"

OSDefineMetaClassAndStructors(HyperVNetwork, super);

bool HyperVNetwork::start(IOService *provider) {
  bool     result = false;
  IOReturn status;

  //
  // Get parent VMBus device object.
  //
  _hvDevice = OSDynamicCast(HyperVVMBusDevice, provider);
  if (_hvDevice == nullptr) {
    HVSYSLOG("Provider is not HyperVVMBusDevice");
    return false;
  }
  _hvDevice->retain();

  HVCheckDebugArgs();
  HVDBGLOG("Initializing Hyper-V Synthetic Networking");

  if (HVCheckOffArg()) {
    HVSYSLOG("Disabling Hyper-V Synthetic Networking due to boot arg");
    OSSafeReleaseNULL(_hvDevice);
    return false;
  }

  if (!super::start(provider)) {
    HVSYSLOG("super::start() returned false");
    OSSafeReleaseNULL(_hvDevice);
    return false;
  }

  do {
    //
    // Install packet handlers.
    //
    status = _hvDevice->installPacketActions(this, OSMemberFunctionCast(HyperVVMBusDevice::PacketReadyAction, this, &HyperVNetwork::handlePacket), OSMemberFunctionCast(HyperVVMBusDevice::WakePacketAction, this, &HyperVNetwork::wakePacketHandler), kHyperVNetworkReceivePacketSize);
    if (status != kIOReturnSuccess) {
      HVSYSLOG("Failed to install packet handlers with status 0x%X", status);
      break;
    }

#if DEBUG
    _hvDevice->installTimerDebugPrintAction(this, OSMemberFunctionCast(HyperVVMBusDevice::TimerDebugAction, this, &HyperVNetwork::handleTimer));
#endif

    //
    // Open VMBus channel.
    //
    status = _hvDevice->openVMBusChannel(kHyperVNetworkRingBufferSize, kHyperVNetworkRingBufferSize, kHyperVNetworkMaximumTransId);
    if (status != kIOReturnSuccess) {
      HVSYSLOG("Failed to open VMBus channel with status 0x%X", status);
      break;
    }
    
    // TODO
    rndisLock = IOLockAlloc();
    connectNetwork();
    
    //
    // Attach and register network interface.
    //
    if (!attachInterface((IONetworkInterface **)&_ethInterface, false)) {
      HVSYSLOG("Failed to attach network interface");
      break;
    }
    _ethInterface->registerService();

    HVDBGLOG("Initialized Hyper-V Synthetic Networking");
    result = true;
  } while (false);

  if (!result) {
    stop(provider);
  }
  return result;
}

void HyperVNetwork::stop(IOService *provider) {
  HVDBGLOG("Stopping Hyper-V Synthetic Networking");
  
  if (_ethInterface != nullptr) {
    detachInterface(_ethInterface);
    OSSafeReleaseNULL(_ethInterface);
  }

  if (_hvDevice != nullptr) {
    _hvDevice->closeVMBusChannel();
    _hvDevice->uninstallPacketActions();
    OSSafeReleaseNULL(_hvDevice);
  }

  super::stop(provider);
}

IOReturn HyperVNetwork::getHardwareAddress(IOEthernetAddress *addrP) {
  *addrP = _ethAddress;
  return kIOReturnSuccess;
}

UInt32 HyperVNetwork::outputPacket(mbuf_t m, void *param) {
  IOReturn status;
  size_t   packetLength;
  UInt32   sendIndex;

  UInt8                     *rndisBuffer;
  HyperVNetworkRNDISMessage *rndisMsg;
  HyperVNetworkMessage      netMsg;

  //
  // Get next available send section.
  //
  sendIndex = getNextSendIndex();
  if (sendIndex == kHyperVNetworkRNDISSendSectionIndexInvalid) {
    HVSYSLOG("No more send sections available, unable to send packet");
    return kIOReturnOutputStall;
  }

  //
  // Create RNDIS data request used for transmitting packet.
  //
  packetLength = mbuf_pkthdr_len(m);
  rndisBuffer  = &_sendBuffer.buffer[_sendSectionSize * sendIndex];
  rndisMsg     = (HyperVNetworkRNDISMessage *)rndisBuffer;
  bzero(rndisMsg, sizeof (*rndisMsg));

  rndisMsg->header.type           = kHyperVNetworkRNDISMessageTypePacket;
  rndisMsg->dataPacket.dataOffset = sizeof (rndisMsg->dataPacket);
  rndisMsg->dataPacket.dataLength = (UInt32)packetLength;
  rndisMsg->header.length         = sizeof (rndisMsg->header) + sizeof (rndisMsg->dataPacket) + rndisMsg->dataPacket.dataLength;

  if (packetLength == 0 || rndisMsg->header.length > _sendSectionSize) {
    HVSYSLOG("Packet of %u bytes is too large or invalid, send section size is %u bytes", packetLength, _sendSectionSize);
    return kIOReturnOutputDropped;
  }

  //
  // Copy packet data to send section.
  //
  rndisBuffer += sizeof (rndisMsg->header) + rndisMsg->dataPacket.dataOffset;
  for (mbuf_t pktCurrent = m; pktCurrent != nullptr; pktCurrent = mbuf_next(pktCurrent)) {
    size_t pktCurrentLength = mbuf_len(pktCurrent);
    memcpy(rndisBuffer, mbuf_data(pktCurrent), pktCurrentLength);
    rndisBuffer += pktCurrentLength;
  }

  //
  // Create and send packet for sending the RNDIS data packet.
  //
  bzero(&netMsg, sizeof (netMsg));
  netMsg.messageType                               = kHyperVNetworkMessageTypeV1SendRNDISPacket;
  netMsg.v1.sendRNDISPacket.channelType            = kHyperVNetworkRNDISChannelTypeData;
  netMsg.v1.sendRNDISPacket.sendBufferSectionIndex = sendIndex;
  netMsg.v1.sendRNDISPacket.sendBufferSectionSize  = rndisMsg->header.length;

  HVDBGLOG("Preparing to send packet of %u bytes using send section %u/%u", rndisMsg->header.length, sendIndex, _sendSectionCount);
  status = _hvDevice->writeInbandPacketWithTransactionId(&netMsg, sizeof (netMsg), sendIndex | kHyperVNetworkSendTransIdBits, true);
  if (status != kIOReturnSuccess) {
    HVSYSLOG("Failed to send packet with status 0x%X", status);
    return kIOReturnOutputStall;
  }

  //
  // Packet is only to be freed on success.
  // Caller is responsible in all other cases.
  //
  if (param != nullptr) {
    if (*(bool *)param == false) {
      return kIOReturnOutputSuccess;
    }
  }
  freePacket(m);
  return kIOReturnOutputSuccess;
}

IOReturn HyperVNetwork::enable(IONetworkInterface *interface) {
  _isNetworkEnabled = true;
  return kIOReturnSuccess;
}

IOReturn HyperVNetwork::disable(IONetworkInterface *interface) {
  _isNetworkEnabled = false;
  return kIOReturnSuccess;
}

IOReturn HyperVNetwork::enable(IOKernelDebugger *debugger) {
  _isNetworkEnabled = true;
  return kIOReturnSuccess;
}

IOReturn HyperVNetwork::disable(IOKernelDebugger *debugger) {
  _isNetworkEnabled = false;
  return kIOReturnSuccess;
}

void HyperVNetwork::receivePacket(void *pkt, UInt32 *pktSize, UInt32 timeoutMS) {
  bool isReceived = false;
  UInt32 costTime = 0;
  while (!isReceived && costTime < timeoutMS) {
    if (!(_isNetworkEnabled && _isLinkUp)) {
      HVDBGLOG("Interface down; waiting");
      continue;
    }
    if (kdpReceiveMbuf != nullptr) {
      _hvDevice->triggerPacketAction();
      size_t pkl = mbuf_len(kdpReceiveMbuf);
      memcpy((UInt8 *)pkt, mbuf_data(kdpReceiveMbuf), pkl);
      
      if (isKdpPacket((UInt8 *)pkt, pkl)) {
        HVDBGLOG("Was a KDP packet");
        *pktSize = pkl;
        isReceived = true;
      }
    }
    if (!isReceived) {
      IODelay(10000);
      costTime += 10;
    }
  }
}

void HyperVNetwork::sendPacket(void *pkt, UInt32 pktSize) {
  if (!(_isNetworkEnabled && _isLinkUp)) {
    HVDBGLOG("Interface down; dropping packets");
    return;
  }

  if (pktSize > KDP_MAXPACKET) {
    HVDBGLOG("KDP packet too large");
    return;
  }
  
  bool shouldFree = false;
  memcpy(mbuf_data(kdpSendMbuf), pkt, pktSize);
  mbuf_setlen(kdpSendMbuf, pktSize);
  
  outputPacket(kdpSendMbuf, (void *)&shouldFree);
}
