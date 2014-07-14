#ifndef HEADER_TESTCODECCONTROLLER
#define HEADER_TESTCODECCONTROLLER

#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/InfoProvider.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>

#include <list>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

namespace OpenHome {
namespace Media {

class HelperCodecPassThrough : public CodecBase
{
public:
    HelperCodecPassThrough(TUint aReadBytes, TUint aChannels, TUint aSampleRate, TUint aBitDepth, EMediaDataEndian aEndianness);
private: // from CodecBase
    TBool SupportsMimeType(const Brx& aMimeType);
    TBool Recognise();
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();
private:
    const TUint iReadBytes;
    const TUint iChannels;
    const TUint iSampleRate;
    const TUint iBitDepth;
    const EMediaDataEndian iEndianness;
    Bwh iReadBuf;
    TUint64 iTrackOffset;
};

class SuiteCodecControllerBase : public SuiteUnitTest
                               , private IPipelineElementUpstream
                               , private IPipelineElementDownstream
                               , private IStreamHandler
                               , private IMsgProcessor
{
public:
    SuiteCodecControllerBase(const TChar* aName);
protected: // from SuiteUnitTest
    void Setup();
    void TearDown();
private: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IPipelineElementDownstream
    void Push(Msg* aMsg);
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TUint TryStop(TUint aTrackId, TUint aStreamId);
    TBool TryGet(IWriter& aWriter, TUint aTrackId, TUint aStreamId, TUint64 aOffset, TUint aBytes);
    void NotifyStarving(const Brx& aMode, TUint aTrackId, TUint aStreamId);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg);
    Msg* ProcessMsg(MsgSession* aMsg);
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgDelay* aMsg);
    Msg* ProcessMsg(MsgEncodedStream* aMsg);
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgWait* aMsg);
    Msg* ProcessMsg(MsgDecodedStream* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
protected:
    enum EMsgType
    {
        ENone
       ,EMsgMode
       ,EMsgSession
       ,EMsgTrack
       ,EMsgDelay
       ,EMsgEncodedStream
       ,EMsgMetaText
       ,EMsgDecodedStream
       ,EMsgAudioPcm
       ,EMsgSilence
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgQuit
    };
protected:
    void Queue(Msg* aMsg);
    void PullNext(EMsgType aExpectedMsg);
    void PullNext(EMsgType aExpectedMsg, TUint64 aExpectedJiffies);
    Msg* CreateTrack();
    Msg* CreateEncodedStream();
    MsgFlush* CreateFlush();
protected:
    static const TUint kMaxMsgBytes = 960;
    static const TUint kWavHeaderBytes = 44;
    static const TUint kSampleRate = 44100;
    static const TUint kNumChannels = 2;
    static const TUint kExpectedFlushId = 5;
    static const TUint kSemWaitMs = 5000;   // only required in case tests fail
    MsgFactory* iMsgFactory;
    CodecController* iController;
    Semaphore* iSemStop;
    TUint iTotalBytes;
    TUint64 iTrackOffset;
    TUint iTrackOffsetBytes;
    TUint64 iJiffies;
    TUint64 iMsgOffset;
    TUint iStopCount;
    TUint iTrackId;
    TUint iStreamId;
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    std::list<Msg*> iPendingMsgs;
    std::list<Msg*> iReceivedMsgs;
    Semaphore* iSemPending;
    Semaphore* iSemReceived;
    Mutex* iLockPending;
    Mutex* iLockReceived;
    EMsgType iLastReceivedMsg;
    TUint iNextTrackId;
    TUint iNextStreamId;
    TBool iSeekable;
};

class SuiteCodecControllerStream : public SuiteCodecControllerBase
                                 , public ISeekObserver
{
public:
    SuiteCodecControllerStream();
private: // from SuiteCodecControllerBase
    void Setup();
    void TearDown();
private: // from SuiteCodecControllerBase
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
private: // from ISeekObserver
    void NotifySeekComplete(TUint aHandle, TUint aFlushId);
private:
    Msg* CreateAudio(TBool aValidHeader, TUint aDataBytes);
    void TestStreamSuccessful();
    void TestRecognitionFail();
    void TestTruncatedStreamInRecognition();
    void TestNoDataAfterRecognition();
    void TestTruncatedStream();
    void TestTrackTrack();
    void TestTrackEncodedStreamTrack();
    void TestSeek();
private:
    Semaphore* iSemSeek;
    TUint iHandle;
    TUint iExpectedFlushId;
    TUint iFlushId;
};

