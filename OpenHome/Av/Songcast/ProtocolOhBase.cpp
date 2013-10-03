#include <OpenHome/Av/Songcast/ProtocolOhBase.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Songcast/OhmMsg.h>
#include <OpenHome/Av/Songcast/Ohm.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/OsWrapper.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

ProtocolOhBase::ProtocolOhBase(Environment& aEnv, Media::TrackFactory& aTrackFactory, IOhmTimestamper& aTimestamper, const TChar* aSupportedScheme, const Brx& aMode)
    : Protocol(aEnv)
    , iMutexTransport("POHB")
    , iTrackFactory(aTrackFactory)
    , iTimestamper(aTimestamper)
    , iSupportedScheme(aSupportedScheme)
    , iMode(aMode)
    , iRunning(false)
    , iRepairing(false)
{
    iTimerRepair = new Timer(aEnv, MakeFunctor(*this, &ProtocolOhBase::TimerRepairExpired));
    iRepairFrames.reserve(kMaxRepairBacklogFrames);
}

ProtocolOhBase::~ProtocolOhBase()
{
    delete iTimerRepair;
}

void ProtocolOhBase::Add(OhmMsg* aMsg)
{
    aMsg->SetRxTimestamp(iTimestamper.Timestamp());
    AutoMutex a(iMutexTransport);
    aMsg->Process(*this);
}

void ProtocolOhBase::ResendSeen()
{
    iMutexTransport.Wait();
    if (iRepairing) {
        iTimerRepair->FireIn(kSubsequentRepairTimeoutMs);
    }
    iMutexTransport.Signal();
}

ProtocolStreamResult ProtocolOhBase::Stream(const Brx& aUri)
{
    // expects a uri of the form
    //    [ohu|ohm]://endpoint/songcast?&interface=[TUint as string]&ttl=[TUint as string]
    iUri.Replace(aUri);
    if (iUri.Scheme() != iSupportedScheme) {
        return EProtocolErrorNotSupported;
    }
    Endpoint ep(iUri.Port(), iUri.Host());
    Parser parser(iUri.Query());
    (void)parser.Next('&');
    Brn nif = parser.Next('&');
    Parser parser2(nif);
    if (parser2.Next('=') != Brn("interface")) {
        LOG2(kSongcast, kError, "Unexpected query fragment in ohm uri: ");
        LOG2(kSongcast, kError, nif);
        LOG2(kSongcast, kError, "\n");
        return EProtocolStreamErrorUnrecoverable;
    }
    const TUint addr = Ascii::Uint(parser2.Remaining());
    parser2.Set(parser.Remaining());
    if (parser2.Next('=') != Brn("ttl")) {
        LOG2(kSongcast, kError, "Unexpected query fragment in ohm uri: ");
        LOG2(kSongcast, kError, nif);
        LOG2(kSongcast, kError, "\n");
        return EProtocolStreamErrorUnrecoverable;
    }
    const TUint ttl = Ascii::Uint(parser2.Remaining());
    Play(addr, ttl, ep);
    return EProtocolStreamErrorUnrecoverable;
}

/*void ProtocolOhBase::Reset()
{
    iTimerRepair->Cancel();
    if (iRepairing) {
        iRepairFirst->RemoveRef();
        for (TUint i=0; i<iRepairFrames.size(); i++) {
            iRepairFrames[i]->RemoveRef();
        }
        iRepairFrames.clear();
        iRepairing = false;
    }
    iRunning = false;
}*/

TBool ProtocolOhBase::RepairBegin(OhmMsgAudioBlob& aMsg)
{
    LOG(kSongcast, "BEGIN ON %d\n", aMsg.Frame());
    iRepairFirst = &aMsg;
    iTimerRepair->FireIn(iEnv.Random(kInitialRepairTimeoutMs)); 
    return true;
}

