#ifndef HEADER_PIPELINE_SEEKER
#define HEADER_PIPELINE_SEEKER

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Msg.h>

namespace OpenHome {
namespace Media {

/*
Element which facilitates seeking within a track
If the track is playing when Seek() is called, it is first ramped down
...once ramped down (or immediately if the pipeline is paused), ISeeker::TrySeek() is called
...if TrySeek returns an invalid flush id, the seek wasn't possible
...if TrySeek returns a valid flush id, data is pulled/discarded until we pull a flush with the target id
...the track is ramped up when we restart playing
Calls to Seek() are ignored if a previous seek is in progress
If TrySeek returned a valid flush id, the MsgFlush with this id is consumed
*/
    
// FIXME - move to somewhere visible to CodecController
class ISeeker
{
public:
    virtual TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint aSecondsAbsolute) = 0;
};

class Seeker : public IPipelineElementUpstream, private IMsgProcessor
{
    friend class SuiteSeeker;
public:
    Seeker(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, ISeeker& aSeeker, TUint aRampDuration);
    virtual ~Seeker();
    TBool Seek(TUint aTrackId, TUint aStreamId, TUint aSecondsAbsolute, TBool aRampDown);
public: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgDecodedStream* aMsg);
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgEncodedStream* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
private:
    void DoSeek();
    Msg* ProcessFlushable(Msg* aMsg);
    Msg* ProcessAudio(MsgAudio* aMsg);
    void NewStream();
private:
    enum EState
    {
        ERunning
       ,ERampingDown
       ,ERampingUp
       ,EFlushing
    };
private:
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstreamElement;
    ISeeker& iSeeker;
    Mutex iLock;
    EState iState;
    const TUint iRampDuration;
    TUint iRemainingRampSize;
    TUint iCurrentRampValue;
    TUint iSeekSeconds;
    MsgQueue iQueue; // empty unless we have to split a msg during a ramp
    TUint iTargetFlushId;
    TUint iTrackId;
    TUint iStreamId;
    IStreamHandler* iStreamHandler;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_SEEKER