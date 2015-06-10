// -*- LSST-C++ -*-

/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
 * 
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the LSST License Statement and 
 * the GNU General Public License along with this program.  If not, 
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
 
//! \file
//! \brief Implementation of Citizen

#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>         // should use std::unique_ptr from C++11 when available
#include <boost/format.hpp>
#include <ctype.h>
#include <cerrno>

#include "lsst/daf/base/Citizen.h"
#include "lsst/pex/exceptions.h"
#include "lsst/utils/Demangle.h"

namespace dafBase = lsst::daf::base;

namespace {

template <typename T>
class ThreadPrivate {
public:
    ThreadPrivate(T const& t) : _init(t) {
        int ret = pthread_key_create(&_key, del);
        if (ret != 0) {
            throw LSST_EXCEPT(lsst::pex::exceptions::MemoryException,
                              "Could not create key");
        }
    };
    T& getRef(void) {
        T* d = reinterpret_cast<T*>(pthread_getspecific(_key));
        if (d == 0) {
            d = new T(_init);
            pthread_setspecific(_key, d);
        }
        return *d;
    };

private:
    pthread_key_t _key;
    T _init;

    static void del(void* data) {
        T* d = reinterpret_cast<T*>(data);
        delete d;
    };
};

static ThreadPrivate<dafBase::Citizen::memId> perThreadId(1);
static ThreadPrivate<bool> perThreadPersistFlag(false);

class RwLock {
public:
    RwLock(void) {
        int ret = pthread_rwlock_init(&_lock, 0);
        if (ret != 0) {
            throw LSST_EXCEPT(lsst::pex::exceptions::MemoryException,
                              "Could not create Citizen lock");
        }
    };
    void lock(void) {
        int ret = pthread_rwlock_wrlock(&_lock);
        if (ret != 0) {
            throw LSST_EXCEPT(lsst::pex::exceptions::MemoryException,
                              "Could not acquire Citizen write lock");
        }
    };
    bool rdlock(void) {
        int ret = pthread_rwlock_rdlock(&_lock);
        if (ret == 0) return true;
        if (ret == EDEADLK) return false;
        throw LSST_EXCEPT(lsst::pex::exceptions::MemoryException,
                          "Could not acquire Citizen read lock");
    };
    void unlock(void) {
        int ret = pthread_rwlock_unlock(&_lock);
        if (ret != 0) {
            throw LSST_EXCEPT(lsst::pex::exceptions::MemoryException,
                              "Could not release Citizen lock");
        }
    };

private:
    pthread_rwlock_t _lock;
};

static RwLock citizenLock;

class ReadGuard {
public:
    ReadGuard(RwLock& lock) : _lock(lock) {
        _mustUnlock = _lock.rdlock();
    };
    ~ReadGuard(void) {
        if (_mustUnlock) _lock.unlock();
    };

private:
    RwLock& _lock;
    bool _mustUnlock;
};

class WriteGuard {
public:
    WriteGuard(RwLock& lock) : _lock(lock) {
        _lock.lock();
    };
    ~WriteGuard(void) {
        _lock.unlock();
    };

private:
    RwLock& _lock;
};

} // anonymous namespace

//! Called once when the memory system is being initialised
//
// \brief A class that is instantiated once during startup
//
// The main purpose of CitizenInit is to provide a place to set
// breakpoints to setup memory debugging; see discussion on trac
//
class CitizenInit {
public:
    CitizenInit() : _dummy(1) { }
private:
    volatile int _dummy;
};
 
CitizenInit one;
//
// Con/Destructors
//
dafBase::Citizen::memId dafBase::Citizen::_addCitizen(Citizen const* c) {
    memId cid = _nextMemIdAndIncrement();
    WriteGuard guard(citizenLock);
    if (_shouldPersistCitizens()) {
        _persistentCitizens[c] = std::make_pair(cid, pthread_self());
    } else {
        _activeCitizens[c] = std::make_pair(cid, pthread_self());
    }
    if (cid == _newId) {
        _newId += _newCallback(cid);
    }
    return cid;
}

dafBase::Citizen::Citizen(std::type_info const& type) :
    _sentinel(magicSentinel),
    _CitizenId(_addCitizen(this)),
    _typeName(type.name()) {
}

dafBase::Citizen::Citizen(Citizen const& citizen) :
    _sentinel(magicSentinel),
    _CitizenId(_addCitizen(this)),
    _typeName(citizen._typeName) {
}

