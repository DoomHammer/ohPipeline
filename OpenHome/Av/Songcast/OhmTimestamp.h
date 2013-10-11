#ifndef HEADER_OHM_TIMESTAMP
#define HEADER_OHM_TIMESTAMP

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Standard.h>

namespace OpenHome {
    class Environment;
namespace Av {

class IOhmTimestamper
{
public:
    virtual ~IOhmTimestamper() {}
    virtual TUint Timestamp() = 0;
};

class DefaultTimestamper : public IOhmTimestamper, private INonCopyable // trivial implementation that just returns the current time
{
public:
    DefaultTimestamper(Environment& aEnv);
private: // from IOhmTimestamper
    TUint Timestamp();
private:
    Environment& iEnv;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_OHM_TIMESTAMP
