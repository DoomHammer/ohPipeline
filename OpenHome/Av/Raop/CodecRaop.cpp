#include <OpenHome/Av/Raop/CodecRaop.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Codec/AlacBase.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;
using namespace OpenHome::Av;


// CodecRaop

CodecRaop::CodecRaop() 
    : CodecAlacBase("RAOP")
{
    LOG(kCodec, "CodecRaop::CodecRaop\n");
}

CodecRaop::~CodecRaop()
{
    LOG(kCodec, "CodecRaop::~CodecRaop\n");
}

TBool CodecRaop::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    LOG(kCodec, "CodecRaop::Recognise\n");
    if (aStreamInfo.RawPcm()) {
        return false;
    }
    Bws<4> buf;
    iController->Read(buf, buf.MaxBytes());

    if (buf == Brn("Raop")) {
        LOG(kCodec, "CodecRaop::Recognise airplay\n");
        return true;
    }

    return false;
}

void CodecRaop::StreamInitialise()
{
    LOG(kCodec, "CodecRaop::StreamInitialise\n");

    CodecAlacBase::Initialise();

    iInBuf.SetBytes(0);
    iController->Read(iInBuf, 4);
    ASSERT(iInBuf == Brn("Raop")); // Already saw this during Recognise().

    // Read and parse fmtp string.
    iInBuf.SetBytes(0);
    iController->Read(iInBuf, 4);
    try {
        const TUint fmtpBytes = Ascii::Uint(iInBuf);    // size of fmtp string
        iInBuf.SetBytes(0);
        iController->Read(iInBuf, fmtpBytes);
        ParseFmtp(iInBuf);
    }
    catch (AsciiError&) {
        THROW(CodecStreamCorrupt);
    }

    iInBuf.SetBytes(0);
    alac = create_alac(iBitDepth, iChannels);

    // FIXME - use iInBuf here instead of local stack buffer?
    Bws<IMpeg4InfoWritable::kMaxStreamDescriptorBytes> streamDescriptor;
    static const TUint kStreamDescriptorIgnoreBytes = 20; // First 20 bytes are ignored by decoder.
    streamDescriptor.SetBytes(kStreamDescriptorIgnoreBytes);
    streamDescriptor.Append(iCodecSpecificData);

    alac_set_info(alac, (char*)streamDescriptor.Ptr()); // Configure decoder.

    iBytesPerSample = iChannels*iBitDepth / 8;
    iSamplesWrittenTotal = 0;
    iTrackLengthJiffies = 0;// (iDuration * Jiffies::kPerSecond) / iTimescale;


    /*LOG(kCodec, "CodecRaop::StreamInitialise  iBitDepth %u, iTimeScale: %u, iSampleRate: %u, iSamplesTotal %llu, iChannels %u, iTrackLengthJiffies %u\n",
                  iContainer->BitDepth(), iContainer->Timescale(), iContainer->SampleRate(), iContainer->Duration(), iContainer->Channels(), iTrackLengthJiffies);*/
    iController->OutputDecodedStream(0, iBitDepth, iSampleRate, iChannels, kCodecAlac, iTrackLengthJiffies, 0, true);
}

TBool CodecRaop::TrySeek(TUint aStreamId, TUint64 aSample)
{
    LOG(kCodec, "CodecRaop::TrySeek(%u, %llu)\n", aStreamId, aSample);
    return false;
}

void CodecRaop::StreamCompleted()
{
    LOG(kCodec, "CodecRaop::StreamCompleted\n");
    CodecAlacBase::StreamCompleted();
}

void CodecRaop::Process() 
{
    //LOG(kCodec, "CodecRaop::Process\n");
    iInBuf.SetBytes(0);

    try {
        // Get size of next packet.
        static const TUint kSizeBytes = 4;
        iController->Read(iInBuf, kSizeBytes);
        if (iInBuf.Bytes() < kSizeBytes) {
            THROW(CodecStreamEnded);
        }

        // Read in next packet.
        try {
            ReaderBuffer readerBuffer(iInBuf);
            ReaderBinary readerBinary(readerBuffer);
            const TUint bytes = readerBinary.ReadUintBe(4);
            iInBuf.SetBytes(0);
            iController->Read(iInBuf, bytes);
            if (iInBuf.Bytes() < bytes) {
                THROW(CodecStreamEnded);
            }
        }
        catch (ReaderError&) {
            THROW(CodecStreamCorrupt);
        }

        Decode();
    }
    catch (CodecStreamStart&) {
        iStreamStarted = true;
        LOG(kCodec, "CodecRaop::Process caught CodecStreamStart\n");
    }
    catch (CodecStreamEnded&) {
        iStreamEnded = true;
        LOG(kCodec, "CodecRaop::Process caught CodecStreamEnded\n");
    }

    OutputFinal();
}

void CodecRaop::ParseFmtp(const Brx& aFmtp)
{
    // SDP FMTP (format parameters) data is received as a string of form:
    // a=fmtp:96 4096 0 16 40 10 14 2 255 0 0 44100
    // Third party ALAC decoder expects the data present in this string to be
    // in a packed binary format, so parse the FMTP and pack it here.

    LOG(kMedia, "CodecRaop::ParseFmtp [%.*s]\n", PBUF(aFmtp));

    Parser p(aFmtp);

    iCodecSpecificData.SetBytes(0);

    try {
        WriterBuffer writerBuf(iCodecSpecificData);
        WriterBinary writerBin(writerBuf);
        writerBin.WriteUint32Be(Ascii::Uint(p.Next()));  // ?
        writerBin.WriteUint32Be(Ascii::Uint(p.Next()));  // max_samples_per_frame
        writerBin.WriteUint8(Ascii::Uint(p.Next()));     // 7a

        iBitDepth = Ascii::Uint(p.Next());               // bit depth
        writerBin.WriteUint8(iBitDepth);

        writerBin.WriteUint8(Ascii::Uint(p.Next()));     // rice_historymult
        writerBin.WriteUint8(Ascii::Uint(p.Next()));     // rice_initialhistory
        writerBin.WriteUint8(Ascii::Uint(p.Next()));     // rice_kmodifier

        iChannels = Ascii::Uint(p.Next());               // 7f
        writerBin.WriteUint8(iChannels);

        writerBin.WriteUint16Be(Ascii::Uint(p.Next()));  // 80
        writerBin.WriteUint32Be(Ascii::Uint(p.Next()));  // 82
        writerBin.WriteUint32Be(Ascii::Uint(p.Next()));  // 86

        iSampleRate = Ascii::Uint(p.Next());
        writerBin.WriteUint32Be(iSampleRate);
    }
    catch (AsciiError&) {
        THROW(CodecStreamCorrupt);
    }
}
