#include <OpenHome/Av/Songcast/ProtocolOhu.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Av/Songcast/Ohm.h>
#include <OpenHome/Av/Songcast/OhmMsg.h>
#include <OpenHome/Av/Songcast/OhmSocket.h>
#include <OpenHome/Av/Songcast/ProtocolOhBase.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/PowerManager.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// ProtocolOhu

ProtocolOhu::ProtocolOhu(Environment& aEnv, IOhmMsgFactory& aMsgFactory, Media::TrackFactory& aTrackFactory, IOhmTimestamper& aTimestamper, const Brx& aMode, IPowerManager& aPowerManager)
    : ProtocolOhBase(aEnv, aMsgFactory, aTrackFactory, aTimestamper, "ohu", aMode)
    , iLeaveLock("POHU")
{
    aPowerManager.RegisterObserver(MakeFunctor(*this, &ProtocolOhu::EmergencyStop), kPowerPriorityLowest+1);
    iTimerLeave = new Timer(aEnv, MakeFunctor(*this, &ProtocolOhu::TimerLeaveExpired));
}

ProtocolOhu::~ProtocolOhu()
{
    delete iTimerLeave;
}

void ProtocolOhu::HandleAudio(const OhmHeader& aHeader)
{
    Broadcast(iMsgFactory.CreateAudioBlob(iReadBuffer, aHeader));

    AutoMutex a(iLeaveLock);
    if (iLeaving) {
        iTimerLeave->Cancel();
        SendLeave();
        iReadBuffer.ReadInterrupt();
    }
}

void ProtocolOhu::HandleTrack(const OhmHeader& aHeader)
{
    Broadcast(iMsgFactory.CreateTrack(iReadBuffer, aHeader));
}

void ProtocolOhu::HandleMetatext(const OhmHeader& aHeader)
{
    Broadcast(iMsgFactory.CreateMetatext(iReadBuffer, aHeader));
}

void ProtocolOhu::HandleSlave(const OhmHeader& aHeader)
{
    OhmHeaderSlave headerSlave;
    headerSlave.Internalise(iReadBuffer, aHeader);
    iSlaveCount = headerSlave.SlaveCount();

    ReaderBinary reader(iReadBuffer);
    for (TUint i = 0; i < iSlaveCount; i++) {
        TIpAddress address = reader.ReadUintLe(4);
        TUint port = reader.ReadUintBe(2);
        iSlaveList[i].SetAddress(address);
        iSlaveList[i].SetPort(port);
    }
}

void ProtocolOhu::Broadcast(OhmMsg* aMsg)
{
    if (iSlaveCount > 0) {
        WriterBuffer writer(iMessageBuffer);
        writer.Flush();
        aMsg->Externalise(writer);
        for (TUint i = 0; i < iSlaveCount; i++) {
            try {
                iSocket.Send(iMessageBuffer, iSlaveList[i]);
            }
            catch (NetworkError&) {
                Endpoint::EndpointBuf buf;
                iSlaveList[i].AppendEndpoint(buf);
                LOG(kError, "NetworkError in ProtocolOhu::Broadcast for slave %s\n", buf.Ptr());
            }
        }
    }

    Add(aMsg);
}

