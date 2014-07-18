#ifndef HEADER_PIPELINE_PROTOCOL
#define HEADER_PIPELINE_PROTOCOL

#include <OpenHome/Buffer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
class Environment;
namespace Media {

enum ProtocolStreamResult
{
    EProtocolStreamSuccess
   ,EProtocolErrorNotSupported
   ,EProtocolStreamStopped
   ,EProtocolStreamErrorRecoverable
   ,EProtocolStreamErrorUnrecoverable
};

enum ProtocolGetResult
{
    EProtocolGetSuccess
   ,EProtocolGetErrorNotSupported
   ,EProtocolGetErrorUnrecoverable
};

class IUriStreamer
{
public:
    virtual TBool DoStream(Track& aTrack) = 0;
    virtual void Interrupt(TBool aInterrupt) = 0;
};

class IProtocolSet
{
public:
    virtual ProtocolStreamResult Stream(const Brx& aUri) = 0;
};

class ContentProcessor;
class IProtocolManager : public IProtocolSet
{
public:
    virtual ContentProcessor* GetContentProcessor(const Brx& aUri, const Brx& aMimeType, const Brx& aData) const = 0;
    virtual ContentProcessor* GetAudioProcessor() const = 0;
    virtual TBool Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) = 0;
    virtual TBool IsCurrentTrack(TUint aTrackId) const = 0;
};

class IProtocolReader : public IReader
{
public:
    virtual Brn ReadRemaining() = 0;
};

/**
 * The start of the pipeline.  Runs in a client thread and feeds data into the pipeline.
 *
 * Does not need to provide any moderation.  Calls to push encoded audio into the pipeline
 * will block if the pipeline reaches its buffering capacity.
 */
class Protocol : protected IStreamHandler, protected INonCopyable
{
public:
    virtual ~Protocol();
    ProtocolGetResult DoGet(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes);
    ProtocolStreamResult TryStream(const Brx& aUri);
    void Initialise(IProtocolManager& aProtocolManager, IPipelineIdProvider& aIdProvider, ISupply& aSupply, IFlushIdProvider& aFlushIdProvider);
    TBool Active() const;
    /**
     * Interrupt any stream that is currently in-progress, or cancel a previous interruption.
     *
     * This may be called from a different thread.  The implementor is responsible for any synchronisation.
     *
     * @param[in] aInterrupt       When true, interrupt any potentially blocking calls inside a
     *                             Stream() call, causing them to report EProtocolStreamErrorUnrecoverable.
     */
    virtual void Interrupt(TBool aInterrupt) = 0;
protected:
    Protocol(Environment& aEnv);
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TBool TryGet(IWriter& aWriter, TUint aTrackId, TUint aStreamId, TUint64 aOffset, TUint aBytes);
    void NotifyStarving(const Brx& aMode, TUint aTrackId, TUint aStreamId);
private:
    /**
     * Stream a track.
     *
     * This call should block for as long as it takes to stream the entire resource into the
     * pipeline (or decide that this protocol is not capable of streaming the resource).
     * The protocol should translate received data into calls to ISupply.  Data should be
     * pushed into the pipeline as quickly as possible, relying on the pipeline to apply
     * any moderation
     *
     * @param[in] aUri             Resource to be streamed.
     *
     * @return  Results of attempted stream:
     *              EProtocolStreamSuccess - the entire stream was pushed into the pipeline
                    EProtocolErrorNotSupported - the uri is not supported by this protocol
                    EProtocolStreamStopped - streaming was interrupted by a downstream request to stop (TryStop was called)
                    EProtocolStreamErrorRecoverable - for internal use only; will not be reported here
                    EProtocolStreamErrorUnrecoverable - error in stream; do not attempt to stream via other protocol(s)
     */
    virtual ProtocolStreamResult Stream(const Brx& aUri) = 0;
    /**
     * Read a block of data from a uri.
     *
     * This call should block for as long as it takes to stream the entire resource into the
     * pipeline (or decide that this protocol is not capable of streaming the resource).
     * Data should be returned as quickly as possible.
     *
     * @param[in] aWriter          Interface used to return the requested data.
     * @param[in] aUri             Uri to be read.
     * @param[in] aOffset          Byte offset to start reading from.
     * @param[in] aBytes           Number of bytes to read
     *
     * @return  EProtocolGetSuccess if the data was read.
     *          EProtocolGetErrorNotSupported if the protocol doesn't support the uri.  Another protocol will be tried in this case.
     *          EProtocolGetErrorUnrecoverable if the read failed and no other protocol should be tried.
     */
    virtual ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) = 0;
