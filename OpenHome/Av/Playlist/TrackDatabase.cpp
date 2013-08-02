#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Private/Env.h>
//#include <OpenHome/Media/IdManager.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// TrackDatabase

TrackDatabase::TrackDatabase(TrackFactory& aTrackFactory)
    : iLock("TRDB")
    , iTrackFactory(aTrackFactory)
    , iSeq(0)
{
    iTrackList.reserve(kMaxTracks);
}

TrackDatabase::~TrackDatabase()
{
    TrackListUtils::Clear(iTrackList);
}

void TrackDatabase::AddObserver(ITrackDatabaseObserver& aObserver)
{
    iObservers.push_back(&aObserver);
}

void TrackDatabase::GetIdArray(std::array<TUint32, kMaxTracks>& aIdArray, TUint& aSeq) const
{
    AutoMutex a(iLock);
    TUint i;
    for (i=0; i<iTrackList.size(); i++) {
        aIdArray[i] = iTrackList[i]->Id();
    }
    for (i=iTrackList.size(); i<kMaxTracks; i++) {
        aIdArray[i] = kTrackIdNone;
    }
    aSeq = iSeq;
}

void TrackDatabase::GetTrackById(TUint aId, Track*& aTrack) const
{
    AutoMutex a(iLock);
    return GetTrackByIdLocked(aId, aTrack);
}

void TrackDatabase::GetTrackByIdLocked(TUint aId, Track*& aTrack) const
{
    const TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
    aTrack = iTrackList[index];
    aTrack->AddRef();
}

void TrackDatabase::GetTrackById(TUint aId, TUint aSeq, Track*& aTrack, TUint& aIndex) const
{
    AutoMutex a(iLock);
    aTrack = NULL;
    if (iSeq != aSeq) {
        GetTrackByIdLocked(aId, aTrack);
        return;
    }
    if (!TryGetTrackById(aId, aTrack, aIndex, iTrackList.size(), aIndex)) {
        TUint endIndex = std::min(aIndex, (TUint)iTrackList.size());
        if (!TryGetTrackById(aId, aTrack, 0, endIndex, aIndex)) {
            THROW(TrackDbIdNotFound);
        }
    }
}

void TrackDatabase::Insert(TUint aIdAfter, const Brx& aUri, const Brx& aMetaData, TUint& aIdInserted)
{
    AutoMutex a(iLock);
    if (iTrackList.size() == kMaxTracks) {
        THROW(TrackDbFull);
    }
    TUint index = 0;
    if (aIdAfter != kTrackIdNone) {
        index = TrackListUtils::IndexFromId(iTrackList, aIdAfter) + 1;
    }
    Track* track = iTrackFactory.CreateTrack(aUri, aMetaData, NULL);
    aIdInserted = track->Id();
    iTrackList.insert(iTrackList.begin() + index, track);
    iSeq++;
    const TUint idBefore = aIdAfter;
    const TUint idAfter = (index == iTrackList.size()-1? kTrackIdNone : index+1);
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyTrackInserted(*track, idBefore, idAfter);
    }
}

void TrackDatabase::DeleteId(TUint aId)
{
    AutoMutex a(iLock);
    TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
    Track* before = (index==0? NULL : iTrackList[index-1]);
    Track* after = (index==iTrackList.size()-1? NULL : iTrackList[index+1]);
    iTrackList[index]->RemoveRef();
    (void)iTrackList.erase(iTrackList.begin() + index);
    iSeq++;
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyTrackDeleted(aId, before, after);
    }
}

void TrackDatabase::DeleteAll()
{
    iLock.Wait();
    const TBool changed = (iTrackList.size() > 0);
    if (changed) {
        TrackListUtils::Clear(iTrackList);
        iSeq++;
    }
    if (changed) {
        for (TUint i=0; i<iObservers.size(); i++) {
            iObservers[i]->NotifyAllDeleted();
        }
    }
    iLock.Signal();
}

void TrackDatabase::SetObserver(ITrackDatabaseObserver& aObserver)
{
    iLock.Wait();
    AddObserver(aObserver);
    iLock.Signal();
}

Track* TrackDatabase::TrackRef(TUint aId)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    try {
        const TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
        track = iTrackList[index];
        track->AddRef();
    }
    catch (TrackDbIdNotFound&) { }
    return track;
}

