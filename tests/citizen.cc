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
 
#include <iostream>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include "lsst/pex/exceptions.h"
#include "lsst/daf/base/Citizen.h"

class Shoe : public lsst::daf::base::Citizen {
public:
    Shoe(int i = 0) : Citizen(typeid(this)), _i(i) { }
    ~Shoe() { }

    void addMemory(std::size_t size) { addMemoryUse(size); }
    void subtractMemory(std::size_t size) { subtractMemoryUse(size); }
private:
    int _i;
};

class MyClass : public lsst::daf::base::Citizen {
  public:
    MyClass(const char * = 0) :
        Citizen(typeid(this)),
        ptr(new int) {
        *ptr = 0;
    }
    int add_one() { return ++*ptr; }
private:
    boost::scoped_ptr<int> ptr;         // no need to track this alloc
};

using namespace lsst::daf::base;

/************************************************************************************************************/

#define BOOST_TEST_MODULE Citizen
#define BOOST_TEST_DYN_LINK
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#include "boost/test/unit_test.hpp"
#pragma clang diagnostic pop

BOOST_AUTO_TEST_SUITE(CitizenSuite)

MyClass *foo() {
    boost::scoped_ptr<Shoe> x(new Shoe(1));
    MyClass *my_instance = new MyClass();

    BOOST_CHECK_EQUAL(Citizen::countCitizens(), 5);

    return my_instance;
}

BOOST_AUTO_TEST_CASE(all) {
    Citizen::setNewCallbackId(2);
    Citizen::setDeleteCallbackId(2);

    Shoe x;
    const Citizen::memId firstId = Citizen::getNextMemId(); // after allocating x
    
    boost::scoped_ptr<Shoe> y(new Shoe);
    boost::scoped_ptr<Shoe> z(new Shoe(10));

    y->addMemory(1);
    z->addMemory(2);
    
    MyClass *mine = foo();

    std::vector<const Citizen *> const leaks = Citizen::census();
    BOOST_CHECK_EQUAL(leaks.end() - leaks.begin(), 4);
    BOOST_CHECK_EQUAL(Citizen::countCitizens(), 4);
    BOOST_CHECK_EQUAL(Citizen::countCitizens(firstId), 3);

    BOOST_CHECK_EQUAL(Citizen::getTotalMemoryUse(firstId), 3);
    z->subtractMemory(1);
    BOOST_CHECK_EQUAL(Citizen::getTotalMemoryUse(firstId), 2);

    std::map<std::string, std::size_t> numbers = Citizen::getNumberByType(firstId);
    BOOST_CHECK_EQUAL(numbers.size(), 2);
    BOOST_CHECK_EQUAL(numbers[std::string(typeid(Shoe*).name())], 2);
    BOOST_CHECK_EQUAL(numbers[std::string(typeid(MyClass*).name())], 1);

    std::map<std::string, std::size_t> uses = Citizen::getMemoryUseByType(firstId);
    BOOST_CHECK_EQUAL(uses.size(), 2);
    BOOST_CHECK_EQUAL(uses[std::string(typeid(Shoe*).name())], 2);
    BOOST_CHECK_EQUAL(uses[std::string(typeid(MyClass*).name())], 0);

    x.markPersistent();                 // x isn't going to be deleted until main exists, so don't list as a leak
    BOOST_CHECK_EQUAL(Citizen::countCitizens(firstId), 3);

    z.reset();                          // i.e. delete pointed-to object
    delete mine;

#if 0                                   // can crash the program.  Drat.
    ((int *)y.get())[0] = 0;            // deliberately corrupt the block
    BOOST_CHECK_THROW((void)Citizen::checkCorruption(), lsst::pex::exceptions::MemoryException);
    ((int *)y.get())[0] = 0xdeadbeef;   // uncorrupt the block
#endif

    y.reset();
    Citizen::census(std::cout, firstId);
    BOOST_CHECK_EQUAL(Citizen::countCitizens(firstId), 0);
}

BOOST_AUTO_TEST_SUITE_END()

