#include <OpenHome/Media/Protocol/ContentAudio.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Buffer.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// ContentAudio

ContentAudio::ContentAudio(ISupply& aSupply)
    : iSupply(aSupply)
{
}

TBool ContentAudio::Recognise(const Brx& /*aUri*/, const Brx& /*aMimeType*/, const Brx& /*aData*/)
{
    /* Assume that this processor will be offered content last.
       Content we don't support will be rejected.
       ...so we might as well have a go at treating everything as audio. */
    return true;
}

ProtocolStreamResult ContentAudio::Stream(IProtocolReader& aReader, TUint64 aTotalBytes)
{
    ProtocolStreamResult res = EProtocolStreamSuccess;
    TUint bytes = EncodedAudio::kMaxBytes;
    const TBool finite = (aTotalBytes!=0);
    try {
        for (;;) {
            if (finite && aTotalBytes < bytes) {
                bytes = (TUint)aTotalBytes;
            }
            Brn buf = aReader.Read(bytes);
            iSupply.OutputData(buf);
            if (finite) {
                aTotalBytes -= buf.Bytes();
                if (aTotalBytes == 0) {
                    break;
                }
            }
        }
    }
    catch (ReaderError&) {
        res = EProtocolStreamErrorRecoverable;
        Brn buf = aReader.ReadRemaining();
        iSupply.OutputData(buf);
        if (finite) {
            aTotalBytes -= buf.Bytes();
        }
    }
    return res;
}
