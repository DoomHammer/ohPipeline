#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Media/SupplyAggregator.h>

#include <algorithm>

namespace OpenHome {
namespace Media {

class HeaderIcyMetadata : public HttpHeader
{
public:
    static void Write(WriterHttpHeader& aWriter);
    TUint Bytes() const;
private: // from HttpHeader
    TBool Recognise(const Brx& aHeader);
    void Process(const Brx& aValue);
private:
    TUint iBytes;
};

class ProtocolHttp : public ProtocolNetwork, private IReader
{
    static const TUint kIcyMetadataBytes = 255 * 16;
    static const TUint kMaxUserAgentBytes = 64;
    static const TUint kMaxContentRecognitionBytes = 100;
public:
    ProtocolHttp(Environment& aEnv, const Brx& aUserAgent);
    ~ProtocolHttp();
private: // from Protocol
    void Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream) override;
    void Interrupt(TBool aInterrupt) override;
    ProtocolStreamResult Stream(const Brx& aUri) override;
    ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) override;
    void Deactivated() override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryStop(TUint aStreamId) override;
private: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    void Reinitialise(const Brx& aUri);
    ProtocolStreamResult DoStream();
    ProtocolGetResult DoGet(IWriter& aWriter, TUint64 aOffset, TUint aBytes);
    ProtocolStreamResult DoSeek(TUint64 aOffset);
    ProtocolStreamResult DoLiveStream();
    void StartStream();
    TUint WriteRequest(TUint64 aOffset);
    ProtocolStreamResult ProcessContent();
    TBool ContinueStreaming(ProtocolStreamResult aResult);
    TBool IsCurrentStream(TUint aStreamId) const;
    void ExtractMetadata();
private:
    SupplyAggregator* iSupply;
    WriterHttpRequest iWriterRequest;
    ReaderUntilS<2048> iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    ReaderHttpChunked iDechunker;
    ContentRecogBuf iContentRecogBuf;
    HttpHeaderContentType iHeaderContentType;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderLocation iHeaderLocation;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    HeaderIcyMetadata iHeaderIcyMetadata;
    Bws<kMaxUserAgentBytes> iUserAgent;
    Bws<kIcyMetadataBytes> iIcyMetadata;
    Bws<kIcyMetadataBytes> iNewIcyMetadata; // only used in a single function but too large to comfortably declare on the stack
    Bws<kIcyMetadataBytes> iIcyData; // only used in a single function but too large to comfortably declare on the stack
    OpenHome::Uri iUri;
    TUint64 iTotalStreamBytes;
    TUint64 iTotalBytes;
    TUint iStreamId;
    TBool iSeekable;
    TBool iSeek;
    TBool iLive;
    TBool iStarted;
    TBool iStopped;
    TBool iStreamIncludesMetaData;
    TBool iReadSuccess;
    TUint iDataChunkSize;
    TUint iDataChunkRemaining;
    TUint64 iSeekPos;
    TUint64 iOffset;
    ContentProcessor* iContentProcessor;
    TUint iNextFlushId;
    Semaphore iSem;
};

};  // namespace Media
};  // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;


Protocol* ProtocolFactory::NewHttp(Environment& aEnv, const Brx& aUserAgent)
{ // static
    return new ProtocolHttp(aEnv, aUserAgent);
}


// HeaderIcyMetadata

void HeaderIcyMetadata::Write(WriterHttpHeader& aWriter)
{
    aWriter.WriteHeader(Brn("Icy-MetaData"), Brn("1"));
}

TUint HeaderIcyMetadata::Bytes() const
{
    if (Received()) {
        return iBytes;
    }
    return 0;
}

TBool HeaderIcyMetadata::Recognise(const Brx& aHeader)
{
    return Ascii::CaseInsensitiveEquals(aHeader, Brn("icy-metaint"));
}

void HeaderIcyMetadata::Process(const Brx& aValue)
{
    try {
        iBytes = Ascii::Uint(aValue);
        SetReceived();
    }
    catch (AsciiError&) {
        THROW(HttpError);
    }
}


// ProtocolHttp

