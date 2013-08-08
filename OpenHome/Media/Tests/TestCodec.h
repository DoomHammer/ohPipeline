#ifndef HEADER_TESTCODEC
#define HEADER_TESTCODEC

#include <OpenHome/Media/Msg.h>
#include <OpenHome/Private/SuiteUnitTest.h>

namespace OpenHome {
    class Uri;
    class Environment;

namespace Media {
    class EncodedAudioReservoir;
    class ProtocolManager;
    class Supply;

namespace Codec {
    class Container;
    class CodecController;

class AudioFileDescriptor
{
public:
    enum ECodec
    {
        eCodecUnknown = 0,
        eCodecWav = 1,
        eCodecFlac = 2,
        eCodecAlac = 3,
        eCodecAac = 4,
        eCodecVorbis = 5,
    };
public:
    AudioFileDescriptor(const Brx& aFilename, TUint aSampleRate, TUint aSamples, TUint aBitDepth, TUint aChannels, ECodec aCodec);
    const Brx& Filename() const;
    TUint SampleRate() const;
    TUint Samples() const;
    TUint64 Jiffies() const;
    TUint BitDepth() const;
    TUint Channels() const;
    ECodec Codec() const;
private:
    Brn iFilename;
    TUint iSampleRate;
    TUint iSamples;
    TUint iBitDepth;
    TUint iChannels;
    ECodec iCodec;
};

class AudioFileCollection
{
public:
    AudioFileCollection();
    AudioFileCollection(std::vector<AudioFileDescriptor> aReqFiles
                      , std::vector<AudioFileDescriptor> aExtraFiles
                      , std::vector<AudioFileDescriptor> aInvalidFiles
                      , std::vector<AudioFileDescriptor> aStreamOnlyFiles
                      );
    void AddRequiredFile(AudioFileDescriptor aFile);
    void AddExtraFile(AudioFileDescriptor aFile);
    void AddInvalidFile(AudioFileDescriptor aFile);
    void AddStreamOnlyFile(AudioFileDescriptor aFile);
    std::vector<AudioFileDescriptor>& RequiredFiles();
    std::vector<AudioFileDescriptor>& ExtraFiles();
    std::vector<AudioFileDescriptor>& InvalidFiles();
    std::vector<AudioFileDescriptor>& StreamOnlyFiles();
private:
    std::vector<AudioFileDescriptor> iReqFiles;
    std::vector<AudioFileDescriptor> iExtraFiles;
    std::vector<AudioFileDescriptor> iInvalidFiles;
    std::vector<AudioFileDescriptor> iStreamOnlyFiles;
};

class TestCodecInfoAggregator : public Av::IInfoAggregator
{
public:
    TestCodecInfoAggregator();
    virtual ~TestCodecInfoAggregator();
public: // from IInfoAggregator
    void Register(Av::IInfoProvider& aProvider, std::vector<Brn>& aSupportedQueries);
};

class TestCodecFlushIdProvider : public IFlushIdProvider
{
public:
    TestCodecFlushIdProvider();
    ~TestCodecFlushIdProvider();
public: // from IFlushIdProvider
    TUint NextFlushId();
private:
    TUint iFlushId;
};

class TestCodecFiller : public Thread, private IPipelineIdProvider
{
private:
    static const TUint kInvalidPipelineId = 0;
public:
    TestCodecFiller(Environment& aEnv, ISupply& aSupply, IFlushIdProvider& aFlushIdProvider, Av::IInfoAggregator& aInfoAggregator);
    ~TestCodecFiller();
    void Start(const Brx& aUrl);
    TUint TrackId();
    TUint StreamId();
private: // from Thread
    void Run();
private: // from IPipelineIdProvider
    TUint NextTrackId();
    TUint NextStreamId();
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
private:
    ProtocolManager* iProtocolManager;
    TrackFactory* iTrackFactory;
    ISupply& iSupply;
    Brn iUrl;
    TUint iNextTrackId;
    TUint iNextStreamId;
};

class TestCodecPipelineElementDownstream : public IPipelineElementDownstream, private INonCopyable
{
public:
    TestCodecPipelineElementDownstream(IMsgProcessor& aMsgProcessor);
    virtual ~TestCodecPipelineElementDownstream();
public: // from IPipelineElementDownstream
    void Push(Msg* aMsg);
private:
    IMsgProcessor& iMsgProcessor;
};

class TestCodecMinimalPipeline
{
private:
    static const TUint kEncodedReservoirSizeBytes = 12 * 1024; // in practice, this can be ~500kB, but it must be limited for seek testing
public:
    TestCodecMinimalPipeline(Environment& aEnv, IMsgProcessor& aMsgProcessor);
    virtual ~TestCodecMinimalPipeline();
    void StartPipeline();
    void StartStreaming(const Brx& aUrl);
    TBool SeekCurrentTrack(TUint aSecondsAbsolute);
protected:
    virtual void RegisterPlugins();
private:
    TestCodecInfoAggregator* iInfoAggregator;
    MsgFactory* iMsgFactory;
    TestCodecPipelineElementDownstream* iElementDownstream;
    EncodedAudioReservoir* iReservoir;
    Container* iContainer;
    CodecController* iController;
    Supply* iSupply;
    TestCodecFlushIdProvider* iFlushIdProvider;
    TestCodecFiller* iFiller;
};

typedef TestCodecMinimalPipeline* (*CreateTestCodecPipelineFunc)(Environment& aEnv, IMsgProcessor& aMsgProcessor);
typedef AudioFileCollection* (*GetTestFiles)();

class MsgProcessor : public IMsgProcessor, public INonCopyable
{
public:
    MsgProcessor(Semaphore& aSem);
    ~MsgProcessor();
public: // from IMsgProcessor
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
    Semaphore& iSem;
};

class SuiteCodecStream : public TestFramework::SuiteUnitTest, public MsgProcessor
{
public:
    static const Brn kPrefixHttp;
    static const TUint kLenPrefixHttp;
    static const TUint kMaxUriBytes;
public:
    SuiteCodecStream(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
protected:
    SuiteCodecStream(const TChar* aSuiteName, std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
    ~SuiteCodecStream();
    Brx* StartStreaming(const Brx& aTestName, const Brx& aFilename);
private:
    void TestJiffies();
protected: // from SuiteUnitTest
    virtual void Setup();
    virtual void TearDown();
public: // from MsgProcessor
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
protected:
    TUint64 iJiffies;
    Environment& iEnv;
    const Uri& iUri;
    Bwh iFileLocation;
    Semaphore iSem;
    TestCodecMinimalPipeline* iPipeline;
    std::vector<AudioFileDescriptor>& iFiles;
    TUint iFileNum;
private:
    CreateTestCodecPipelineFunc iCreatePipeline;
protected:
    static const TUint kMaxFilenameLen = 100;    // max filename length
    static const TUint kDuration = 10;          // Test file duration (in seconds).
    static const TUint kTotalJiffies = kDuration * Jiffies::kJiffiesPerSecond;
    static const TUint kFrequencyHz = 1000;
};

class SuiteCodecSeek : public SuiteCodecStream
{
public:
    SuiteCodecSeek(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
protected:
    SuiteCodecSeek(const TChar* aSuiteName, std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
    ~SuiteCodecSeek();
    static TUint64 ExpectedJiffies(TUint aDuration, TUint aSeekInit, TUint aSeekPos);
private: // from SuiteUnitTest
    void Setup();
public: // from MsgProcessor
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
private:
    void TestSeeking(TUint aDuration, TUint aSeekPos, AudioFileDescriptor::ECodec aCodec);
    void TestSeekingToStart();
    void TestSeekingToEnd();
    void TestSeekingBackwards();
    void TestSeekingForwards();
protected:
    TBool iSeek;
    TUint iSeekPos;
    TBool iSeekSuccess;
private:
    TUint iFileNumStart;
    TUint iFileNumEnd;
    TUint iFileNumBack;
    TUint iFileNumForward;
};

class SuiteCodecSeekFromStart : public SuiteCodecSeek
{
public:
    SuiteCodecSeekFromStart(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
private:
    ~SuiteCodecSeekFromStart();
    void TestSeekingFromStart(TUint aDuration, TUint aSeekPos, AudioFileDescriptor::ECodec aCodec);
    void TestSeekingToMiddle();
    void TestSeekingToEnd();
public: // from MsgProcessor
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
private:
    TUint iFileNumMiddle;
    TUint iFileNumEnd;
};

class SuiteCodecZeroCrossings : public SuiteCodecStream
{
public:
    SuiteCodecZeroCrossings(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
private:
    ~SuiteCodecZeroCrossings();
    void TestCrossingDelta();
    Msg* TestSimilarity(MsgAudioPcm* aMsg);
    void TestZeroCrossings();
private: // from SuiteUnitTest
    void Setup();
public: // from MsgProcessor
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
private:
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iChannels;
    TUint iBytesProcessed;
    TUint iLastCrossingByte;
    TUint iZeroCrossings;
    TUint iUnacceptableCrossingDeltas;
    AudioFileDescriptor::ECodec iCodec;
};

class SuiteCodecInvalidType : public SuiteCodecStream
{
public:
    SuiteCodecInvalidType(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri);
protected:
    ~SuiteCodecInvalidType();
private:
    void TestInvalidType();
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

#endif // HEADER_TESTCODEC