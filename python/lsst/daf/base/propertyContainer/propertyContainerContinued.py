#
# LSST Data Management System
#
# Copyright 2008-2017  AURA/LSST.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <http://www.lsstcorp.org/LegalNotices/>.
#


__all__ = ["getstate", "setstate"]

import enum
import numbers
import warnings

from lsst.utils import continueClass

from .propertySet import PropertySet
from .propertyList import PropertyList
import lsst.pex.exceptions
from ..dateTime import DateTime


class ReturnStyle(enum.Enum):
    ARRAY = enum.auto()
    SCALAR = enum.auto()
    AUTO = enum.auto()


def _propertyContainerElementTypeName(container, name):
    """Return name of the type of a particular element"""
    t = container.typeOf(name)
    for checkType in ("Bool", "Short", "Int", "Long", "LongLong", "Float", "Double", "String", "DateTime"):
        if t == getattr(container, "TYPE_" + checkType):
            return checkType
    return None


def _propertyContainerGet(container, name, returnStyle):
    """Get a value of unknown type as a scalar or array

    Parameters
    ----------
    container : ``lsst.daf.base.PropertySet`` or ``lsst.daf.base.PropertyList``
        Container from which to get the value
    name : ``str``
        Name of item
    returnStyle : ``ReturnStyle``
        Control whether numeric or string data is returned as an array
        or scalar (the other types, ``PropertyList``, ``PropertySet``
            and ``PersistablePtr``, are always returned as a scalar):
        - ReturnStyle.ARRAY: return numeric or string data types
            as an array of values.
        - ReturnStyle.SCALAR: return numeric or string data types
            as a single value; if the item has multiple values then
            return the last value.
        - ReturnStyle.AUTO: (deprecated) return numeric or string data
            as a scalar if there is just one item, or as an array
            otherwise.
    """
    if not container.exists(name):
        raise lsst.pex.exceptions.NotFoundError(name + " not found")
    if returnStyle not in ReturnStyle:
        raise ValueError("returnStyle {} must be a ReturnStyle".format(returnStyle))

    elemType = _propertyContainerElementTypeName(container, name)
    if elemType:
        value = getattr(container, "getArray" + elemType)(name)
        if returnStyle == ReturnStyle.ARRAY or (returnStyle == ReturnStyle.AUTO and len(value) > 1):
            return value
        return value[-1]

    try:
        return container.getAsPropertyListPtr(name)
    except Exception:
        pass
    if container.typeOf(name) == container.TYPE_PropertySet:
        return container.getAsPropertySetPtr(name)
    try:
        return container.getAsPersistablePtr(name)
    except Exception:
        pass
    raise lsst.pex.exceptions.TypeError('Unknown PropertySet value type for ' + name)


def _guessIntegerType(container, name, value):
    """Given an existing container and name, determine the type
    that should be used for the supplied value. The supplied value
    is assumed to be a scalar.

    On Python 3 all ints are LongLong but we need to be able to store them
    in Int containers if that is what is being used (testing for truncation).
    Int is assumed to mean 32bit integer (2147483647 to -2147483648).

    If there is no pre-existing value we have to decide what to do. For now
    we pick Int if the value is less than maxsize.

    Returns None if the value supplied is a bool or not an integral value.
    """
    useType = None
    maxInt = 2147483647
    minInt = -2147483648

    # We do not want to convert bool to int so let the system work that
    # out itself
    if isinstance(value, bool):
        return useType

    if isinstance(value, numbers.Integral):
        try:
            containerType = _propertyContainerElementTypeName(container, name)
        except lsst.pex.exceptions.NotFoundError:
            # nothing in the container so choose based on size. Safe option is to
            # always use LongLong
            if value <= maxInt and value >= minInt:
                useType = "Int"
            else:
                useType = "LongLong"
        else:
            if containerType == "Int":
                # Always use an Int even if we know it won't fit. The later
                # code will trigger OverflowError if appropriate. Setting the
                # type to LongLong here will trigger a TypeError instead so it's
                # best to trigger a predictable OverflowError.
                useType = "Int"
            elif containerType == "LongLong":
                useType = "LongLong"
    return useType


def _propertyContainerSet(container, name, value, typeMenu, *args):
    """Set a single Python value of unknown type"""
    if hasattr(value, "__iter__") and not isinstance(value, str):
        exemplar = value[0]
    else:
        exemplar = value

    t = type(exemplar)
    setType = _guessIntegerType(container, name, exemplar)

    if setType is not None or t in typeMenu:
        if setType is None:
            setType = typeMenu[t]
        return getattr(container, "set" + setType)(name, value, *args)
    # Allow for subclasses
    for checkType in typeMenu:
        if isinstance(exemplar, checkType):
            return getattr(container, "set" + typeMenu[checkType])(name, value, *args)
    raise lsst.pex.exceptions.TypeError("Unknown value type for %s: %s" % (name, t))