ProtocolHttp::ProtocolHttp(Environment& aEnv, const Brx& aUserAgent)
    : ProtocolNetwork(aEnv)
    , iSupply(nullptr)
    , iWriterRequest(iWriterBuf)
    , iReaderUntil(iReaderBuf)
    , iReaderResponse(aEnv, iReaderUntil)
    , iDechunker(iReaderUntil)
    , iContentRecogBuf(iDechunker)
    , iUserAgent(aUserAgent)
    , iTotalStreamBytes(0)
    , iTotalBytes(0)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iSeekable(false)
    , iSem("PRTH", 0)
{
    iReaderResponse.AddHeader(iHeaderContentType);
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderLocation);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);
    iReaderResponse.AddHeader(iHeaderIcyMetadata);
}

ProtocolHttp::~ProtocolHttp()
{
    delete iSupply;
}

void ProtocolHttp::Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolHttp::Interrupt(TBool aInterrupt)
{
    iLock.Wait();
    if (iActive) {
        LOG(kMedia, "ProtocolHttp::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
            iSem.Signal(); // no need to check iLive - iSem will be cleared when this protocol is next reused anyway
        }
        iTcpClient.Interrupt(aInterrupt);
    }
    iLock.Signal();
}

ProtocolStreamResult ProtocolHttp::Stream(const Brx& aUri)
{
    Reinitialise(aUri);
    if (iUri.Scheme() != Brn("http")) {
        return EProtocolErrorNotSupported;
    }
    LOG(kMedia, "ProtocolHttp::Stream(%.*s)\n", PBUF(aUri));

    ProtocolStreamResult res = DoStream();
    if (res == EProtocolStreamErrorUnrecoverable) {
        if (iContentProcessor != nullptr) {
            iContentProcessor->Reset();
        }
        return res;
    }
    if (iLive) {
        // don't want to buffer content from a live stream
        // ...so need to wait on pipeline signalling it is ready to play
        LOG(kMedia, "ProtocolHttp::Stream live stream waiting to be (re-)started\n");
        Close();
        iSem.Wait();
        LOG(kMedia, "ProtocolHttp::Stream live stream restart\n");
        res = EProtocolStreamErrorRecoverable; // bodge to drop into the loop below
    }
    while (ContinueStreaming(res)) {
        if (iStopped) {
            res = EProtocolStreamStopped;
            break;
        }
        Close();
        if (iLive) {
            res = DoLiveStream();
        }
        else if (iSeek) {
            iLock.Wait();
            iSupply->OutputFlush(iNextFlushId);
            iNextFlushId = MsgFlush::kIdInvalid;
            iOffset = iSeekPos;
            iSeek = false;
            iLock.Signal();
            res = DoSeek(iOffset);
        }
        else {
            // FIXME - if stream is non-seekable, set ErrorUnrecoverable as soon as Connect succeeds
            /* FIXME - reconnects should use extra http headers to check that content hasn't changed
               since our first attempt at reading it.  Any change should result in ErrorUnrecoverable */
            TUint code = WriteRequest(iOffset);
            if (code != 0) {
                iTotalBytes = iHeaderContentLength.ContentLength();
                res = ProcessContent();
            }
        }
        if (res == EProtocolStreamErrorUnrecoverable) {
            // FIXME - msg to indicate bad track
        }
        if (res == EProtocolStreamErrorRecoverable) {
            Thread::Sleep(50);
        }
    }

    iSupply->Flush();
    iLock.Wait();
    if ((iStopped || iSeek) && iNextFlushId != MsgFlush::kIdInvalid) {
        iSupply->OutputFlush(iNextFlushId);
    }
    // clear iStreamId to prevent TrySeek or TryStop returning a valid flush id
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iLock.Signal();

    return res;
}

ProtocolGetResult ProtocolHttp::Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes)
{
    LOG(kMedia, "> ProtocolHttp::Get\n");
    Reinitialise(aUri);

    if (iUri.Scheme() != Brn("http")) {
        LOG(kMedia, "ProtocolHttp::Get Scheme not recognised\n");
        Close();
        return EProtocolGetErrorNotSupported;
    }

    Close();
    if (!Connect(iUri, 80)) {
        LOG(kMedia, "ProtocolHttp::Get Connection failure\n");
        return EProtocolGetErrorUnrecoverable;
    }

    ProtocolGetResult res = DoGet(aWriter, aOffset, aBytes);
    iTcpClient.Interrupt(false);
    Close();
    LOG(kMedia, "< ProtocolHttp::Get\n");
    return res;
}

void ProtocolHttp::Deactivated()
{
    if (iContentProcessor != nullptr) {
        iContentProcessor->Reset();
        iContentProcessor = nullptr;
    }
    Close();
}

