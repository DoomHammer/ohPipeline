#ifndef HEADER_PIPELINE_MANAGER
#define HEADER_PIPELINE_MANAGER

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Media/Supply.h>
#include <OpenHome/Media/EncodedAudioReservoir.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/DecodedAudioReservoir.h>
#include <OpenHome/Media/VariableDelay.h>
#include <OpenHome/Media/Stopper.h>
#include <OpenHome/Media/Reporter.h>
#include <OpenHome/Media/Splitter.h>
#include <OpenHome/Media/Logger.h>
#include <OpenHome/Media/StarvationMonitor.h>
#include <OpenHome/Media/PreDriver.h>
#include <OpenHome/Av/InfoProvider.h>

namespace OpenHome {
namespace Media {

enum EPipelineState
{
    EPipelinePlaying
   ,EPipelinePaused
   ,EPipelineStopped
   ,EPipelineBuffering
};

class IPipelineObserver
{
public:
    virtual void NotifyPipelineState(EPipelineState aState) = 0;
    virtual void NotifyTrack(const Brx& aUri, TUint aIdPipeline) = 0;
    virtual void NotifyMetaText(const Brx& aText) = 0;
    virtual void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds) = 0;
    virtual void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo) = 0;
};
    
class Pipeline : public ISupply, public IPipelineElementUpstream, public IFlushIdProvider, private IStopperObserver, private IPipelinePropertyObserver, private IStarvationMonitorObserver
{
    friend class SuitePipeline; // test code
    static const TUint kMsgCountEncodedAudio    = 512;
    static const TUint kMsgCountAudioEncoded    = 768;
    static const TUint kMsgCountDecodedAudio    = 512;
    static const TUint kMsgCountAudioPcm        = 768;
    static const TUint kMsgCountSilence         = 512;
    static const TUint kMsgCountPlayablePcm     = 1024;
    static const TUint kMsgCountPlayableSilence = 1024;
    static const TUint kMsgCountEncodedStream   = 20;
    static const TUint kMsgCountTrack           = 20;
    static const TUint kMsgCountDecodedStream   = 20;
    static const TUint kMsgCountMetaText        = 20;
    static const TUint kMsgCountHalt            = 20;
    static const TUint kMsgCountFlush           = 1;
    static const TUint kMsgCountQuit            = 1;

    static const TUint kEncodedReservoirSizeBytes            = 500 * 1024;
    static const TUint kDecodedReservoirSize                 = Jiffies::kJiffiesPerMs * 1000;
    static const TUint kVariableDelayRampDuration            = Jiffies::kJiffiesPerMs * 200;
    static const TUint kStopperRampDuration                  = Jiffies::kJiffiesPerMs * 500;
    static const TUint kStarvationMonitorNormalSize          = Jiffies::kJiffiesPerMs * 100;
    static const TUint kStarvationMonitorStarvationThreshold = Jiffies::kJiffiesPerMs * 50;
    static const TUint kStarvationMonitorGorgeSize           = Jiffies::kJiffiesPerMs * 1000;
    static const TUint kStarvationMonitorRampUpDuration      = Jiffies::kJiffiesPerMs * 100;
public:
    Pipeline(Av::IInfoAggregator& aInfoAggregator, IPipelineObserver& aObserver, TUint aDriverMaxAudioBytes);
    virtual ~Pipeline();
    void AddCodec(Codec::CodecBase* aCodec);
    void Start();
    MsgFactory& Factory();
    void Play();
    void Pause();
    void Stop();
    TBool Seek(TUint aTrackId, TUint aStreamId, TUint aSecondsAbsolute);
public: // from ISupply
    void OutputTrack(const Brx& aUri, TUint aTrackId);
    void OutputStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, IStreamHandler& aStreamHandler, TUint aStreamId);
    void OutputData(const Brx& aData);
    void OutputMetadata(const Brx& aMetadata);
    void OutputFlush(TUint aFlushId);
    void OutputQuit();
public: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IFlushIdProvider
    TUint NextFlushId();
private:
    void Quit();
    void NotifyStatus();
private: // from IStopperObserver
    void PipelineHalted();
    void PipelineFlushed();
private: // from IPipelinePropertyObserver
    void NotifyTrack(const Brx& aUri, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo);
private: // from IStarvationMonitorObserver
    void NotifyStarvationMonitorBuffering(TBool aBuffering);
private:
    enum EStatus
    {
        EPlaying
       ,EHalting
       ,EHalted
       ,EFlushing
       ,EFlushed
       ,EQuit
    };
private:
    IPipelineObserver& iObserver;
    Mutex iLock;
    MsgFactory* iMsgFactory;
    Supply* iSupply;
    Logger* iLoggerSupply;
    EncodedAudioReservoir* iEncodedAudioReservoir;
    Logger* iLoggerEncodedAudioReservoir;
    Codec::Container* iContainer;
    Logger* iLoggerContainer;
    Codec::CodecController* iCodecController;
    Logger* iLoggerCodecController;
    DecodedAudioReservoir* iDecodedAudioReservoir;
    Logger* iLoggerDecodedAudioReservoir;
    VariableDelay* iVariableDelay;
    Logger* iLoggerVariableDelay;
    Stopper* iStopper;
    Logger* iLoggerStopper;
    Reporter* iReporter;
    Logger* iLoggerReporter;
    Splitter* iSplitter;
    Logger* iLoggerSplitter;
    StarvationMonitor* iStarvationMonitor;
    Logger* iLoggerStarvationMonitor;
    PreDriver* iPreDriver;
    Logger* iLoggerPreDriver;
    IPipelineElementUpstream* iPipelineEnd;
    PipelineBranchNull iNullSongcaster; // FIXME - placeholder for real songcaster
    EStatus iStatus;
    EStatus iTargetStatus; // status at the end of a series of async operations
    TUint iHaltCompletedIgnoreCount;
    TUint iFlushCompletedIgnoreCount;
    TBool iBuffering;
    TBool iQuitting;
    TUint iNextFlushId;
};

class NullPipelineObserver : public IPipelineObserver // test helper
{
private: // from IPipelineObserver
    void NotifyPipelineState(EPipelineState aState);
    void NotifyTrack(const Brx& aUri, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo);
};

class LoggingPipelineObserver : public IPipelineObserver // test helper
{
private: // from IPipelineObserver
    void NotifyPipelineState(EPipelineState aState);
    void NotifyTrack(const Brx& aUri, TUint aIdPipeline);
    void NotifyMetaText(const Brx& aText);
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds);
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo);
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_MANAGER