class SuiteCodecControllerPcmSize : public SuiteCodecControllerBase
{
public:
    SuiteCodecControllerPcmSize();
private: // // from SuiteCodecControllerBase
    void Setup();
    void TearDown();
private:
    Msg* CreateAudio();
    void TestPcmIsExpectedSize();
private:
    static const TUint kBitsPerSample = 16;
    static const TUint kSamplesPerMsg = 16;
    static const TUint kAudioBytesPerMsg = 2*2*kSamplesPerMsg; // 16 bits (2 bytes) * 2 channels * kSamplesPerMsg
};

} // namespace Media
} // namespace OpenHome


// HelperCodecPassThrough

HelperCodecPassThrough::HelperCodecPassThrough(TUint aReadBytes, TUint aChannels, TUint aSampleRate, TUint aBitDepth, EMediaDataEndian aEndianness)
    : iReadBytes(aReadBytes)
    , iChannels(aChannels)
    , iSampleRate(aSampleRate)
    , iBitDepth(aBitDepth)
    , iEndianness(aEndianness)
    , iReadBuf(iReadBytes)
    , iTrackOffset(0)
{
}

TBool HelperCodecPassThrough::SupportsMimeType(const Brx& /*aMimeType*/)
{
    // CodecController passes through calls to this. Shouldn't happen in tests.
    ASSERTS();
    return false;
}

TBool HelperCodecPassThrough::Recognise()
{
    return true;    // always recognise
}

void HelperCodecPassThrough::StreamInitialise()
{
    iTrackOffset = 0;
}

void HelperCodecPassThrough::Process()
{
    if (iTrackOffset == 0) {
        iController->OutputDecodedStream(0, iBitDepth, iSampleRate, iChannels, Brn("PASS"), 0, 0, true);
    }

    iReadBuf.SetBytes(0);
    try {
        iController->Read(iReadBuf, iReadBytes);
    }
    catch (CodecStreamEnded&) {
        throw; // rethrow CodecStreamEnded
    }
    iTrackOffset += iController->OutputAudioPcm(iReadBuf, iChannels, iSampleRate, iBitDepth, iEndianness, iTrackOffset);
}

TBool HelperCodecPassThrough::TrySeek(TUint aStreamId, TUint64 aSample)
{
    iController->OutputDecodedStream(aStreamId, iBitDepth, iSampleRate, iChannels, Brn("PASS"), 0, aSample, true);
    return true;
}

void HelperCodecPassThrough::StreamCompleted()
{
}


// SuiteCodecControllerBase

SuiteCodecControllerBase::SuiteCodecControllerBase(const TChar* aName)
    : SuiteUnitTest(aName)
{
}

void SuiteCodecControllerBase::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 5);
    // Need so many (Msg)AudioEncoded because kMaxMsgBytes is currently 960, and msgs are queued in advance of being pulled for these tests.
    iMsgFactory = new MsgFactory(iInfoAggregator, 400, 400, 100, 100, 10, 50, 0, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1);
    iController = new CodecController(*iMsgFactory, *this, *this);
    iSemPending = new Semaphore("TCSP", 0);
    iSemReceived = new Semaphore("TCSR", 0);
    iSemStop = new Semaphore("TCSS", 0);
    iLockPending = new Mutex("TCMP");
    iLockReceived = new Mutex("TCMR");
    iTrackId = iStreamId = UINT_MAX;
    iNextTrackId = iNextStreamId = 0;
    iTotalBytes = iTrackOffsetBytes = 0;
    iTrackOffset = 0;
    iJiffies = 0;
    iMsgOffset = 0;
    iSeekable = true;
    iStopCount = 0;
}