EStreamPlay ProtocolHttp::OkToPlay(TUint aStreamId)
{
    LOG(kMedia, "> ProtocolHttp::OkToPlay(%u)\n", aStreamId);
    const EStreamPlay canPlay = iIdProvider->OkToPlay(aStreamId);
    if (canPlay != ePlayNo && iLive && iStreamId == aStreamId) {
        iSem.Signal();
    }
    LOG(kMedia, "< ProtocolHttp::OkToPlay(%u) == %s\n", aStreamId, kStreamPlayNames[canPlay]);
    return canPlay;
}

TUint ProtocolHttp::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    LOG(kMedia, "ProtocolHttp::TrySeek\n");

    AutoMutex _(iLock);
    if (!IsCurrentStream(aStreamId)) {
        return MsgFlush::kIdInvalid;
    }

    if (!iLive && aOffset >= iTotalStreamBytes) {
        // Attempting to request beyond end of file.
        LOG(kMedia, "ProtocolHttp::TrySeek attempting to seek beyond end of file. aStreamId: %u, aOffset: %llu, iTotalBytes: %llu\n", aStreamId, aOffset, iTotalBytes);
        return MsgFlush::kIdInvalid;
    }

    iSeek = true;
    iSeekPos = aOffset;
    if (iNextFlushId == MsgFlush::kIdInvalid) {
        /* If a valid flushId is set then We've previously promised to send a Flush but haven't
            got round to it yet.  Re-use the same id for any other requests that come in before
            our main thread gets a chance to issue a Flush */
        iNextFlushId = iFlushIdProvider->NextFlushId();
    }

    iTcpClient.Interrupt(true);
    return iNextFlushId;
}

TUint ProtocolHttp::TryStop(TUint aStreamId)
{
    AutoMutex _(iLock);
    if (!IsCurrentStream(aStreamId)) {
        return MsgFlush::kIdInvalid;
    }

    if (iNextFlushId == MsgFlush::kIdInvalid) {
        /* If a valid flushId is set then We've previously promised to send a Flush but haven't
            got round to it yet.  Re-use the same id for any other requests that come in before
            our main thread gets a chance to issue a Flush */
        iNextFlushId = iFlushIdProvider->NextFlushId();
    }
    iStopped = true;
    iTcpClient.Interrupt(true);
    if (iLive) {
        iSem.Signal();
    }
    return iNextFlushId;
}

Brn ProtocolHttp::Read(TUint aBytes)
{
    TUint bytes = aBytes;
    if (iStreamIncludesMetaData) {
        if (iDataChunkRemaining == 0) {
            ExtractMetadata();
            iDataChunkRemaining = iDataChunkSize;
        }
        bytes = std::min(iDataChunkRemaining, bytes);
    }
    Brn buf = iContentRecogBuf.Read(bytes);
    if (iStreamIncludesMetaData) {
        iDataChunkRemaining -= buf.Bytes();
    }
    iOffset += buf.Bytes();
    iReadSuccess = true;
    return buf;
}

void ProtocolHttp::ReadFlush()
{
    iContentRecogBuf.ReadFlush();
}

void ProtocolHttp::ReadInterrupt()
{
    iContentRecogBuf.ReadInterrupt();
}

void ProtocolHttp::Reinitialise(const Brx& aUri)
{
    iTotalStreamBytes = iTotalBytes = iSeekPos = iOffset = 0;
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iSeekable = iSeek = iLive = iStarted = iStopped = iStreamIncludesMetaData = iReadSuccess = false;
    iDataChunkSize = iDataChunkRemaining = 0;
    iContentProcessor = nullptr;
    iNextFlushId = MsgFlush::kIdInvalid;
    (void)iSem.Clear();
    iUri.Replace(aUri);
    iIcyMetadata.SetBytes(0);
    iNewIcyMetadata.SetBytes(0);
    iContentRecogBuf.ReadFlush();
}