def _propertyContainerAdd(container, name, value, typeMenu, *args):
    """Add a single Python value of unknown type"""
    if hasattr(value, "__iter__"):
        exemplar = value[0]
    else:
        exemplar = value

    t = type(exemplar)
    addType = _guessIntegerType(container, name, exemplar)

    if addType is not None or t in typeMenu:
        if addType is None:
            addType = typeMenu[t]
        return getattr(container, "add" + addType)(name, value, *args)
    # Allow for subclasses
    for checkType in typeMenu:
        if isinstance(exemplar, checkType):
            return getattr(container, "add" + typeMenu[checkType])(name, value, *args)
    raise lsst.pex.exceptions.TypeError("Unknown value type for %s: %s" % (name, t))


def getstate(container):
    """Get the state of a PropertySet or PropertyList in a form that
    can be pickled.

    Parameters
    ----------
    container : `PropertySet` or `PropertyList`
        The property container.

    Returns
    -------
    state : `list`
        The state, as a list of tuples, each of which contains
        the following 3 or 4 items, depending on the type of ``container``:
        - name (a `str`): the name of the item
        - elementTypeName (a `str`): the suffix of a ``setX`` method name
            which is appropriate for the data type. For example integer
            data has ``elementTypeName="Int"` which corresponds to
            the ``setInt`` method.
        - value: the data for the item, as returned by
          ``propertySetContainer`` with ``returnStyle=AUTO``.
        - comment (a `str`): the comment. This item is only present
            if ``container`` is a PropertyList.
    """
    if isinstance(container, PropertyList):
        return [(name, _propertyContainerElementTypeName(container, name),
                 _propertyContainerGet(container, name, returnStyle=ReturnStyle.AUTO),
                 container.getComment(name))
                for name in container.getOrderedNames()]
    else:
        return [(name, _propertyContainerElementTypeName(container, name),
                 _propertyContainerGet(container, name, returnStyle=ReturnStyle.AUTO))
                for name in container.paramNames(False)]


def setstate(container, state):
    """Restore the state of a PropertySet or PropertyList, in place.

    Parameters
    ----------
    container : `PropertySet` or `PropertyList`
        The property container whose state is to be restored.
        It should be empty to start with and is updated in place.
    state : `list`
        The state, as returned by ``getstate``
    """
    if isinstance(container, PropertyList):
        for name, elemType, value, comment in state:
            getattr(container, "set" + elemType)(name, value, comment)
    else:
        for name, elemType, value in state:
            getattr(container, "set" + elemType)(name, value)


def _makePropertyList(state):
    """Make a ``PropertyList`` from the state returned by ``getstate``

    Parameters
    ----------
    state : `list`
        The data returned by ``getstate``.
    """
    pl = PropertyList()
    setstate(pl, state)
    return pl


@continueClass
class PropertySet:
    # Mapping of type to method names;
    # int types are omitted due to use of _guessIntegerType
    _typeMenu = {bool: "Bool",
                 float: "Double",
                 str: "String",
                 DateTime: "DateTime",
                 PropertySet: "PropertySet",
                 PropertyList: "PropertySet",
                 }

    # Map unicode to String, but this only works on Python 2
    # so catch the error and do nothing on Python 3.
    try:
        _typeMenu[unicode] = "String"  # noqa F821
    except Exception:
        pass

    def get(self, name):
        """Return an item as a scalar or array

        Return an array if the item is of numeric or string type and has
        more than one value, otherwise return a scalar.

        .. deprecated:: 20180-06
                  `get` is superseded by `getArray` or `getScalar`

        Parameters
        ----------
        name : ``str``
            Name of item

        Raises
        ------
        lsst.pex.exceptions.NotFoundError
            If the item does not exist.
        """
        warnings.warn("Use getArray or getScalar instead", DeprecationWarning, stacklevel=2)
        return _propertyContainerGet(self, name, returnStyle=ReturnStyle.AUTO)

    def getArray(self, name):
        """Return an item as an array if the item is numeric or string

        If the item is a ``PropertySet``, ``PropertyList`` or
        ``lsst.daf.base.PersistablePtr`` then return the item as a scalar.

        Parameters
        ----------
        name : ``str``
            Name of item

        Raises
        ------
        lsst.pex.exceptions.NotFoundError
            If the item does not exist.
        """
        return _propertyContainerGet(self, name, returnStyle=ReturnStyle.ARRAY)

    def getScalar(self, name):
        """Return an item as a scalar

        If the item has more than one value then the last value is returned

        Parameters
        ----------
        name : ``str``
            Name of item

        Raises
        ------
        lsst.pex.exceptions.NotFoundError
            If the item does not exist.
        """
        return _propertyContainerGet(self, name, returnStyle=ReturnStyle.SCALAR)

    def set(self, name, value):
        """Set the value of an item

        If the item already exists it is silently replaced; the types
        need not match.

        Parameters
        ----------
        name : ``str``
            Name of item
        value : any supported type
            Value of item; may be a scalar or array
        """
        return _propertyContainerSet(self, name, value, self._typeMenu)

    def add(self, name, value):
        """Append one or more values to a given item, which need not exist

        If the item exists then the new value(s) are appended;
        otherwise it is like calling `set`

        Parameters
        ----------
        name : ``str``
            Name of item
        value : any supported type
            Value of item; may be a scalar or array

        Notes
        -----
        If `value` is an ``lsst.daf.base.PropertySet`` or
        ``lsst.daf.base.PropertyList`` then `value` replaces
        the existing value. Also the item is added as a live
        reference, so updating `value` will update this container
        and vice-versa.

        Raises
        ------
        lsst::pex::exceptions::TypeError
            If the type of `value` is incompatible with the existing value
            of the item.
        """
        return _propertyContainerAdd(self, name, value, self._typeMenu)

    def toDict(self):
        """Returns a (possibly nested) dictionary with all properties.
        """

        d = {}
        for name in self.names():
            v = _propertyContainerGet(self, name, returnStyle=ReturnStyle.AUTO)

            if isinstance(v, PropertySet):
                d[name] = PropertySet.toDict(v)
            else:
                d[name] = v
        return d


