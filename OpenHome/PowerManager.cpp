#include <OpenHome/PowerManager.h>
#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Configuration/ConfigManager.h>

using namespace OpenHome;
using namespace OpenHome::Configuration;

// PowerManager

const Brn PowerManager::kConfigKey("StartupStandby");
const TUint PowerManager::kConfigIdStartupStandbyDisabled = 0;
const TUint PowerManager::kConfigIdStartupStandbyEnabled  = 1;

PowerManager::PowerManager(IConfigInitialiser& aConfigInit)
    : iNextPowerId(0)
    , iNextStandbyId(0)
    , iPowerDown(false)
    , iStandby(false)
    , iLock("PMLO")
{
    const int arr[] ={ kConfigIdStartupStandbyDisabled, kConfigIdStartupStandbyEnabled };
    std::vector<TUint> options(arr, arr + sizeof(arr)/sizeof(arr[0]));
    iConfigStartupStandby = new ConfigChoice(aConfigInit, kConfigKey, options, kConfigIdStartupStandbyDisabled);
    auto id = iConfigStartupStandby->Subscribe(MakeFunctorConfigChoice(*this, &PowerManager::StartupStandbyChanged));
    // we don't care about run-time changes to iConfigStartupStandby so can unsubscribe as soon as we've read its initial value
    iConfigStartupStandby->Unsubscribe(id);
}

PowerManager::~PowerManager()
{
    AutoMutex _(iLock);
    ASSERT(iPowerObservers.empty());
    delete iConfigStartupStandby;
}

void PowerManager::NotifyPowerDown()
{
    // FIXME - the caller of power down should provide some kind of interrupt
    // for stopping any non-essential store tasks in progress
    AutoMutex _(iLock);
    ASSERT(!iPowerDown);
    iPowerDown = true;
    for (auto it = iPowerObservers.cbegin(); it != iPowerObservers.cend(); ++it) {
        IPowerHandler& handler = (*it)->PowerHandler();
        handler.PowerDown();
    }
}

void PowerManager::StandbyEnable()
{
    AutoMutex _(iLock);
    if (iStandby) {
        return;
    }
    iStandby = true;
    for (auto it = iStandbyObservers.cbegin(); it != iStandbyObservers.cend(); ++it) {
        (*it)->Handler().StandbyEnabled();
    }
}

void PowerManager::StandbyDisable(StandbyDisableReason aReason)
{
    AutoMutex _(iLock);
    if (!iStandby) {
        return;
    }
    iStandby = false;
    iLastDisableReason = aReason;
    for (auto it = iStandbyObservers.cbegin(); it != iStandbyObservers.cend(); ++it) {
        (*it)->Handler().StandbyDisabled(aReason);
    }
}

IPowerManagerObserver* PowerManager::RegisterPowerHandler(IPowerHandler& aHandler, TUint aPriority)
{
    ASSERT(aPriority <= kPowerPriorityHighest)
    ASSERT(aPriority >= kPowerPriorityLowest); // shouldn't matter as lowest is 0, and parameter type is TUint

    AutoMutex a(iLock);
    if (iPowerDown) {
        return new PowerManagerObserverNull();
    }

    PowerManagerObserver* observer = new PowerManagerObserver(*this, aHandler, iNextPowerId++, aPriority);

    PriorityList::iterator it;
    for (it = iPowerObservers.begin(); it != iPowerObservers.end(); ++it) {
        if ((*it)->Priority() < observer->Priority()) {
            iPowerObservers.insert(it, observer);
            break;
        }
    }

    if (it == iPowerObservers.cend()) {
        // Callback is lower priority than anything in list.
        iPowerObservers.push_back(observer);
    }

    aHandler.PowerUp();
    return observer;
}

IStandbyObserver* PowerManager::RegisterStandbyHandler(IStandbyHandler& aHandler)
{
    AutoMutex _(iLock);
    auto* observer = new StandbyObserver(*this, aHandler, iNextStandbyId++);
    iStandbyObservers.push_back(observer);
    if (iStandby) {
        aHandler.StandbyEnabled();
    }
    else {
        aHandler.StandbyDisabled(iLastDisableReason);
    }
    return observer;
}

// Called from destructor of PowerManagerObserver.
void PowerManager::DeregisterPower(TUint aId)
{
    AutoMutex _(iLock);
    for (auto it = iPowerObservers.begin(); it != iPowerObservers.end(); ++it) {
        if ((*it)->Id() == aId) {
            // Call PowerDown() on handler under normal shutdown circumstances.
            // PowerDown() may have been invoked on the PowerManager itself,
            // and object destruction started before power failure. Don't want
            // to call PowerDown() on the handler again in that case.
            if (!iPowerDown) {
                IPowerHandler& handler = (*it)->PowerHandler();
                handler.PowerDown();
            }
            iPowerObservers.erase(it);
            return;
        }
    }
}