protected:
    Environment& iEnv;
    IProtocolManager* iProtocolManager;
    IPipelineIdProvider* iIdProvider;
    ISupply* iSupply;
    IFlushIdProvider* iFlushIdProvider;
    TBool iActive;
private:
    class AutoStream : private INonCopyable
    {
    public:
        AutoStream(Protocol& aProtocol);
        ~AutoStream();
    private:
        Protocol& iProtocol;
    };
};

class ProtocolNetwork : public Protocol
{
protected:
    static const TUint kReadBufferBytes = 9 * 1024; // WMA radio streams (in ProtocolRtsp) require at least an 8k buffer
                                                    // Songcast requires a 9k buffer
    static const TUint kWriteBufferBytes = 1024;
    static const TUint kConnectTimeoutMs = 3000;
protected:
    ProtocolNetwork(Environment& aEnv);
    TBool Connect(const OpenHome::Uri& aUri, TUint aDefaultPort);
protected: // from Protocol
    void Interrupt(TBool aInterrupt);
protected:
    void Open();
    void Close();
protected:
    Srs<kReadBufferBytes> iReaderBuf;
    Sws<kWriteBufferBytes> iWriterBuf;
    Mutex iLock;
    SocketTcpClient iTcpClient;
    TBool iSocketIsOpen;
};

class ContentProcessor
{
    static const TUint kMaxLineBytes = 512;
    static const TUint kMaxTagBytes = 512;
public:
    virtual ~ContentProcessor();
    void Initialise(IProtocolSet& aProtocolSet);
    TBool IsActive() const;
    void SetActive();
protected:
    ContentProcessor();
public:
    virtual TBool Recognise(const Brx& aUri, const Brx& aMimeType, const Brx& aData) = 0;
    virtual void Reset();
    virtual ProtocolStreamResult Stream(IProtocolReader& aReader, TUint64 aTotalBytes) = 0;
protected:
    Brn ReadLine(IProtocolReader& aReader, TUint64& aBytesRemaining);
    Brn ReadTag(IProtocolReader& aReader, TUint64& aBytesRemaining);
protected:
    IProtocolSet* iProtocolSet;
    Bws<kMaxLineBytes> iPartialLine;
    Bws<kMaxTagBytes> iPartialTag;
private:
    TBool iActive;
    TBool iInTag;
};

class ProtocolManager : public IUriStreamer, private IProtocolManager, private INonCopyable
{
    static const TUint kMaxUriBytes = 1024;
public:
    ProtocolManager(ISupply& aSupply, IPipelineIdProvider& aIdProvider, IFlushIdProvider& aFlushIdProvider);
    virtual ~ProtocolManager();
    void Add(Protocol* aProtocol);
    void Add(ContentProcessor* aProcessor);
public: // from IUriStreamer
    TBool DoStream(Track& aTrack);
    void Interrupt(TBool aInterrupt);
private: // from IProtocolManager
    ProtocolStreamResult Stream(const Brx& aUri);
    ContentProcessor* GetContentProcessor(const Brx& aUri, const Brx& aMimeType, const Brx& aData) const;
    ContentProcessor* GetAudioProcessor() const;
    TBool Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes);
    TBool IsCurrentTrack(TUint aTrackId) const;
private:
    IPipelineIdProvider& iIdProvider;
    IFlushIdProvider& iFlushIdProvider;
    ISupply& iSupply;
    mutable Mutex iLock;
    std::vector<Protocol*> iProtocols;
    std::vector<ContentProcessor*> iContentProcessors;
    ContentProcessor* iAudioProcessor;
    TUint iTrackId;
};

} // namespace Media
} // namespace OpenHome

#endif  // HEADER_PIPELINE_PROTOCOL
