#ifndef HEADER_PIPELINE_FILLER
#define HEADER_PIPELINE_FILLER

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/Protocol/Protocol.h>

#include <vector>

EXCEPTION(FillerInvalidStyle);
EXCEPTION(UriProviderInvalidId);

namespace OpenHome {
namespace Media {

class UriProvider
{
public:
    virtual ~UriProvider();
    const Brx& Style() const;
    virtual void Begin(const Brx& aProviderId) = 0;
    virtual EStreamPlay GetNext(Track*& aTrack) = 0;
    virtual TBool MoveCursorAfter(const Brx& aProviderId) = 0;
    virtual TBool MoveCursorBefore(const Brx& aProviderId) = 0;
protected:
    UriProvider(const TChar* aStyle);
private:
    BwsStyle iStyle;
};

class Filler : private Thread, public ISupply
{
public:
    Filler(ISupply& aSupply, IPipelineIdTracker& aPipelineIdTracker);
    ~Filler();
    void Add(UriProvider& aUriProvider);
    void Start(IUriStreamer& aUriStreamer);
    void Play(const Brx& aStyle, const Brx& aProviderId);
    void Stop();
    TBool Next(const Brx& aStyle, const Brx& aProviderId);
    TBool Prev(const Brx& aStyle, const Brx& aProviderId);
private: // from Thread
    void Run();
private: // from ISupply
    void OutputTrack(Track& aTrack, TUint aTrackId);
    void OutputStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, IStreamHandler& aStreamHandler, TUint aStreamId);
    void OutputData(const Brx& aData);
    void OutputMetadata(const Brx& aMetadata);
    void OutputFlush(TUint aFlushId);
    void OutputQuit();
private:
    Mutex iLock;
    ISupply& iSupply;
    IPipelineIdTracker& iPipelineIdTracker;
    std::vector<UriProvider*> iUriProviders;
    UriProvider* iActiveUriProvider;
    IUriStreamer* iUriStreamer;
    Track* iTrack;
    TUint iTrackId;
    TBool iStopped;
    TBool iGetPrevious;
    TBool iQuit;
    EStreamPlay iTrackPlayStatus;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_FILLER