void PowerManager::DeregisterStandby(TUint aId)
{
    AutoMutex _(iLock);
    for (auto it = iStandbyObservers.begin(); it != iStandbyObservers.end(); ++it) {
        if ((*it)->Id() == aId) {
            iStandbyObservers.erase(it);
            return;
        }
    }
}

void PowerManager::StartupStandbyChanged(KeyValuePair<TUint>& aKvp)
{
    iStandby = (aKvp.Value() == kConfigIdStartupStandbyEnabled);
    iLastDisableReason = eStandbyDisableUser; // this callback only runs during PowerManager c'tor - assume this only happens due to user interaction
}


// PowerManagerObserverNull

PowerManagerObserverNull::~PowerManagerObserverNull()
{
    // Not actually registered in PowerManager, so do nothing.
}


// PowerManagerObserver

PowerManagerObserver::PowerManagerObserver(PowerManager& aPowerManager, IPowerHandler& aHandler, TUint aId, TUint aPriority)
    : iPowerManager(aPowerManager)
    , iHandler(aHandler)
    , iId(aId)
    , iPriority(aPriority)
{
}

PowerManagerObserver::~PowerManagerObserver()
{
    iPowerManager.DeregisterPower(iId);
}

IPowerHandler& PowerManagerObserver::PowerHandler() const
{
    return iHandler;
}

TUint PowerManagerObserver::Id() const
{
    return iId;
}

TUint PowerManagerObserver::Priority() const
{
    return iPriority;
}


// StandbyObserver

StandbyObserver::StandbyObserver(PowerManager& aPowerManager, IStandbyHandler& aHandler, TUint aId)
    : iPowerManager(aPowerManager)
    , iHandler(aHandler)
    , iId(aId)
{
}

StandbyObserver::~StandbyObserver()
{
    iPowerManager.DeregisterStandby(iId);
}

IStandbyHandler& StandbyObserver::Handler() const
{
    return iHandler;
}

TUint StandbyObserver::Id() const
{
    return iId;
}


// StoreVal

StoreVal::StoreVal(IStoreReadWrite& aStore, const Brx& aKey)
    : iObserver(nullptr)
    , iStore(aStore)
    , iKey(aKey)
    , iLock("STVM")
{
}

void StoreVal::PowerDown()
{
    AutoMutex a(iLock);
    Write();
}


// StoreInt

StoreInt::StoreInt(IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, TInt aDefault)
    : StoreVal(aStore, aKey)
    , iVal(aDefault)
{
    // Cannot allow StoreVal to register, as IPowerManager will call PowerUp()
    // after successful registration, and can't successfully call an overridden
    // virtual function from constructor (or destructor).
    iObserver = aPowerManager.RegisterPowerHandler(*this, aPriority);
}

StoreInt::~StoreInt()
{
    delete iObserver;
}

TInt StoreInt::Get() const
{
    AutoMutex a(iLock);
    return iVal;
}

void StoreInt::Set(TInt aValue)
{
    AutoMutex a(iLock);
    iVal = aValue;
}

void StoreInt::Write(const Brx& aKey, TInt aValue, Configuration::IStoreReadWrite& aStore)
{ // static
    Bws<sizeof(TInt)> buf;
    WriterBuffer writerBuf(buf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aValue);
    aStore.Write(aKey, buf);
}

void StoreInt::PowerUp()
{
    AutoMutex _(iLock);
    // read value from store (if it exists; otherwise write default)
    Bws<sizeof(TInt)> buf;
    try {
        iStore.Read(iKey, buf);
        iVal = Converter::BeUint32At(buf, 0);
    }
    catch (StoreKeyNotFound&) {
        Write();
    }
}

void StoreInt::Write()
{
    Write(iKey, iVal, iStore);
}


// StoreText

StoreText::StoreText(IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, const Brx& aDefault, TUint aMaxLength)
    : StoreVal(aStore, aKey)
    , iVal(aMaxLength)
{
    iVal.Replace(aDefault);
    iObserver = aPowerManager.RegisterPowerHandler(*this, aPriority);
}

StoreText::~StoreText()
{
    delete iObserver;
}

void StoreText::Get(Bwx& aBuf) const
{
    AutoMutex a(iLock);
    aBuf.Replace(iVal);
}

void StoreText::Set(const Brx& aValue)
{
    AutoMutex a(iLock);
    iVal.Replace(aValue);
}

void StoreText::PowerUp()
{
    AutoMutex _(iLock);
    try {
        iStore.Read(iKey, iVal);
    }
    catch (StoreKeyNotFound&) {
        Write();
    }
}

void StoreText::Write()
{
    iStore.Write(iKey, iVal);
}
