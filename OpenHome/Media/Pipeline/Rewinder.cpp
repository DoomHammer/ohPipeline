#include <OpenHome/Media/Pipeline/Rewinder.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>

#include <limits.h>

namespace OpenHome {
namespace Media {

class MsgCloner : private IMsgProcessor
{
public:
    static Msg* NewRef(Msg& aMsg);
private:
    MsgCloner();
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgSession* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgChangeInput* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
};

/**
 * A class that examines buffered messages from the Rewinder for whether
 * buffering should start again after Stop() has been called.
 *
 * E.g., consider case of 3 consecutive MsgEncodedStreams:
 * - 1st MsgEncodedStream will be passed on and not buffered.
 * - 2nd and 3rd MsgEncodedStreams will be buffered.
 * - When Rewind() and Stop() are called and the stream is pulled again, the
 *   Rewinder cannot identify that the 2nd MsgEncodedStream should now be
 *   removed from the buffer after being passed on.
 *
 * This class eradicates the above problem by having its static function return
 * true if the Rewinder should resume buffering messages after the current message.
 *
 * This allows the Rewinder to operate as follows:
 * - After a Rewind(), the Rewinder knows it is in a buffering state, and
 *   should continue re-buffering all previously buffered messages.
 * - After a Stop(), the Rewinder should dispose of all buffered messages until
 *   after it encounters a MsgEncodedStream. This class provides the necessary
 *   functionality to allow the Rewinder to do that.
 */
class RewinderBufferProcessor : private IMsgProcessor
{
public:
    static TBool ShouldStartBuffering(Msg* aMsg);
private:
    RewinderBufferProcessor();
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgSession* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgChangeInput* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
};

class RewinderReservoir : public MsgReservoir
{
public:
    RewinderReservoir(TUint aMaxEncodedAudio);
    void Enqueue(Msg* aMsg);
    Msg* Dequeue();
    TBool IsEmpty() const;
    TBool IsFull() const;
private:
    const TUint iMaxEncodedAudio;
};

} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;

// MsgCloner

Msg* MsgCloner::NewRef(Msg& aMsg)
{ // static
    MsgCloner self;
    return aMsg.Process(self);
}

MsgCloner::MsgCloner()
{
}

Msg* MsgCloner::ProcessMsg(MsgMode* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgSession* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgTrack* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgChangeInput* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgDelay* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgEncodedStream* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgAudioEncoded* aMsg)
{
    return aMsg->Clone();
}

Msg* MsgCloner::ProcessMsg(MsgMetaText* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgHalt* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgFlush* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgWait* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* MsgCloner::ProcessMsg(MsgDecodedStream* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* MsgCloner::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* MsgCloner::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* MsgCloner::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* MsgCloner::ProcessMsg(MsgQuit* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}


// RewinderBufferProcessor

TBool RewinderBufferProcessor::ShouldStartBuffering(Msg* aMsg)
{
    RewinderBufferProcessor self;
    Msg* msg = aMsg->Process(self);    // this won't modify aMsg
    if (msg != NULL) {
        return true;
    }
    return false;
}

RewinderBufferProcessor::RewinderBufferProcessor()
{
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgMode* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgSession* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgTrack* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgChangeInput* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgDelay* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgEncodedStream* aMsg)
{
    return aMsg;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS(); // shouldn't have been buffered
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgStreamInterrupted* /*aMsg*/)
{
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgHalt* /*aMsg*/)
{
    ASSERTS(); // shouldn't have been buffered
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS(); // shouldn't have been buffered
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS(); // shouldn't have been buffered
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgDecodedStream* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* RewinderBufferProcessor::ProcessMsg(MsgQuit* /*aMsg*/)
{
    ASSERTS(); // shouldn't have been buffered
    return NULL;
}


// RewinderReservoir

RewinderReservoir::RewinderReservoir(TUint aMaxEncodedAudio)
    : iMaxEncodedAudio(aMaxEncodedAudio)
{
}

void RewinderReservoir::Enqueue(Msg* aMsg)
{
    DoEnqueue(aMsg);
}

Msg* RewinderReservoir::Dequeue()
{
    return DoDequeue();
}

TBool RewinderReservoir::IsEmpty() const
{
    return MsgReservoir::IsEmpty();
}

TBool RewinderReservoir::IsFull() const
{
    if (EncodedAudioCount() < iMaxEncodedAudio) {
        return false;
    }
    return true;
}


// Rewinder

Rewinder::Rewinder(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement)
    : iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iStreamHandler(NULL)
    , iBuffering(false)
    , iLock("REWI")
{
    iQueueCurrent = new RewinderReservoir(kMaxEncodedAudioMsgs);
    iQueueNext = new RewinderReservoir(kMaxEncodedAudioMsgs);
}

Rewinder::~Rewinder()
{
    delete iQueueCurrent;
    delete iQueueNext;
}

void Rewinder::TryBuffer(Msg* aMsg)
{
    if (iBuffering) {
        Msg* copy = MsgCloner::NewRef(*aMsg);
        iQueueNext->Enqueue(copy);
    }
}

Msg* Rewinder::Pull()
{
    Msg* msg = NULL;
    do {
        {
            AutoMutex _(iLock);
            if (iBuffering && iQueueNext->IsFull()) {
                return NULL;
            }
            if (!iQueueCurrent->IsEmpty()) {
                msg = iQueueCurrent->Dequeue();
                if (iBuffering) {
                    TryBuffer(msg);
                }
                else if (RewinderBufferProcessor::ShouldStartBuffering(msg)) {
                    // Don't want to re-buffer msgs until after a MsgEncodedStream
                    // is retrieved after Stop() (i.e., iBuffering = false) has been
                    // called.
                    // Otherwise, the previously buffered MsgEncodedStream will be
                    // re-buffered and pulled after each Rewind().
                    iBuffering = true;
                }
            }
        }
        if (msg == NULL) {
            msg = iUpstreamElement.Pull();
            if (msg != NULL) {
                iLock.Wait();
                msg = msg->Process(*this);
                iLock.Signal();
            }
        }
    } while (msg == NULL);
    return msg;
}

Msg* Rewinder::ProcessMsg(MsgMode* aMsg)
{
    TryBuffer(aMsg);
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgSession* aMsg)
{
    TryBuffer(aMsg);
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgTrack* aMsg)
{
    TryBuffer(aMsg);
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgChangeInput* aMsg)
{
    TryBuffer(aMsg);
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgDelay* aMsg)
{
    TryBuffer(aMsg);
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgEncodedStream* aMsg)
{
    iStreamHandler = aMsg->StreamHandler();
    MsgEncodedStream* msg = NULL;
    if (aMsg->RawPcm()) {
        msg = iMsgFactory.CreateMsgEncodedStream(aMsg->Uri(), aMsg->MetaText(), aMsg->TotalBytes(), aMsg->StreamId(), aMsg->Seekable(), aMsg->Live(), this, aMsg->PcmStream());
    }
    else {
        msg = iMsgFactory.CreateMsgEncodedStream(aMsg->Uri(), aMsg->MetaText(), aMsg->TotalBytes(), aMsg->StreamId(), aMsg->Seekable(), aMsg->Live(), this);
    }
    aMsg->RemoveRef();
    TryBuffer(msg);
    iBuffering = true;
    return msg;
}

Msg* Rewinder::ProcessMsg(MsgAudioEncoded* aMsg)
{
    TryBuffer(aMsg);
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgMetaText* aMsg)
{
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgFlush* aMsg)
{
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* Rewinder::ProcessMsg(MsgDecodedStream* /*aMsg*/)
{
    ASSERTS(); // expect this Msg to be generated by a downstream decoder element
    return NULL;
}

Msg* Rewinder::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Rewinder::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Rewinder::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // only expect encoded audio at this stage of the pipeline
    return NULL;
}

Msg* Rewinder::ProcessMsg(MsgQuit* aMsg)
{
    return aMsg;
}

void Rewinder::Rewind()
{
    AutoMutex a(iLock);
    ASSERT(iBuffering == true);

    while (!iQueueCurrent->IsEmpty()) {
        iQueueNext->Enqueue(iQueueCurrent->Dequeue());
    }
    auto tmpQueue = iQueueCurrent;
    iQueueCurrent = iQueueNext;
    iQueueNext = tmpQueue;
}

void Rewinder::Stop()
{
    // Discard any msgs that have been re-buffered - don't want to see them again.
    AutoMutex a(iLock);
    ASSERT(iBuffering);
    while (!iQueueNext->IsEmpty()) {
        iQueueNext->Dequeue()->RemoveRef();
    }
    iBuffering = false;
}

EStreamPlay Rewinder::OkToPlay(TUint aStreamId)
{
    ASSERT(iStreamHandler != NULL);
    EStreamPlay canPlay = iStreamHandler->OkToPlay(aStreamId);
    //Log::Print("Rewinder::OkToPlay(%u) returned %s\n", aStreamId, kStreamPlayNames[canPlay]);
    return canPlay;
}

TUint Rewinder::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    AutoMutex a(iLock);
    return iStreamHandler->TrySeek(aStreamId, aOffset);
}

TUint Rewinder::TryStop(TUint aStreamId)
{
    AutoMutex a(iLock);
    return iStreamHandler->TryStop(aStreamId);
}

void Rewinder::NotifyStarving(const Brx& aMode, TUint aStreamId)
{
    AutoMutex a(iLock);
    if (iStreamHandler != NULL) {
        iStreamHandler->NotifyStarving(aMode, aStreamId);
    }
}