ProtocolStreamResult ProtocolHttp::DoStream()
{
    TUint code;
    for (;;) { // loop until we don't get a redirection response (i.e. normally don't loop at all!)
        code = WriteRequest(0);
        if (code == 0) {
            return EProtocolStreamErrorUnrecoverable;
        }
        // Check for redirection
        if (code >= HttpStatus::kRedirectionCodes && code < HttpStatus::kClientErrorCodes) {
            if (!iHeaderLocation.Received()) {
                return EProtocolStreamErrorUnrecoverable;
            }
            iUri.Replace(iHeaderLocation.Location());
            continue;
        }
        break;
    }

    iSeekable = false;
    iTotalStreamBytes = iHeaderContentLength.ContentLength();
    iTotalBytes = iTotalStreamBytes;
    iLive = (iTotalBytes == 0);
    if (code != HttpStatus::kPartialContent.Code() && code != HttpStatus::kOk.Code()) {
        LOG(kMedia, "ProtocolHttp::DoStream server returned error %u\n", code);
        return EProtocolStreamErrorUnrecoverable;
    }
    if (code == HttpStatus::kPartialContent.Code()) {
        if (iTotalBytes > 0) {
            iSeekable = true;
        }
        LOG(kMedia, "ProtocolHttp::DoStream 'Partial Content' seekable=%d (%lld bytes)\n", iSeekable, iTotalBytes);
    }
    else { // code == HttpStatus::kOk.Code()
        LOG(kMedia, "ProtocolHttp::DoStream 'OK' non-seekable (%lld bytes)\n", iTotalBytes);
    }
    if (iHeaderIcyMetadata.Received()) {
        ASSERT(iTotalBytes == 0); // if non-live streams contain icy data, we'll need to adjust totalBytes passed to content processor
        iStreamIncludesMetaData = true;
        iDataChunkSize = iDataChunkRemaining = iHeaderIcyMetadata.Bytes();
    }

    iDechunker.SetChunked(iHeaderTransferEncoding.IsChunked());

    return ProcessContent();
}

ProtocolGetResult ProtocolHttp::DoGet(IWriter& aWriter, TUint64 aOffset, TUint aBytes)
{
    try {
        LOG(kMedia, "ProtocolHttp::DoGet send request\n");
        iWriterRequest.WriteMethod(Http::kMethodGet, iUri.PathAndQuery(), Http::eHttp11);
        const TUint port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
        Http::WriteHeaderHostAndPort(iWriterRequest, iUri.Host(), port);
        Http::WriteHeaderConnectionClose(iWriterRequest);
        TUint64 last = aOffset+aBytes;
        if (last > 0) {
            last -= 1;  // need to adjust for last byte position as request
                        // requires absolute positions, rather than range
        }
        Http::WriteHeaderRange(iWriterRequest, aOffset, last);
        iWriterRequest.WriteFlush();
    }
    catch(WriterError&) {
        LOG(kMedia, "ProtocolHttp::DoGet WriterError\n");
        return EProtocolGetErrorUnrecoverable;
    }

    try {
        LOG(kMedia, "ProtocolHttp::DoGet read response\n");
        iReaderResponse.Read();

        const TUint code = iReaderResponse.Status().Code();
        iTotalBytes = iHeaderContentLength.ContentLength();
        iTotalBytes = std::min(iTotalBytes, (TUint64)aBytes);
        // FIXME - should parse the Content-Range response to ensure we're
        // getting the bytes requested - the server may (validly) opt not to
        // honour our request.
        LOG(kMedia, "ProtocolHttp::DoGet response code %d\n", code);
        if (code != HttpStatus::kPartialContent.Code() && code != HttpStatus::kOk.Code()) {
            LOG(kMedia, "ProtocolHttp::DoGet server returned error %u\n", code);
            return EProtocolGetErrorUnrecoverable;
        }
        if (code == HttpStatus::kPartialContent.Code()) {
            LOG(kMedia, "ProtocolHttp::DoGet 'Partial Content' (%lld bytes)\n", iTotalBytes);
            if (iTotalBytes >= aBytes) {
                TUint64 count = 0;
                TUint bytes = 1024; // FIXME - choose better value or justify this
                while (count < iTotalBytes) {
                    const TUint remaining = static_cast<TUint>(iTotalBytes-count);
                    if (remaining < bytes) {
                        bytes = remaining;
                    }
                    Brn buf = Read(bytes);
                    aWriter.Write(buf);
                    count += buf.Bytes();
                    // If we start pushing some bytes to IWriter then get an
                    // error, will fall through and return
                    // EProtocolGetErrorUnrecoverable below, so IWriter won't
                    // receive duplicate data and TryGet() will return false,
                    // so IWriter knows to invalidate any data it's received.
                }
                return EProtocolGetSuccess;
            }
        }
        else { // code == HttpStatus::kOk.Code()
            LOG(kMedia, "ProtocolHttp::DoGet 'OK' (%lld bytes)\n", iTotalBytes);
        }

    }
    catch(HttpError&) {
        LOG(kMedia, "ProtocolHttp::DoGet HttpError\n");
    }
    catch(ReaderError&) {
        LOG(kMedia, "ProtocolHttp::DoGet ReaderError\n");
    }
    return EProtocolGetErrorUnrecoverable;
}

