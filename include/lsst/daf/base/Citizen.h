// -*- lsst-c++ -*-

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
 
#ifndef LSST_DAF_BASE_CITIZEN_H
#define LSST_DAF_BASE_CITIZEN_H

#include <map>
#include <ostream>
#include <pthread.h>
#include <string>
#include <vector>
#include <typeinfo>

#include "boost/noncopyable.hpp"


namespace lsst {
namespace daf {
namespace base {

     class PersistentCitizenScope;

/*! \brief Citizen is a class that should be among all LSST
 * classes base classes, and handles basic memory management
 *
 * Instances of subclasses of Citizen will automatically be
 * given a unique id.
 *
 * You can ask for infomation about the currently allocated
 * Citizens using the census functions, request that
 * a function of your choice be called when a specific
 * block ID is allocated or deleted, and check whether any
 * of the data blocks are known to be corrupted
 */
    class Citizen {
    public:
        //! Type of the block's ID
        typedef unsigned long memId;
        //! A function used to register a callback
        typedef memId (*memNewCallback)(const memId cid);
        typedef memId (*memCallback)(const Citizen *ptr);

        /// Constructor
        ///
        /// We require the type name, that is usually provided by calling
        /// 'typeid(this)'.
        Citizen(const std::type_info &type);

        Citizen(Citizen const &);
        ~Citizen();
        Citizen & operator=(Citizen const &) { return *this; }

        std::string repr() const;

        /// Mark a Citizen as persistent
        ///
        /// Persistent Citizens are not included in a census or count.
        void markPersistent(void);

        /// Return the number of active Citizens
        ///
        /// Active Citizens with ID less than startingMemId are not included.
        /// Citizens marked as persistent are not included.
        static std::size_t countCitizens(memId startingMemId=0);

        /// Deprecated function to return the number of active Citizens
        ///
        /// @deprecated Use countCitizens instead, which doesn't require
        ///   the dummy 'int' argument for overloading.
        static std::size_t census(int, memId startingMemId=0) { return countCitizens(startingMemId); }

        /// Print a summary of active Citizens
        ///
        /// Active Citizens with ID less than startingMemId are not included.
        /// Citizens marked as persistent are not included.
        ///
        /// @param stream  Stream to which to print
        /// @param startingMemId  Ignore Citizens with ID less than this
        static void census(std::ostream &stream, memId startingMemId = 0);

        /// Return a list of active Citizens
        ///
        /// The list is sorted by ID.  Citizens with ID less than startingMemId
        /// are not included.
        /// Citizens marked as persistent are not included.
        static std::vector<Citizen const*> const census(memId startingMemId=0);

        /// Return whether any Citizens have been corrupted
        ///
        /// The check for corruption is not exhaustive, but should catch basic
        /// underrun and overrun.
        static bool hasBeenCorrupted();
        
        /// Return the Citizen ID
        memId getId() const;
        
        /// Return the next Citizen ID that will be used
        static memId getNextMemId();

        //{
        /// Set the ID for the next callback
        static memId setNewCallbackId(memId id);
        static memId setDeleteCallbackId(memId id);
        //}
        //{
        /// Set a callback
        static memNewCallback setNewCallback(memNewCallback func);
        static memCallback setDeleteCallback(memCallback func);
        static memCallback setCorruptionCallback(memCallback func);
        //}
        enum { magicSentinel = 0xdeadbeef }; //!< a magic known bit pattern

        /// Initialise the Citizen system
        ///
        /// This is called once when the memory system is being initialised
        /// This allows this routine to be used as a place to set
        /// breakpoints to setup memory debugging
        static int init();
    private:
        typedef std::pair<memId, pthread_t> CitizenInfo;
        typedef std::map<Citizen const*, CitizenInfo> table;

        int _sentinel;                  //< Initialised to _magicSentinel to detect overwritten memory
        memId _CitizenId;               //< unique identifier for this pointer
        const char *_typeName;          //< typeid()->name

        static memId _addCitizen(Citizen const* c);
        static memId _nextMemIdAndIncrement(void);
        //
        // Book-keeping for _CitizenId
        //
        static memId _nextMemId(void);
        static table _activeCitizens;
        static table _persistentCitizens;
        static bool& _shouldPersistCitizens();
        //
        // Callbacks
        //
        static memId _newId;       // call _newCallback when _newID is allocated
        static memId _deleteId;    // call _deleteCallback when _deleteID is deleted

        static memNewCallback _newCallback;
        static memCallback _deleteCallback;        
        static memCallback _corruptionCallback;        
        //
        bool _hasBeenCorrupted() const;

        friend class PersistentCitizenScope;
    };

#ifndef SWIG
    /**
     * A PersistentCitizenScope object causes all Citizen objects created during its lifetime
     * to be marked as persistent. This is useful when constructing static objects that contain
     * a heirarchy of other Citizens which would otherwise need to be marked persistent on an
     * individual basis.
     *
     * @sa Citizen::markPersistent()
     */
    class PersistentCitizenScope : private boost::noncopyable {
    public:
        PersistentCitizenScope();
        ~PersistentCitizenScope();
    };
#endif

}}} // namespace lsst::daf::base

#endif