ProtocolStreamResult ProtocolOhu::Play(TIpAddress aInterface, TUint aTtl, const Endpoint& aEndpoint)
{
    iLeaving = false;
    iStopped = false;
    iSlaveCount = 0;
    iNextFlushId = MsgFlush::kIdInvalid;
    iEndpoint.Replace(aEndpoint);
    iSocket.OpenUnicast(aInterface, aTtl);
    do {
        try {
            OhmHeader header;
            SendJoin();

            // Phase 1, periodically send join until Track and Metatext have been received
            TBool joinComplete = false;
            TBool receivedTrack = false;
            TBool receivedMetatext = false;

            while (!joinComplete) {
                try {
                    header.Internalise(iReadBuffer);

                    switch (header.MsgType())
                    {
                    case OhmHeader::kMsgTypeJoin:
                    case OhmHeader::kMsgTypeListen:
                    case OhmHeader::kMsgTypeLeave:
                        break;
                    case OhmHeader::kMsgTypeAudio:
                        HandleAudio(header);
                        break;
                    case OhmHeader::kMsgTypeTrack:
                        HandleTrack(header);
                        receivedTrack = true;
                        joinComplete = receivedMetatext;
                        break;
                    case OhmHeader::kMsgTypeMetatext:
                        HandleMetatext(header);
                        receivedMetatext = true;
                        joinComplete = receivedTrack;
                        break;
                    case OhmHeader::kMsgTypeSlave:
                        HandleSlave(header);
                        break;
                    case OhmHeader::kMsgTypeResend:
                        ResendSeen();
                        break;
                    default:
                        ASSERTS();
                    }

                    iReadBuffer.ReadFlush();
                }
                catch (OhmError&) {
                }
            }
            
            iTimerJoin->Cancel();

            // Phase 2, periodically send listen if required
            iTimerListen->FireIn((kTimerListenTimeoutMs >> 2) - iEnv.Random(kTimerListenTimeoutMs >> 3)); // listen primary timeout
            for (;;) {
                try {
                    header.Internalise(iReadBuffer);

                    switch (header.MsgType())
                    {
                    case OhmHeader::kMsgTypeJoin:
                    case OhmHeader::kMsgTypeLeave:
                        break;
                    case OhmHeader::kMsgTypeListen:
                        iTimerListen->FireIn((kTimerListenTimeoutMs >> 1) - iEnv.Random(kTimerListenTimeoutMs >> 3)); // listen secondary timeout
                        break;
                    case OhmHeader::kMsgTypeAudio:
                        HandleAudio(header);
                        break;
                    case OhmHeader::kMsgTypeTrack:
                        HandleTrack(header);
                        break;
                    case OhmHeader::kMsgTypeMetatext:
                        HandleMetatext(header);
                        break;
                    case OhmHeader::kMsgTypeSlave:
                        HandleSlave(header);
                        break;
                    case OhmHeader::kMsgTypeResend:
                        ResendSeen();
                        break;
                    default:
                        ASSERTS();
                    }

                    iReadBuffer.ReadFlush();
                }
                catch (OhmError&) {
                }
            }
        }
        catch (ReaderError&) {
        }
    } while (!iStopped);
    
    iReadBuffer.ReadFlush();
    iLeaveLock.Wait();
    if (iLeaving) {
        iLeaving = false;
        SendLeave();
    }
    iLeaveLock.Signal();
    iTimerJoin->Cancel();
    iTimerListen->Cancel();
    iTimerLeave->Cancel();
    iSocket.Close();
    if (iNextFlushId != MsgFlush::kIdInvalid) {
        iSupply->OutputFlush(iNextFlushId);
    }
    return iStopped? EProtocolStreamStopped : EProtocolStreamErrorUnrecoverable;
}

TUint ProtocolOhu::TryStop(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    // omit tests of aTrackId, aStreamId.  Any request to Stop() should probably result in us breaking the stream
    iLeaveLock.Wait();
    iNextFlushId = iFlushIdProvider->NextFlushId();
    iStopped = true;
    iLeaving = true;
    iTimerLeave->FireIn(kTimerLeaveTimeoutMs);
    iLeaveLock.Signal();
    return iNextFlushId;
}

void ProtocolOhu::EmergencyStop()
{
    //iLeaveLock.Wait();
    //// FIXME - use of Send from TimerLeaveExpired isn't obviously threadsafe
    //iStopped = true;
    //iLeaving = true;
    //iLeaveLock.Signal();
    //TimerLeaveExpired();
}

void ProtocolOhu::SendLeave()
{
    Send(OhmHeader::kMsgTypeLeave);
}

void ProtocolOhu::TimerLeaveExpired()
{
    AutoMutex a (iLeaveLock);
    if (!iLeaving) {
        return;
    }
    iLeaving = false;
    SendLeave();
    iReadBuffer.ReadInterrupt();
}