ProtocolStreamResult ProtocolHttp::DoSeek(TUint64 aOffset)
{
    Interrupt(false);
    const TUint code = WriteRequest(aOffset);
    if (code == 0) {
        return EProtocolStreamErrorRecoverable;
    }
    iTotalBytes = iHeaderContentLength.ContentLength();
    if (code != HttpStatus::kPartialContent.Code()) {
        return EProtocolStreamErrorUnrecoverable;
    }

    return ProcessContent();
}

ProtocolStreamResult ProtocolHttp::DoLiveStream()
{
    const TUint code = WriteRequest(0);
    iLive = false;
    if (code == 0) {
        return EProtocolStreamErrorRecoverable;
    }

    return ProcessContent();
}

void ProtocolHttp::StartStream()
{
    LOG(kMedia, "ProtocolHttp::StartStream\n");

    iStreamId = iIdProvider->NextStreamId();
    iSupply->OutputStream(iUri.AbsoluteUri(), iTotalBytes, iOffset, iSeekable, iLive, *this, iStreamId);
    iStarted = true;
}

TUint ProtocolHttp::WriteRequest(TUint64 aOffset)
{
    iContentRecogBuf.ReadFlush();
    //iTcpClient.LogVerbose(true);
    Close();
    const TUint port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
    if (!Connect(iUri, port)) {
        LOG(kMedia, "ProtocolHttp::WriteRequest Connection failure\n");
        return 0;
    }

    /* GETting ASX for BBC Scotland responds with invalid chunking if we request ICY metadata.
       Suppress this header if we're requesting a resource with an extension that matches
       a known ContentProcessor */
    Brn path(iUri.Path());
    ASSERT_DEBUG(path.Bytes() > 0 && path[0] != '.');
    Brn ext;
    for (TUint i=path.Bytes()-1; i!=0; i--) {
        if (path[i] == '.') {
            ext.Set(path.Split(i));
            break;
        }
    }
    TBool suppressIcyHeader = false;
    if (Ascii::CaseInsensitiveEquals(ext, Brn(".asx")) ||
        Ascii::CaseInsensitiveEquals(ext, Brn(".pls")) ||
        Ascii::CaseInsensitiveEquals(ext, Brn(".m3u")) ||
        Ascii::CaseInsensitiveEquals(ext, Brn(".xml")) ||
        Ascii::CaseInsensitiveEquals(ext, Brn(".opml"))) {
        suppressIcyHeader = true;;
    }
    try {
        LOG(kMedia, "ProtocolHttp::WriteRequest send request\n");
        iWriterRequest.WriteMethod(Http::kMethodGet, iUri.PathAndQuery(), Http::eHttp11);
        const TUint port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
        Http::WriteHeaderHostAndPort(iWriterRequest, iUri.Host(), port);
        if (iUserAgent.Bytes() > 0) {
            iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
        }
        Http::WriteHeaderConnectionClose(iWriterRequest);
        if (!suppressIcyHeader) {
            HeaderIcyMetadata::Write(iWriterRequest);
        }
        Http::WriteHeaderRangeFirstOnly(iWriterRequest, aOffset);
        iWriterRequest.WriteFlush();
    }
    catch(WriterError&) {
        LOG(kMedia, "ProtocolHttp::WriteRequest writer error\n");
        return 0;
    }

    try {
        LOG(kMedia, "ProtocolHttp::WriteRequest read response\n");
        //iTcpClient.LogVerbose(true);
        iReaderResponse.Read();
        //iTcpClient.LogVerbose(false);
    }
    catch(HttpError&) {
        LOG(kMedia, "ProtocolHttp::WriteRequest http error\n");
        return 0;
    }
    catch(ReaderError&) {
        LOG(kMedia, "ProtocolHttp::WriteRequest reader error\n");
        return 0;
    }
    const TUint code = iReaderResponse.Status().Code();
    LOG(kMedia, "ProtocolHttp::WriteRequest response code %d\n", code);
    return code;
}