void SuiteCodecControllerBase::TearDown()
{
    Queue(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);

    iLockPending->Wait();
    ASSERT(iPendingMsgs.size() == 0);
    iLockPending->Signal();
    iLockReceived->Wait();
    ASSERT(iReceivedMsgs.size() == 0);
    iLockReceived->Signal();

    delete iLockReceived;
    delete iLockPending;
    delete iSemStop;
    delete iSemReceived;
    delete iSemPending;
    delete iController;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteCodecControllerBase::Pull()
{
    // This is called from CodecController's own thread, so block until msg
    // available.

    iSemPending->Wait(kSemWaitMs);
    AutoMutex a(*iLockPending);
    ASSERT(iPendingMsgs.size() > 0);
    Msg* msg = iPendingMsgs.front();
    iPendingMsgs.pop_front();
    return msg;
}

void SuiteCodecControllerBase::Push(Msg* aMsg)
{
    iLockReceived->Wait();
    iReceivedMsgs.push_back(aMsg);
    iLockReceived->Signal();
    iSemReceived->Signal();
}

EStreamPlay SuiteCodecControllerBase::OkToPlay(TUint /*aTrackId*/, TUint /*aStreamId*/)
{
    ASSERTS();
    return ePlayNo;
}

TUint SuiteCodecControllerBase::TrySeek(TUint /*aTrackId*/, TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteCodecControllerBase::TryStop(TUint aTrackId, TUint aStreamId)
{
    iStopCount++;
    iSemStop->Signal();
    if (aTrackId == iTrackId && aStreamId == iStreamId) {
        return kExpectedFlushId;
    }
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TBool SuiteCodecControllerBase::TryGet(IWriter& /*aWriter*/, TUint /*aTrackId*/, TUint /*aStreamId*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    ASSERTS();
    return false;
}

void SuiteCodecControllerBase::NotifyStarving(const Brx& /*aMode*/, TUint /*aTrackId*/, TUint /*aStreamId*/)
{
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgMode* aMsg)
{
    iLastReceivedMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgSession* aMsg)
{
    iLastReceivedMsg = EMsgSession;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgTrack* aMsg)
{
    iLastReceivedMsg = EMsgTrack;
    iTrackId = aMsg->IdPipeline();
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgDelay* aMsg)
{
    iLastReceivedMsg = EMsgDelay;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastReceivedMsg = EMsgEncodedStream;
    iStreamId = aMsg->StreamId();
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgMetaText* aMsg)
{
    iLastReceivedMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgHalt* aMsg)
{
    iLastReceivedMsg = EMsgHalt;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgFlush* aMsg)
{
    iLastReceivedMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgWait* aMsg)
{
    iLastReceivedMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastReceivedMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastReceivedMsg = EMsgAudioPcm;
    iMsgOffset = aMsg->TrackOffset();
    iJiffies += aMsg->Jiffies();
    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufPacked pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    ASSERT(buf.Bytes() >= 4);   // check we have enough bytes to examine first
                                // and last subsamples before manipulating pointers
    const TByte* ptr = buf.Ptr();
    const TUint bytes = buf.Bytes();
    const TUint firstSubsample = (ptr[0]<<8) | ptr[1];
    TEST(firstSubsample == 0x7f7f);
    const TUint lastSubsample = (ptr[bytes-2]<<8) | ptr[bytes-1];
    TEST(lastSubsample == 0x7f7f);

    return playable;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgSilence* aMsg)
{
    iLastReceivedMsg = EMsgSilence;
    return aMsg;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* SuiteCodecControllerBase::ProcessMsg(MsgQuit* aMsg)
{
    iLastReceivedMsg = EMsgQuit;
    return aMsg;
}

void SuiteCodecControllerBase::Queue(Msg* aMsg)
{
    iLockPending->Wait();
    iPendingMsgs.push_back(aMsg);
    iLockPending->Signal();
    iSemPending->Signal();
}

void SuiteCodecControllerBase::PullNext(EMsgType aExpectedMsg)
{
    iSemReceived->Wait(kSemWaitMs);
    iLockReceived->Wait();
    ASSERT(iReceivedMsgs.size() > 0);
    Msg* msg = iReceivedMsgs.front();
    iReceivedMsgs.pop_front();
    iLockReceived->Signal();

    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastReceivedMsg == aExpectedMsg);
}

void SuiteCodecControllerBase::PullNext(EMsgType aExpectedMsg, TUint64 aExpectedJiffies)
{
    TUint64 jiffiesStart = iJiffies;
    PullNext(aExpectedMsg);
    TUint64 jiffiesDiff = iJiffies - jiffiesStart;
    //Log::Print("jiffiesDiff: %llu, aExpectedJiffies: %llu\n", jiffiesDiff, aExpectedJiffies);
    TEST(jiffiesDiff == aExpectedJiffies);
}

Msg* SuiteCodecControllerBase::CreateTrack()
{
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    Msg* msg = iMsgFactory->CreateMsgTrack(*track, ++iNextTrackId);
    track->RemoveRef();
    return msg;
}

Msg* SuiteCodecControllerBase::CreateEncodedStream()
{
    return iMsgFactory->CreateMsgEncodedStream(Brx::Empty(), Brx::Empty(), 1<<21, ++iNextStreamId, iSeekable, false, this);
}

MsgFlush* SuiteCodecControllerBase::CreateFlush()
{
    return iMsgFactory->CreateMsgFlush(kExpectedFlushId);
}


// SuiteCodecControllerStream

SuiteCodecControllerStream::SuiteCodecControllerStream()
    : SuiteCodecControllerBase("SuiteCodecControllerStream")
{
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestStreamSuccessful), "TestStreamSuccessful");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestRecognitionFail), "TestRecognitionFail");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestTrackTrack), "TestTrackTrack");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestTrackEncodedStreamTrack), "TestTrackEncodedStreamTrack");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestTruncatedStreamInRecognition), "TestTruncatedStreamInRecognition");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestNoDataAfterRecognition), "TestNoDataAfterRecognition");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestTruncatedStream), "TestTruncatedStream");
    AddTest(MakeFunctor(*this, &SuiteCodecControllerStream::TestSeek), "TestSeek");
}

