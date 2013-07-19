#ifndef HEADER_PROVIDER_UTILS
#define HEADER_PROVIDER_UTILS

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Standard.h>

namespace OpenHome {
namespace Net {
    class IDvInvocationResponseString;
} // namespace Net
namespace Av {

class WriterInvocationResponseString : public IWriter, private INonCopyable
{
public:
    WriterInvocationResponseString(Net::IDvInvocationResponseString& aIrs);
    ~WriterInvocationResponseString();
public: // from IWriter
    void Write(TByte aValue);
    void Write(const Brx& aBuffer);
    void WriteFlush();
private:
    Net::IDvInvocationResponseString& iIrs;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_PROVIDER_UTILS