@continueClass
class PropertyList:
    # Mapping of type to method names
    _typeMenu = {bool: "Bool",
                 int: "Int",
                 float: "Double",
                 str: "String",
                 DateTime: "DateTime",
                 PropertySet: "PropertySet",
                 PropertyList: "PropertySet",
                 }

    # Map unicode to String, but this only works on Python 2
    # so catch the error and do nothing on Python 3.
    try:
        _typeMenu[unicode] = "String"  # noqa F821
    except Exception:
        pass

    def get(self, name):
        """Return an item as a scalar or array

        Return an array if the item has more than one value,
        otherwise return a scalar.

        .. deprecated:: 20180-06
                  `get` is superseded by `getArray` or `getScalar`

        Parameters
        ----------
        name : ``str``
            Name of item

        Raises
        ------
        lsst.pex.exceptions.NotFoundError
            If the item does not exist.
        """
        warnings.warn("Use getArray or getScalar instead", DeprecationWarning, stacklevel=2)
        return _propertyContainerGet(self, name, returnStyle=ReturnStyle.AUTO)

    def getArray(self, name):
        """Return an item as an array

        Parameters
        ----------
        name : ``str``
            Name of item

        Raises
        ------
        lsst.pex.exceptions.NotFoundError
            If the item does not exist.
        """
        return _propertyContainerGet(self, name, returnStyle=ReturnStyle.ARRAY)

    def getScalar(self, name):
        """Return an item as a scalar

        If the item has more than one value then the last value is returned

        Parameters
        ----------
        name : ``str``
            Name of item

        Raises
        ------
        lsst.pex.exceptions.NotFoundError
            If the item does not exist.
        """
        return _propertyContainerGet(self, name, returnStyle=ReturnStyle.SCALAR)

    def set(self, name, value, comment=None):
        """Set the value of an item

        If the item already exists it is silently replaced; the types
        need not match.

        Parameters
        ----------
        name : ``str``
            Name of item
        value : any supported type
            Value of item; may be a scalar or array
        """
        args = []
        if comment is not None:
            args.append(comment)
        return _propertyContainerSet(self, name, value, self._typeMenu, *args)

    def add(self, name, value, comment=None):
        """Append one or more values to a given item, which need not exist

        If the item exists then the new value(s) are appended;
        otherwise it is like calling `set`

        Parameters
        ----------
        name : ``str``
            Name of item
        value : any supported type
            Value of item; may be a scalar or array

        Notes
        -----
        If `value` is an ``lsst.daf.base.PropertySet`` items are added
        using dotted names (e.g. if name="a" and value contains
        an item "b" which is another PropertySet and contains an
        item "c" which is numeric or string, then the value of "c"
        is added as "a.b.c", appended to the existing values of
        "a.b.c" if any (in which case the types must be compatible).

        Raises
        ------
        lsst::pex::exceptions::TypeError
            If the type of `value` is incompatible with the existing value
            of the item.
        """
        args = []
        if comment is not None:
            args.append(comment)
        return _propertyContainerAdd(self, name, value, self._typeMenu, *args)

    def toList(self):
        orderedNames = self.getOrderedNames()
        ret = []
        for name in orderedNames:
            if self.isArray(name):
                values = _propertyContainerGet(self, name, returnStyle=ReturnStyle.AUTO)
                for v in values:
                    ret.append((name, v, self.getComment(name)))
            else:
                ret.append((name, _propertyContainerGet(self, name, returnStyle=ReturnStyle.AUTO),
                            self.getComment(name)))
        return ret

    def toOrderedDict(self):
        """Return an ordered dictionary with all properties in the order that
        they were inserted.
        """
        from collections import OrderedDict

        d = OrderedDict()
        for name in self.getOrderedNames():
            d[name] = _propertyContainerGet(self, name, returnStyle=ReturnStyle.AUTO)
        return d

    def __reduce__(self):
        # It would be a bit simpler to use __setstate__ and __getstate__.
        # However, implementing __setstate__ in Python causes segfaults
        # because pickle creates a new instance by calling
        # object.__new__(PropertyList, *args) which bypasses
        # the pybind11 memory allocation step.
        return (_makePropertyList, (getstate(self),))