void ProtocolOhBase::RepairReset()
{
    LOG(kSongcast, "RESET\n");
    iTimerRepair->Cancel();
    iRepairFirst->RemoveRef();
    iRepairFirst = NULL;
    for (TUint i=0; i<iRepairFrames.size(); i++) {
        iRepairFrames[i]->RemoveRef();
    }
    iRepairFrames.clear();
    iRunning = false;
}

TBool ProtocolOhBase::Repair(OhmMsgAudioBlob& aMsg)
{
    // get the incoming frame number
    const TUint frame = aMsg.Frame();
    LOG(kSongcast, "GOT %d\n", frame);

    // get difference between this and the last frame sent down the pipeline
    TInt diff = frame - iFrame;
    if (diff < 1) {
        // incoming frames is equal to or earlier than the last frame sent down the pipeline
        // in other words, it's a duplicate, so so discard it and continue
        aMsg.RemoveRef();
        return true;
    }
    if (diff == 1) {
        // incoming frame is one greater than the last frame sent down the pipeline, so send this ...
        iFrame++;
        OutputAudio(aMsg);
        // ... and see if the current first waiting frame is now also ready to be sent
        while (iRepairFirst->Frame() == iFrame + 1) {
            // ... yes, it is, so send it
            iFrame++;
            OutputAudio(*iRepairFirst);
            // ... and see if there are further messages waiting in the fifo
            if (iRepairFrames.size() == 0) {
                // ... no, so we have completed the repair
                iRepairFirst = NULL;
                LOG(kSongcast, "END\n");
                return false;
            }
            // ... yes, so update the current first waiting frame and continue testing to see if this can also be sent
            iRepairFirst = iRepairFrames[0];
            iRepairFrames.erase(iRepairFrames.begin());
        }
        // ... we're done
        return true;
    }

    // Ok, its a frame that needs to be put into the backlog, but where?
    // compare it to the current first waiting frame
    diff = frame - iRepairFirst->Frame();
    if (diff == 0) {
        // it's equal to the currently first waiting frame, so discard it - it's a duplicate
        aMsg.RemoveRef();
        return true;
    }
    if (diff < 0) {
        // it's earlier than the current first waiting message, so it should become the new current first waiting frame
        // and the old first waiting frame needs to be injected into the start of the backlog, so inject it into the end
        // and rotate the others (if there is space to add another frame)
        if (iRepairFrames.size() == kMaxRepairBacklogFrames) {
            // can't fit another frame into the backlog
            RepairReset();
            aMsg.RemoveRef();
            return false;
        }
        iRepairFrames.insert(iRepairFrames.begin(), iRepairFirst);
        iRepairFirst = &aMsg;
        return true;
    }
    // ok, it's after the currently first waiting frame, so it needs to go into the backlog
    // first check if the backlog is empty
    if (iRepairFrames.size() == 0) {
        // ... yes, so just inject it
        iRepairFrames.insert(iRepairFrames.begin(), &aMsg);
        return true;
    }
    // ok, so the backlog is not empty
    // is it a duplicate of the last frame in the backlog?
    if (diff == 0) {
        // ... yes, so discard
        aMsg.RemoveRef();
        return true;
    }
    // is the incoming frame later than the last one currently in the backlog?
    diff = frame - iRepairFrames[iRepairFrames.size()-1]->Frame();
    if (diff > 0) {
        // ... yes, so, again, just inject it (if there is space)
        if (iRepairFrames.size() == kMaxRepairBacklogFrames) {
            // can't fit another frame into the backlog
            RepairReset();
            aMsg.RemoveRef();
            return false;
        }
        iRepairFrames.push_back(&aMsg);
        return true;
    }
    // ... no, so it has to go somewhere in the middle of the backlog, so iterate through and inject it at the right place (if there is space)
    TUint count = iRepairFrames.size();
    for (std::vector<OhmMsgAudioBlob*>::iterator it = iRepairFrames.begin(); it != iRepairFrames.end(); ++it) {
        diff = frame - (*it)->Frame();
        if (diff > 0) {
            continue;
        }
        if (diff == 0) {
            aMsg.RemoveRef();
        }
        else {
            if (count == kMaxRepairBacklogFrames) {
                // can't fit another frame into the backlog
                aMsg.RemoveRef();
                RepairReset();
                return false;
            }
            iRepairFrames.insert(it, &aMsg);
        }
        break;
    }

    return true;
}