ProtocolStreamResult ProtocolHttp::ProcessContent()
{
    LOG(kMedia, "ProtocolHttp::ProcessContent %lld\n", iTotalBytes);

    if (iContentProcessor == nullptr && !iStarted) {
        try {
            iContentRecogBuf.Populate(iTotalBytes);
            const Brx& contentType = iHeaderContentType.Received()? iHeaderContentType.Type() : Brx::Empty();
            const Brx& content = iContentRecogBuf.Buffer();
            iContentProcessor = iProtocolManager->GetContentProcessor(iUri.AbsoluteUri(), contentType, content);
        }
        catch (ReaderError&) {
            return EProtocolStreamErrorRecoverable;
        }
    }
    if (iContentProcessor != nullptr) {
        iLive = false; /* Only audio streams will result in pipeline msgs and calls to OkToPlay().
                          Clear 'live' flag for other cases to avoid Stream() waiting on iSem. */
        return iContentProcessor->Stream(*this, iTotalBytes);
    }

    if (!iStarted) {
        StartStream();
        if (iLive) {
            return EProtocolStreamErrorRecoverable;
        }
    }
    iContentProcessor = iProtocolManager->GetAudioProcessor();
    ProtocolStreamResult res = iContentProcessor->Stream(*this, iTotalBytes);
    if (!iReadSuccess) {
        return EProtocolStreamErrorUnrecoverable;
    }
    if (res == EProtocolStreamErrorRecoverable) {
        LOG(kMedia, "EProtocolStreamErrorRecoverable from audio processor after %llu bytes (total=%llu)\n",
            iOffset, iTotalBytes);
    }
    if (res == EProtocolStreamSuccess && iSeek) {
        // Seek request was accepted just before we read the last fragment of this stream
        // Report a recoverable error to allow Stream()'s main loop a chance to process the seek.
        res = EProtocolStreamErrorRecoverable;
    }
    return res;
}

TBool ProtocolHttp::ContinueStreaming(ProtocolStreamResult aResult)
{
    if (aResult == EProtocolStreamErrorRecoverable) {
        return true;
    }
    return false;
}

TBool ProtocolHttp::IsCurrentStream(TUint aStreamId) const
{
    if (iStreamId != aStreamId || aStreamId == IPipelineIdProvider::kStreamIdInvalid) {
        return false;
    }
    return true;
}

void ProtocolHttp::ExtractMetadata()
{
    Brn metadata = iContentRecogBuf.Read(1);
    iOffset++;
    TUint metadataBytes = metadata[0] * 16;

    if (metadataBytes != 0) {
        iIcyData.SetBytes(0);
        do {
            Brn buf = iContentRecogBuf.Read(metadataBytes);
            iOffset += buf.Bytes();
            metadataBytes -= buf.Bytes();
            iIcyData.Append(buf);
        } while (metadataBytes != 0);

        iNewIcyMetadata.Replace("<DIDL-Lite xmlns:dc='http://purl.org/dc/elements/1.1/' ");
        iNewIcyMetadata.Append("xmlns:upnp='urn:schemas-upnp-org:metadata-1-0/upnp/' ");
        iNewIcyMetadata.Append("xmlns='urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/'>");
        iNewIcyMetadata.Append("<item id='' parentID='' restricted='True'><dc:title>");

        Parser data(iIcyData);
        while(!data.Finished()) {
            Brn name = data.Next('=');
            if (name == Brn("StreamTitle")) {
                // metadata is in the format: 'data';
                // may contain single quote characters so seek to the semicolon and discard the trailing single quote
                data.Next('\'');
                Brn title = data.Next(';');
                if (title.Bytes() > 1) {
                    iNewIcyMetadata.Append(Brn(title.Ptr(), title.Bytes()-1));
                }

                iNewIcyMetadata.Append("</dc:title><upnp:albumArtURI></upnp:albumArtURI>");
                iNewIcyMetadata.Append("<upnp:class>object.item</upnp:class></item></DIDL-Lite>");
                if (iNewIcyMetadata != iIcyMetadata) {
                    LOG(kMedia, "ProtocolHttp::ExtractMetadata() - %.*s\n", PBUF(iNewIcyMetadata));
                    iIcyMetadata.Replace(iNewIcyMetadata);
                    iSupply->OutputMetadata(iIcyMetadata);
                }
                break;
            }
        }
    }
}