dafBase::Citizen::~Citizen() {
    {
        WriteGuard guard(citizenLock);
        if (_CitizenId == _deleteId) {
            _deleteId += _deleteCallback(this);
        }
    }

    (void)_hasBeenCorrupted();  // may execute callback
    _sentinel = 0x0000dead;     // In case we have a dangling pointer

    bool corrupt = false;
    {
        WriteGuard guard(citizenLock);
        size_t nActive = _activeCitizens.erase(this);
        corrupt = nActive > 1 ||
            (nActive == 0 && _persistentCitizens.erase(this) != 1);
    }
    if (corrupt) {
        (void)_corruptionCallback(this);
    }
}

int dafBase::Citizen::init() {
    volatile int dummy = 1;
    return dummy;
}

/******************************************************************************/
//
// Return (some) private state
//
//! Return the Citizen's ID
dafBase::Citizen::memId dafBase::Citizen::getId() const {
    return _CitizenId;
}

//! Return the memId of the next object to be allocated
dafBase::Citizen::memId dafBase::Citizen::getNextMemId() {
    return _nextMemId();
}

//! Return the memId of the next object to be allocated
dafBase::Citizen::memId dafBase::Citizen::_nextMemId() {
    return perThreadId.getRef();
}

//! Return the memId and prepare for the next object to be allocated
dafBase::Citizen::memId dafBase::Citizen::_nextMemIdAndIncrement() {
    return perThreadId.getRef()++;
}

//! Return a string representation of a Citizen
//
std::string dafBase::Citizen::repr() const {
    return boost::str(boost::format("%d: %08x %s")
                      % _CitizenId
                      % this
                      % lsst::utils::demangleType(_typeName)
                     );
}

//! Mark a Citizen as persistent and not destroyed until process end.
void dafBase::Citizen::markPersistent(void) {
    WriteGuard guard(citizenLock);
    _persistentCitizens[this] = _activeCitizens[this];
    _activeCitizens.erase(this);
}

//! \name Census
//! Provide a list of current Citizens
//@{
//
//
//! How many active Citizens are there?
//
std::size_t dafBase::Citizen::countCitizens(
    memId startingMemId                 //!< Don't print Citizens with lower IDs
    ) {
    if (startingMemId == 0) {              // easy
        ReadGuard guard(citizenLock);
        return _activeCitizens.size();
    }

    int n = 0;
    ReadGuard guard(citizenLock);
    for (table::iterator cur = _activeCitizens.begin();
         cur != _activeCitizens.end(); cur++) {
        if (cur->first->_CitizenId >= startingMemId) {
            n++;
        }
    }

    return n;    
}
//
//! Print a list of all active Citizens to stream, sorted by ID
//
void dafBase::Citizen::census(
    std::ostream &stream,               //!< stream to print to
    memId startingMemId                 //!< Don't print Citizens with lower IDs
    ) {
    ReadGuard guard(citizenLock);

    std::vector<Citizen const*> const leaks = Citizen::census(startingMemId);

    for (std::vector<Citizen const *>::const_iterator citizen = leaks.begin(), end = leaks.end();
         citizen != end; ++citizen) {
        stream << (*citizen)->repr() << "\n";
    }
}

/************************************************************************************************************/
namespace {
bool cmpId(dafBase::Citizen const *a, dafBase::Citizen const *b)
{
    return a->getId() < b->getId();
}
} 

std::vector<dafBase::Citizen const*> const dafBase::Citizen::census(memId startingMemId) {
    std::vector<Citizen const*> vec;
    ReadGuard guard(citizenLock);
    vec.reserve(_activeCitizens.size());

    for (table::const_iterator cur = _activeCitizens.begin(); cur != _activeCitizens.end(); cur++) {
        Citizen const* citizen = cur->first;
        if (citizen->getId() >= startingMemId) {
            vec.push_back(citizen);
        }
    }
    vec.reserve(vec.size());
    std::sort(vec.begin(), vec.end(), cmpId);

    return vec;
}

//@}

//! Check for corruption
//! Return true if the block is corrupted, but
//! only after calling the corruptionCallback
bool dafBase::Citizen::_hasBeenCorrupted() const {
    if (_sentinel == static_cast<int>(magicSentinel)) {
        return false;
    }

    (void)_corruptionCallback(this);
    return true;
}

//! Check all allocated blocks for corruption
bool dafBase::Citizen::hasBeenCorrupted() {
    ReadGuard guard(citizenLock);
    for (table::iterator cur = _activeCitizens.begin();
         cur != _activeCitizens.end(); cur++) {
        if (cur->first->_hasBeenCorrupted()) {
            return true;
        }
    }
    for (table::iterator cur = _persistentCitizens.begin();
         cur != _persistentCitizens.end(); cur++) {
        if (cur->first->_hasBeenCorrupted()) {
            return true;
        }
    }

    return false;
}