void SuiteCodecControllerStream::Setup()
{
    SuiteCodecControllerBase::Setup();
    iSemSeek = new Semaphore("SCCS", 0);
    iHandle = ISeeker::kHandleError;
    iExpectedFlushId = iFlushId = MsgFlush::kIdInvalid;
    iController->AddCodec(CodecFactory::NewWav());
    iController->Start();
}

void SuiteCodecControllerStream::TearDown()
{
    delete iSemSeek;
    SuiteCodecControllerBase::TearDown();
}

TUint SuiteCodecControllerStream::TrySeek(TUint aTrackId, TUint aStreamId, TUint64 /*aOffset*/)
{
    TEST(aTrackId == iTrackId);
    TEST(aStreamId == aStreamId);
    return kExpectedFlushId;
}

void SuiteCodecControllerStream::NotifySeekComplete(TUint aHandle, TUint aFlushId)
{
    iHandle = aHandle;
    iFlushId = aFlushId;
    iSemSeek->Signal();
}

Msg* SuiteCodecControllerStream::CreateAudio(TBool aValidHeader, TUint aDataBytes)
{
    ASSERT(iTotalBytes > 0);
    static const TUint32 kFmtChunkSize = 16;
    static const TUint16 kFmtAudioFormat = 1;
    static const TUint16 kBitsPerSample = 16;
    static const TUint kBytesPerSample = kBitsPerSample/8;

    TUint headerBytes = kWavHeaderBytes;
    if (iTotalBytes < headerBytes) {
        headerBytes = iTotalBytes;
    }
    const TUint audioBytes = iTotalBytes-headerBytes;
    TByte encodedAudioData[kMaxMsgBytes];

    Bws<kWavHeaderBytes> header;
    //TUint dataBytes = kMaxMsgBytes;
    TUint dataBytes = aDataBytes;
    if (iTrackOffset == 0) {
        // populate wav header
        // RIFF header
        if (aValidHeader) {
            header.Append("RIFF");                                                      // ChunkID
        }
        else {
            header.Append("NULL");
        }
        header.Append(Arch::LittleEndian4(36+audioBytes));                              // ChunkSize
        header.Append("WAVE");                                                          // Format

        // fmt subchunk
        header.Append("fmt ");                                                          // Subchunk1ID
        header.Append(Arch::LittleEndian4(kFmtChunkSize));                              // Subchunk1Size
        header.Append(Arch::LittleEndian2(kFmtAudioFormat));                            // AudioFormat
        header.Append(Arch::LittleEndian2(kNumChannels));                               // NumChannels
        header.Append(Arch::LittleEndian4(kSampleRate));                                // SampleRate
        header.Append(Arch::LittleEndian4(kSampleRate*kNumChannels*kBytesPerSample));   // ByteRate
        header.Append(Arch::LittleEndian2(kNumChannels*kBytesPerSample));               // BlockAlign
        header.Append(Arch::LittleEndian2(kBitsPerSample));                             // BitsPerSample

        // data subchunk
        header.Append("data");                                                          // Subchunk2ID
        header.Append(Arch::LittleEndian4(audioBytes));                                 // Subchunk2Size

        // append to encoded bytes buffer
        (void)memcpy(encodedAudioData, header.Ptr(), headerBytes);

        // update data byte count
        dataBytes = kMaxMsgBytes - headerBytes;
    }

    // Only output iTotalBytes-kWavHeaderBytes of audio in total.
    TUint remaining = audioBytes - iTrackOffsetBytes;
    if (dataBytes > remaining) {
        dataBytes = remaining;
    }

    TUint dataByteOffset = 0;
    TUint dataBytesTotal = dataBytes;
    if (iTrackOffset == 0) {
        dataByteOffset += kWavHeaderBytes;
        dataBytesTotal += headerBytes;
    }

    (void)memset(encodedAudioData+dataByteOffset, 0x7f, dataBytes);
    Brn encodedAudioBuf(encodedAudioData, dataBytesTotal);
    MsgAudioEncoded* audio = iMsgFactory->CreateMsgAudioEncoded(encodedAudioBuf);

    TUint samples = dataBytes / (kNumChannels*kBytesPerSample);
    TUint jiffiesPerSample = Jiffies::kPerSecond / kSampleRate;
    iTrackOffset += samples * jiffiesPerSample;
    iTrackOffsetBytes += dataBytes;
    return audio;
}