Track* TrackDatabase::NextTrackRef(TUint aId)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    if (aId == kTrackIdNone) {
        if (iTrackList.size() > 0) {
            track = iTrackList[0];
            track->AddRef();
        }
    }
    else {
        try {
            const TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
            if (index < iTrackList.size()-1) {
                track = iTrackList[index+1];
                track->AddRef();
            }
        }
        catch (TrackDbIdNotFound&) { }
    }
    return track;
}

Track* TrackDatabase::PrevTrackRef(TUint aId)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    try {
        const TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
        if (index > 0) {
            track = iTrackList[index-1];
            track->AddRef();
        }
    }
    catch (TrackDbIdNotFound&) { }
    return track;
}

Track* TrackDatabase::TrackRefByIndex(TUint aIndex)
{
    Track* track = NULL;
    iLock.Wait();
    if (aIndex < iTrackList.size()) {
        track = iTrackList[aIndex];
        track->AddRef();
    }
    iLock.Signal();
    return track;
}

TBool TrackDatabase::TryGetTrackById(TUint aId, Track*& aTrack, TUint aStartIndex, TUint aEndIndex, TUint& aFoundIndex) const
{
    for (TUint i=aStartIndex; i<aEndIndex; i++) {
        if (iTrackList[i]->Id() == aId) {
            aTrack = iTrackList[i];
            aTrack->AddRef();
            aFoundIndex = i;
            return true;
        }
    }
    return false;
}


// Shuffler

Shuffler::Shuffler(Environment& aEnv, ITrackDatabaseReader& aReader)
    : iLock("TSHF")
    , iEnv(aEnv)
    , iReader(aReader)
    , iObserver(NULL)
    , iShuffle(false)
{
    aReader.SetObserver(*this);
    iShuffleList.reserve(ITrackDatabase::kMaxTracks);
}

Shuffler::~Shuffler()
{
    TrackListUtils::Clear(iShuffleList);
}

void Shuffler::SetShuffle(TBool aShuffle)
{
    iShuffle = aShuffle;
}

void Shuffler::SetObserver(ITrackDatabaseObserver& aObserver)
{
    iLock.Wait();
    iObserver = &aObserver;
    iLock.Signal();
}

Track* Shuffler::TrackRef(TUint aId)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    if (iShuffle) {
        try {
            const TUint index = TrackListUtils::IndexFromId(iShuffleList, aId);
            track = iShuffleList[index];
            track->AddRef();
        }
        catch (TrackDbIdNotFound&) { }
    }
    else {
        track = iReader.TrackRef(aId);
    }
    return track;
}

Track* Shuffler::NextTrackRef(TUint aId)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    if (!iShuffle) {
        track = iReader.NextTrackRef(aId);
    }
    else {
        if (aId == ITrackDatabase::kTrackIdNone) {
            if (iShuffleList.size() > 0) {
                track = iShuffleList[0];
                track->AddRef();
            }
        }
        else {
            try {
                const TUint index = TrackListUtils::IndexFromId(iShuffleList, aId);
                if (index < iShuffleList.size()-1) {
                    track = iShuffleList[index+1];
                    track->AddRef();
                }
            }
            catch (TrackDbIdNotFound&) { }
        }
    }
    return track;
}

Track* Shuffler::PrevTrackRef(TUint aId)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    if (!iShuffle) {
        track = iReader.PrevTrackRef(aId);
    }
    else {
        try {
            const TUint index = TrackListUtils::IndexFromId(iShuffleList, aId);
            if (index != 0) {
                track = iShuffleList[index-1];
                track->AddRef();
            }
        }
        catch (TrackDbIdNotFound&) { }
    }
    return track;
}

Track* Shuffler::TrackRefByIndex(TUint aIndex)
{
    Track* track = NULL;
    AutoMutex a(iLock);
    if (!iShuffle) {
        track = iReader.TrackRefByIndex(aIndex);
    }
    else {
        track = iShuffleList[aIndex];
        track->AddRef();
    }
    return track;
}