//! \name callbackIDs
//! Set callback Ids. The old Id is returned
//@{
//
//! Call the NewCallback when block is allocated
dafBase::Citizen::memId dafBase::Citizen::setNewCallbackId(
    Citizen::memId id                   //!< Desired ID
    ) {
    WriteGuard guard(citizenLock);
    Citizen::memId oldId = _newId;
    _newId = id;

    return oldId;
}

//! Call the current DeleteCallback when block is deleted
dafBase::Citizen::memId dafBase::Citizen::setDeleteCallbackId(
    Citizen::memId id                   //!< Desired ID
    ) {
    WriteGuard guard(citizenLock);
    Citizen::memId oldId = _deleteId;
    _deleteId = id;

    return oldId;
}
//@}

//! \name callbacks
//! Set the New/Delete callback functions; in each case
//! the previously installed callback is returned. These
//! callback functions return a value which is Added to
//! the previously registered id.
//!
//! The default callback functions are called
//! default{New,Delete}Callback; you may want to set a break
//! point in these callbacks from your favourite debugger
//

//@{
//! Set the NewCallback function

dafBase::Citizen::memNewCallback dafBase::Citizen::setNewCallback(
    Citizen::memNewCallback func //! The new function to be called when a designated block is allocated
    ) {
    Citizen::memNewCallback old = _newCallback;
    _newCallback = func;

    return old;
}

//! Set the DeleteCallback function
dafBase::Citizen::memCallback dafBase::Citizen::setDeleteCallback(
    Citizen::memCallback func           //!< function be called when desired block is deleted
    ) {
    Citizen::memCallback old = _deleteCallback;
    _deleteCallback = func;

    return old;
}
    
//! Set the CorruptionCallback function
dafBase::Citizen::memCallback dafBase::Citizen::setCorruptionCallback(
    Citizen::memCallback func //!< function be called when block is found to be corrupted
                                                   ) {
    Citizen::memCallback old = _corruptionCallback;
    _corruptionCallback = func;

    return old;
}
    
//! Default callbacks.
//!
//! Note that these may well be the target of debugger breakpoints, so e.g. dId
//! may well be changed behind our back
//@{
//! Default NewCallback
dafBase::Citizen::memId defaultNewCallback(
                                           dafBase::Citizen::memId const cid //!< ID for just-allocated Citizen
                                 ) {
    static int dId = 0;             // how much to incr memId
    std::cerr << boost::format("Allocating memId %d\n") % cid;

    return dId;
}

//! Default DeleteCallback
dafBase::Citizen::memId defaultDeleteCallback(dafBase::Citizen const* ptr //!< About-to-be deleted Citizen
                                    ) {
    static int dId = 0;             // how much to incr memId
    std::cerr << boost::format("Deleting memId %s\n") % ptr->repr();

    return dId;
}

//! Default CorruptionCallback
dafBase::Citizen::memId defaultCorruptionCallback(dafBase::Citizen const* ptr //!< About-to-be deleted Citizen
                              ) {
    throw LSST_EXCEPT(lsst::pex::exceptions::MemoryException,
                      str(boost::format("Citizen \"%s\" is corrupted") % ptr->repr()));

    return ptr->getId();                // NOTREACHED
}

bool& dafBase::Citizen::_shouldPersistCitizens(void) {
    return perThreadPersistFlag.getRef();
}

//@}
//
// Initialise static members
//
dafBase::Citizen::memId dafBase::Citizen::_newId = 0;
dafBase::Citizen::memId dafBase::Citizen::_deleteId = 0;
dafBase::Citizen::table dafBase::Citizen::_activeCitizens;
dafBase::Citizen::table dafBase::Citizen::_persistentCitizens;

dafBase::Citizen::memNewCallback dafBase::Citizen::_newCallback = defaultNewCallback;
dafBase::Citizen::memCallback dafBase::Citizen::_deleteCallback = defaultDeleteCallback;
dafBase::Citizen::memCallback dafBase::Citizen::_corruptionCallback = defaultCorruptionCallback;


dafBase::PersistentCitizenScope::PersistentCitizenScope() {
    Citizen::_shouldPersistCitizens() = true;
}

dafBase::PersistentCitizenScope::~PersistentCitizenScope() {
    Citizen::_shouldPersistCitizens() = false;
}
