#ifndef HEADER_PIPELINE_ID_PROVIDER
#define HEADER_PIPELINE_ID_PROVIDER

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {

class IdManager : public IPipelineIdManager, public IPipelineIdProvider, public IPipelineIdTracker
{
    static const TUint kMaxActiveStreams = 100;
public:
    IdManager(IStopper& aStopper);
    TUint MaxStreams() const;
public: // from IPipelineIdTracker
    void AddStream(TUint aId, TUint aPipelineTrackId, TUint aStreamId, TBool aPlayNow);
public: // from IPipelineIdManager
    void InvalidateAt(TUint aId);
    void InvalidateAfter(TUint aId);
    void InvalidatePending();
    void InvalidateAll();
private:
    static inline void UpdateIndex(TUint& aIndex);
    TUint UpdateId(TUint& aId);
    void Log(const TChar* aPrefix);
private: // from IIdManager
    TUint NextTrackId();
    TUint NextStreamId();
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
private:
    class ActiveStream : public INonCopyable
    {
    public:
        ActiveStream();
        void Set(TUint aId, TUint aPipelineTrackId, TUint aStreamId, TBool aPlayNow);
        void Set(const ActiveStream& aActiveStream);
        void Clear();
        TBool IsClear() const { return iClear; }
        TUint Id() const { return iId; }
        TUint PipelineTrackId() const { return iPipelineTrackId; }
        TUint StreamId() const { return iStreamId; }
        TBool PlayNow() const { return iPlayNow; }
    private:
        TUint iId;
        TUint iPipelineTrackId;
        TUint iStreamId;
        TBool iPlayNow;
        TBool iClear;
    };
private:
    Mutex iLock;
    IStopper& iStopper;
    TUint iNextTrackId;
    TUint iNextStreamId;
    ActiveStream iActiveStreams[kMaxActiveStreams];
    TUint iIndexHead;
    TUint iIndexTail;
    ActiveStream iPlaying;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_ID_PROVIDER