void Shuffler::NotifyTrackInserted(Track& aTrack, TUint aIdBefore, TUint aIdAfter)
{
    TUint idBefore = aIdBefore;
    TUint idAfter = aIdAfter;
    iLock.Wait();
    TUint index = 0;
    if (iShuffleList.size() > 0) {
        index = iEnv.Random(iShuffleList.size());
    }
    iShuffleList.insert(iShuffleList.begin() + index, &aTrack);
    aTrack.AddRef();
    if (iShuffle) {
        idBefore = (index == 0? ITrackDatabase::kTrackIdNone : iShuffleList[index-1]->Id());
        idAfter = (index == iShuffleList.size()-1? ITrackDatabase::kTrackIdNone : iShuffleList[index+1]->Id());
    }
    iObserver->NotifyTrackInserted(aTrack, idBefore, idAfter);
    iLock.Signal();
}

void Shuffler::NotifyTrackDeleted(TUint aId, Track* aBefore, Track* aAfter)
{
    AutoMutex a(iLock);
    const TUint index = TrackListUtils::IndexFromId(iShuffleList, aId);
    Track* before = aBefore;
    Track* after = aAfter;
    if (iShuffle) {
        before = (index==0? NULL : iShuffleList[index-1]);
        after = (index==iShuffleList.size()-1? NULL : iShuffleList[index+1]);
    }
    iShuffleList[index]->RemoveRef();
    iShuffleList.erase(iShuffleList.begin() + index);
    iObserver->NotifyTrackDeleted(aId, before, after);
}

void Shuffler::NotifyAllDeleted()
{
    iLock.Wait();
    TrackListUtils::Clear(iShuffleList);
    iObserver->NotifyAllDeleted();
    iLock.Signal();
}


// Repeater

Repeater::Repeater(ITrackDatabaseReader& aReader)
    : iLock("TRPT")
    , iReader(aReader)
    , iObserver(NULL)
    , iRepeat(false)
    , iTrackCount(0)
{
}

void Repeater::SetRepeat(TBool aRepeat)
{
    iRepeat = aRepeat;
}

void Repeater::SetObserver(ITrackDatabaseObserver& aObserver)
{
    iObserver = &aObserver;
    iReader.SetObserver(*this);
}

Track* Repeater::TrackRef(TUint aId)
{
    AutoMutex a(iLock);
    Track* track = iReader.TrackRef(aId);
    if (track == NULL && iRepeat) {
        track = iReader.TrackRef(ITrackDatabase::kTrackIdNone);
    }
    return track;
}

Track* Repeater::NextTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    Track* track = iReader.NextTrackRef(aId);
    if (track == NULL && iRepeat) {
        track = iReader.NextTrackRef(ITrackDatabase::kTrackIdNone);
    }
    return track;
}

Track* Repeater::TrackRefByIndex(TUint aIndex)
{
    return iReader.TrackRefByIndex(aIndex);
}

Track* Repeater::PrevTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    Track* track = iReader.PrevTrackRef(aId);
    if (track == NULL && iRepeat) {
        track = iReader.TrackRefByIndex(iTrackCount-1);
    }
    return track;
}

void Repeater::NotifyTrackInserted(Track& aTrack, TUint aIdBefore, TUint aIdAfter)
{
    AutoMutex a(iLock);
    iTrackCount++;
    ASSERT(iTrackCount <= ITrackDatabase::kMaxTracks);
    iObserver->NotifyTrackInserted(aTrack, aIdBefore, aIdAfter);
}

void Repeater::NotifyTrackDeleted(TUint aId, Track* aBefore, Track* aAfter)
{
    AutoMutex a(iLock);
    iTrackCount--;
    ASSERT(iTrackCount <= ITrackDatabase::kMaxTracks);
    iObserver->NotifyTrackDeleted(aId, aBefore, aAfter);
}

void Repeater::NotifyAllDeleted()
{
    AutoMutex a(iLock);
    iTrackCount = 0;
    iObserver->NotifyAllDeleted();
}


// TrackListUtils

TUint TrackListUtils::IndexFromId(const std::vector<Track*>& aList, TUint aId)
{ // static
    for (TUint i=0; i<aList.size(); i++) {
        if (aList[i]->Id() == aId) {
            return i;
        }
    }
    THROW(TrackDbIdNotFound);
}

void TrackListUtils::Clear(std::vector<Track*>& aList)
{ // static
    for (TUint i=0; i<aList.size(); i++) {
        aList[i]->RemoveRef();
    }
    aList.clear();
}