void SuiteCodecControllerStream::TestStreamSuccessful()
{
    static const TUint kAudioBytes = 6144;
    iTotalBytes = kWavHeaderBytes + kAudioBytes;
    Queue(CreateTrack());
    PullNext(EMsgTrack);
    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    while (iTrackOffsetBytes < kAudioBytes) {
        Queue(CreateAudio(true, kMaxMsgBytes));
    }
    // Pushing a MsgEncodedAudio should cause a MsgDecodedStream to be pushed
    // out other end of CodecController.
    PullNext(EMsgDecodedStream);
    PullNext(EMsgAudioPcm);

    ASSERT(iTrackOffsetBytes == kAudioBytes);
    TEST(iJiffies == iTrackOffset);
}

void SuiteCodecControllerStream::TestRecognitionFail()
{
    static const TUint kAudioBytes = 6144;
    iTotalBytes = kWavHeaderBytes + kAudioBytes;
    Queue(CreateTrack());
    PullNext(EMsgTrack);

    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    Queue(CreateAudio(false, kMaxMsgBytes));

    iSemStop->Wait();
    TEST(iStopCount == 1);
}

void SuiteCodecControllerStream::TestTruncatedStreamInRecognition()
{
    // This partially tests the ability of the WAV codec to return from its
    // Recognise() method, but what we're interested in is that the
    // CodecController then attempts to stop the unrecognised stream.
    //
    // WAV tries to read in 12 bytes for recognition, so give it fewer.
    iTotalBytes = 10;
    Queue(CreateTrack());
    PullNext(EMsgTrack);

    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    Queue(CreateAudio(true, kMaxMsgBytes));

    // Flush remaining audio from stream out by sending a new MsgTrack.
    Queue(CreateTrack());
    PullNext(EMsgTrack);

    iSemStop->Wait(kSemWaitMs);
    TEST(iStopCount == 1);

    TEST(iJiffies == iTrackOffset);
}

void SuiteCodecControllerStream::TestNoDataAfterRecognition()
{
    // WAV codec requires 12 bytes for successful recognition.
    iTotalBytes = 12;
    Queue(CreateTrack());
    PullNext(EMsgTrack);
    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    // Only send one msg (i.e., a truncated stream).
    // Won't get a MsgDecodedStream, as data is truncated.
    Queue(CreateAudio(true, kMaxMsgBytes));

    // Flush remaining audio from stream out by sending a new MsgTrack.
    Queue(CreateTrack());
    PullNext(EMsgTrack);

    TEST(iJiffies == iTrackOffset);
}

void SuiteCodecControllerStream::TestTruncatedStream()
{
    static const TUint kAudioBytes = 6144;
    iTotalBytes = kWavHeaderBytes + kAudioBytes;
    Queue(CreateTrack());
    PullNext(EMsgTrack);
    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    // Only send one msg (i.e., a truncated stream).
    Queue(CreateAudio(true, kMaxMsgBytes));
    PullNext(EMsgDecodedStream);

    // Flush remaining audio from stream out by sending a new MsgTrack.
    Queue(CreateTrack());
    PullNext(EMsgAudioPcm); // remaining audio from prev track
    PullNext(EMsgTrack);

    TEST(iJiffies == iTrackOffset);
}

