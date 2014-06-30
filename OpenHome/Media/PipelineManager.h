#ifndef HEADER_PIPELINE_MANAGER
#define HEADER_PIPELINE_MANAGER

#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Private/Thread.h>

#include <vector>

namespace OpenHome {
namespace Av {
    class IInfoAggregator;
}
namespace Media {
class Pipeline;
class ProtocolManager;
class ITrackObserver;
class Filler;
class IdManager;
namespace Codec {
    class CodecBase;
}
class Protocol;
class ContentProcessor;
class UriProvider;

/**
 * External interface to the pipeline.
 */
class PipelineManager : public IPipelineElementUpstream, public IPipelineIdManager, private IPipelineObserver
{
public:
    PipelineManager(Av::IInfoAggregator& aInfoAggregator, TrackFactory& aTrackFactory, TUint aDriverMaxAudioBytes); // FIXME - config options
    ~PipelineManager();
    /**
     * Signal that the pipeline should quit.
     *
     * Normal shutdown order is
     *    Call Quit()
     *    Wait until Pull() returns a MsgQuit
     *    delete PipelineManager
     */
    void Quit();
    /**
     * Add a codec to the pipeline.
     *
     * There should only be a single instance of each codec aded.
     * Must be called before Start().
     *
     * @param[in] aCodec           Ownership transfers to PipelineManager.
     */
    void Add(Codec::CodecBase* aCodec);
    /**
     * Add a protocol to the pipeline.
     *
     * Multiple instances of a protocol may be added.
     * Must be called before Start().
     *
     * @param[in] aProtocol        Ownership transfers to PipelineManager.
     */
    void Add(Protocol* aProtocol);
    /**
     * Add a content processor to the pipeline.
     *
     * Typically only used by the Radio source (so may be added by it)
     * Must be called before Start().
     *
     * @param[in] aContentProcessor   Ownership transfers to PipelineManager.
     */
    void Add(ContentProcessor* aContentProcessor);
    /**
     * Add a uri provider to the pipeline.
     *
     * Must be called before Start().
     * Will typically be called during construction of a source so need not be called
     * directly by application code.
     *
     * @param[in] aUriProvider     Ownership transfers to PipelineManager.
     */
    void Add(UriProvider* aUriProvider);
    /**
     * Signal that all plug-ins have been Add()ed and the pipeline is ready to receive audio.
     *
     * Begin() can only be called after Start() returns.
     */
    void Start();
    /**
     * Add an observer of changes in pipeline state.
     *
     * Should be called before Start().
     *
     * @param[in] aObserver        Observer.  Ownership remains with caller.
     */
    void AddObserver(IPipelineObserver& aObserver);
    /**
     * Remove an observer.
     *
     * Can be called at any time.  Can be called even if AddObserver() was not called.
     * Callbacks may be run while this call is in progress.  No more callbacks will be
     * received after this completes.
     *
     * @param[in] aObserver        Previously added observer.
     */
    void RemoveObserver(IPipelineObserver& aObserver);
    void AddObserver(ITrackObserver& aObserver);
    /**
     * Instruct the pipeline what should be streamed next.
     *
     * Several other tracks may already exist in the pipeline.  Call Stop() or
     * RemoveAll() before this to control what is played next.
     *
     * @param[in] aMode            Identifier for the UriProvider
     * @param[in] aTrackId         Identifier of track to be played (Id() from Track class,
     *                             not pipelineTrackId as reported by pipeline).
     */
    void Begin(const Brx& aMode, TUint aTrackId);
    /**
     * Play the pipeline.
     *
     * Pipeline state will move to either EPipelinePlaying or EPipelineBuffering.
     * Begin() must have been called more recently than Stop() or RemoveAll() for audio
     * to be played.
     */
    void Play();
    /**
     * Pause the pipeline.
     *
     * If the pipeline is playing, the current track will ramp down.  Calling Play()
     * will restart playback at the same point.
     * Pipeline state will then change to EPipelinePaused.
     */
    void Pause();
    /**
     * Warn of a (planned) pending discontinuity in audio.
     *
     * Tell the pipeline to ramp down then discard audio until it pulls a MsgFlush with
     * identifier aFlushId.  Pipeline state will then move into EPipelineWaiting.
     */
    void Wait(TUint aFlushId);
    /**
     * Stop the pipeline.
     *
     * Stop playing any current track.  Don't play any pending tracks already in the
     * pipeline.  Don't add any new tracks to the pipeline.  Pipeline state will move
     * to EPipelineStopped.
     */
    void Stop();
    /**
     * Remove all current pipeline content, fetch but don't play a new track.
     *
     * Stops any current track (ramping down if necessary) then invalidates any pending
     * tracks.  Begins to fetch a new track.  Metadata describing the track will be
     * reported to observers but the track will not start playing.
     * This allows UIs to show what will be played next and can be useful e.g. when switching sources.
     *
     * @param[in] aMode            Identifier for the UriProvider
     * @param[in] aTrackId         Identifier of track to be played (Id() from Track class,
     *                             not pipelineTrackId as reported by pipeline).
     */
    void StopPrefetch(const Brx& aMode, TUint aTrackId);
    /**
     * Remove all pipeline content.  Prevent new content from being added.
     *
     * Use Begin() to select what should be played next.
     */
    void RemoveAll();
    /**
     * Seek to a specified point inside the current track.
     *
     * @param[in] aPipelineTrackId Track identifier.
     * @param[in] aStreamId        Stream identifier.
     * @param[in] aSecondsAbsolute Number of seconds into the track to seek to.
     *
     * @return  true if the seek succeeded; false otherwise.
     */
    TBool Seek(TUint aPipelineTrackId, TUint aStreamId, TUint aSecondsAbsolute);
    /**
     * Move immediately to the next track from the current UriProvider (or Source).
     *
     * Ramps down, removes the rest of the current track then fetches the track that
     * logically follows it.  The caller is responsible for calling Play() to start
     * playing this new track.
     *
     * @return  true if a track is being fetched; false if no other track exists or
     *          we were playing the last track in the UriProvider's list.
     */
    TBool Next();
    /**
     * Move immediately to the previous track from the current UriProvider (or Source).
     *
     * Ramps down, removes the rest of the current track then fetches the track that
     * logically precedes it.  The caller is responsible for calling Play() to start
     * playing this new track.
     *
     * @return  true if a track is being fetched; false if no other track exists or
     *          we were playing the first track in the UriProvider's list.
     */
    TBool Prev();
    TBool SupportsMimeType(const Brx& aMimeType); // can only usefully be called after codecs have been added
    IPipelineElementDownstream* SetSender(IPipelineElementDownstream& aSender);
    TUint SenderMinLatencyMs() const;
private: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IPipelineIdManager
    void InvalidateAt(TUint aId);
    void InvalidateAfter(TUint aId);
    void InvalidatePending();
    void InvalidateAll();
private: // from IPipelineObserver
    void NotifyPipelineState(EPipelineState aState);
    void NotifyTrack(Track& aTrack, const Brx& aMode, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo);
private:
    class PrefetchObserver : public IStreamPlayObserver
    {
    public:
        PrefetchObserver();
        ~PrefetchObserver();
        void Quit();
        void SetTrack(TUint aTrackId);
        void Wait(TUint aTimeoutMs);
    private: // from IStreamPlayObserver
        void NotifyTrackFailed(TUint aTrackId);
        void NotifyStreamPlayStatus(TUint aTrackId, TUint aStreamId, EStreamPlay aStatus);
    private:
        void CheckTrack(TUint aTrackId);
    private:
        Mutex iLock;
        Semaphore iSem;
        TUint iTrackId;
    };
private:
    Mutex iLock;
    Mutex iPublicLock;
    Pipeline* iPipeline;
    ProtocolManager* iProtocolManager;
    Filler* iFiller;
    IdManager* iIdManager;
    std::vector<UriProvider*> iUriProviders;
    std::vector<IPipelineObserver*> iObservers;
    EPipelineState iPipelineState;
    Semaphore iPipelineStoppedSem;
    BwsMode iMode;
    TUint iTrackId;
    PrefetchObserver iPrefetchObserver;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_MANAGER