void ProtocolOhBase::TimerRepairExpired()
{
    iMutexTransport.Wait();
    if (iRepairing) {
        LOG(kSongcast, "REQUEST RESEND");
        Bws<kMaxRepairMissedFrames * 4> missed;
        WriterBuffer buffer(missed);
        WriterBinary writer(buffer);

        TUint count = 0;
        TUint start = iFrame + 1;
        TUint end = iRepairFirst->Frame();

        // phase 1 - request the frames between the last sent down the pipeline and the first waiting frame
        for (TUint i = start; i < end; i++) {
            writer.WriteUint32Be(i);
            LOG(kSongcast, " %d", i);
            if (++count == kMaxRepairMissedFrames) {
                break;
            }
        }

        // phase 2 - if there is room add the missing frames in the backlog
        for (TUint j = 0; count < kMaxRepairMissedFrames && j < iRepairFrames.size(); j++) {
            OhmMsgAudioBlob* msg = iRepairFrames[j];
            start = end + 1;
            end = msg->Frame();
            for (TUint i = start; i < end; i++) {
                writer.WriteUint32Be(i);
                LOG(kSongcast, " %d", i);
                if (++count == kMaxRepairMissedFrames) {
                    break;
                }
            }
        }
        LOG(kSongcast, "\n");

        RequestResend(missed);
        iTimerRepair->FireIn(kSubsequentRepairTimeoutMs);
    }

    iMutexTransport.Signal();
}

void ProtocolOhBase::OutputAudio(OhmMsgAudioBlob& aMsg)
{
    // FIXME - also need to OutputStream (probably when we infer a stream change, not just relying on following MsgTrack)
    WriterBuffer writer(iFrameBuf);
    aMsg.Externalise(writer);
    iSupply->OutputData(iFrameBuf);
    aMsg.RemoveRef();
}

void ProtocolOhBase::Process(OhmMsgAudio& /*aMsg*/)
{
    ASSERTS();
}

void ProtocolOhBase::Process(OhmMsgAudioBlob& aMsg)
{
    if (!iRunning) {
        iFrame = aMsg.Frame();
        iRunning = true;
        OutputAudio(aMsg);
        return;
    }
    
    if (iRepairing) {
        iRepairing = Repair(aMsg);
        return;
    }

    const TInt diff = aMsg.Frame() - iFrame;
    if (diff == 1) {
        iFrame++;
        OutputAudio(aMsg);
    }
    else if (diff < 1) {
        aMsg.RemoveRef();
    }
    else {
        iRepairing = RepairBegin(aMsg);
    }
}

void ProtocolOhBase::Process(OhmMsgTrack& aMsg)
{
    Track* track = iTrackFactory.CreateTrack(aMsg.Uri(), aMsg.Metadata(), NULL);
    iSupply->OutputTrack(*track, iIdProvider->NextTrackId(), iMode);
    track->RemoveRef();
    aMsg.RemoveRef();
    // FIXME - also need OutputStream (which is complicated by repair vector)
}

void ProtocolOhBase::Process(OhmMsgMetatext& aMsg)
{
    iSupply->OutputMetadata(aMsg.Metatext());
    aMsg.RemoveRef();
}


// DefaultTimestamper

DefaultTimestamper::DefaultTimestamper(Environment& aEnv)
    : iEnv(aEnv)
{
}

TUint DefaultTimestamper::Timestamp()
{
    return Os::TimeInMs(iEnv.OsCtx());
}