void SuiteCodecControllerStream::TestTrackTrack()
{
    Queue(CreateTrack());
    PullNext(EMsgTrack);

    Queue(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteCodecControllerStream::TestTrackEncodedStreamTrack()
{
    Queue(CreateTrack());
    PullNext(EMsgTrack);

    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    Queue(CreateTrack());
    PullNext(EMsgTrack);

    iSemStop->Wait();
    TEST(iStopCount == 1);
}

void SuiteCodecControllerStream::TestSeek()
{
    static const TUint kMaxEncodedBytes = kMaxMsgBytes;
    static const TUint kSeconds = 3;
    static const TUint kChannels = 2;
    static const TUint kBitDepthBytes = 16/8;
    static const TUint kSamples = kSampleRate * kSeconds;
    static const TUint kAudioBytes = kSamples * kChannels * kBitDepthBytes;
    iTotalBytes = kWavHeaderBytes + kAudioBytes;

    Queue(CreateTrack());
    PullNext(EMsgTrack);

    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    while (iTrackOffsetBytes < kAudioBytes/2) { // get halfway through stream
        Queue(CreateAudio(true, kMaxEncodedBytes));
    }
    // Pushing a MsgEncodedAudio should cause a MsgDecodedStream to be pushed
    // out other end of CodecController.
    PullNext(EMsgDecodedStream);

    // Can only pull through queued-1 msgs, as CodecController will block when no more available.
    TUint maxPullBytes = iTrackOffsetBytes - kMaxEncodedBytes;
    TUint maxPullSamples = maxPullBytes/kChannels/kBitDepthBytes;
    TUint maxPullJiffies = maxPullSamples * Jiffies::JiffiesPerSample(kSampleRate);
    while (iJiffies < maxPullJiffies) {
        TUint64 jiffiesBefore = iJiffies;
        PullNext(EMsgAudioPcm);
        TUint64 offsetAfter = iMsgOffset;
        TEST(offsetAfter == jiffiesBefore); // check next Msg starts where previous msg ended in stream
    }

    // Do a seek.
    Thread::Sleep(50);  // FIXME - quick fix to stop race condition where last
                        // encoded audio msg may not have been processed before
                        // seek (and is thus discarded).
    ISeeker& seeker = *iController;
    TUint handle = ISeeker::kHandleError;
    TUint seekSeconds = 1;
    seeker.StartSeek(iTrackId, iStreamId, seekSeconds, *this, handle); // seek to 1s

    // Send some more msgs down to cause CodecController to unblock and start the seek.
    // These will be discarded.
    Queue(CreateAudio(true, kMaxEncodedBytes));
    Queue(CreateAudio(true, kMaxEncodedBytes));
    Queue(CreateAudio(true, kMaxEncodedBytes));
    Queue(CreateAudio(true, kMaxEncodedBytes));
    Queue(CreateAudio(true, kMaxEncodedBytes));
    Queue(CreateAudio(true, kMaxEncodedBytes));

    // Wait for seek to complete.
    iSemSeek->Wait(kSemWaitMs);
    TEST(iHandle == handle);
    TEST(iFlushId == kExpectedFlushId);

    // Adjust sending offsets and queue some more msgs.
    //iTrackOffsetBytes = kWavHeaderBytes + kSampleRate*kChannels*kBitDepthBytes * seekSeconds;
    iTrackOffsetBytes = kSampleRate*kChannels*kBitDepthBytes * seekSeconds;
    iTrackOffset = Jiffies::kPerSecond * seekSeconds;
    Queue(CreateFlush());
    while (iTrackOffsetBytes < kAudioBytes) {
        Queue(CreateAudio(true, kMaxEncodedBytes));
    }
    Queue(CreateEncodedStream()); // allows us to retrieve ALL MsgAudioPcm

    // Pull last EMsgAudioPcm that has been flushed out.
    TUint64 jiffiesBefore = iJiffies;
    PullNext(EMsgAudioPcm);
    TUint64 offsetAfter = iMsgOffset;
    TEST(offsetAfter == jiffiesBefore);

    // MsgFlush and MsgDecodedStream follow a successful seek.
    PullNext(EMsgFlush);
    PullNext(EMsgDecodedStream);

    // Adjust jiffy total to account for seek.
    TUint64 totalJiffies = iJiffies + (kSeconds-seekSeconds)*Jiffies::kPerSecond;

    // Get first MsgAudioPcm after seek.
    TUint64 jiffiesOffset = iJiffies;
    PullNext(EMsgAudioPcm);
    TUint64 offsetAfterSeek = iMsgOffset;
    TEST(offsetAfterSeek == Jiffies::kPerSecond*seekSeconds); // check new offset is seek position

    // Pull remainder of stream.
    while (iJiffies < totalJiffies) {
        TUint64 jiffiesBefore = iJiffies;
        PullNext(EMsgAudioPcm);
        TUint64 offsetAfter = iMsgOffset + jiffiesOffset - offsetAfterSeek;
        TEST(offsetAfter == jiffiesBefore);
    }

    PullNext(EMsgEncodedStream);
    //PullNext(EMsgDecodedStream);    // WAV doesn't output this until more audio passes through

    TEST(iJiffies == totalJiffies);
}


// SuiteCodecControllerPcmSize

SuiteCodecControllerPcmSize::SuiteCodecControllerPcmSize()
    : SuiteCodecControllerBase("SuiteCodecControllerPcmSize")
{
    AddTest(MakeFunctor(*this, &SuiteCodecControllerPcmSize::TestPcmIsExpectedSize), "TestPcmIsExpectedSize");
}

void SuiteCodecControllerPcmSize::Setup()
{
    SuiteCodecControllerBase::Setup();
    iController->AddCodec(new HelperCodecPassThrough(kAudioBytesPerMsg, kNumChannels, kSampleRate, kBitsPerSample, EMediaDataBigEndian));
    iController->Start();
}

void SuiteCodecControllerPcmSize::TearDown()
{
    SuiteCodecControllerBase::TearDown();
}

Msg* SuiteCodecControllerPcmSize::CreateAudio()
{
    static const TUint kBytesPerSample = kBitsPerSample/8;
    static const TUint dataBytes = kAudioBytesPerMsg;

    TByte encodedAudioData[kAudioBytesPerMsg];
    (void)memset(encodedAudioData, 0x7f, dataBytes);
    Brn encodedAudioBuf(encodedAudioData, dataBytes);
    MsgAudioEncoded* audio = iMsgFactory->CreateMsgAudioEncoded(encodedAudioBuf);

    TUint samples = dataBytes / (kNumChannels*kBytesPerSample);
    TUint jiffiesPerSample = Jiffies::kPerSecond / kSampleRate;
    iTrackOffset += samples * jiffiesPerSample;
    iTrackOffsetBytes += dataBytes;
    return audio;
}

void SuiteCodecControllerPcmSize::TestPcmIsExpectedSize()
{
    static const TUint kAudioBytes = 6144;
    static const TUint64 kJiffiesPerEncodedMsg = (Jiffies::kPerSecond / 44100) * kSamplesPerMsg;

    iTotalBytes = kWavHeaderBytes + kAudioBytes;
    Queue(CreateTrack());
    PullNext(EMsgTrack);
    Queue(CreateEncodedStream());
    PullNext(EMsgEncodedStream);

    while (iTrackOffsetBytes < kAudioBytes) {
        Queue(CreateAudio());
    }
    Queue(CreateEncodedStream());

    // Pushing a MsgEncodedAudio should cause a MsgDecodedStream to be pushed
    // out other end of CodecController.
    PullNext(EMsgDecodedStream);
    while (iJiffies < iTrackOffset) {
        PullNext(EMsgAudioPcm, kJiffiesPerEncodedMsg);
    }

    PullNext(EMsgEncodedStream);
    PullNext(EMsgDecodedStream);    // dummy codec will always recognise a stream and output MsgDecodedStream

    ASSERT(iTrackOffsetBytes == kAudioBytes); // check correct number of bytes have been output by test code
    TEST(iJiffies == iTrackOffset);
}


void TestCodecController()
{
    Runner runner("CodecController tests\n");
    runner.Add(new SuiteCodecControllerStream());
    runner.Add(new SuiteCodecControllerPcmSize());
    runner.Run();
}

#endif // HEADER_TESTCODECCONTROLLER
