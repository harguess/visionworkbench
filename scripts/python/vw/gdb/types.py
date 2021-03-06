#!/usr/bin/env python
# __BEGIN_LICENSE__
# Copyright (C) 2006-2010 United States Government as represented by
# the Administrator of the National Aeronautics and Space Administration.
# All Rights Reserved.
# __END_LICENSE__


import re
import sys
import gdb

# Reminder: All globals that don't begin with _ are imported!
try:
    if not hasattr(sys, 'argv'):
        sys.argv = ['gdb']
        import IPython as ip
    _ipshell = ip.Shell.IPShellEmbed(argv=[''],banner="Hello!",exit_msg="Goodbye!")
except ImportError:
    def noop():
        pass
    _ipshell = noop

def resolve(obj):
    if obj is None:
        return None
    return gdb.default_visualizer(obj)

class _Basic(object):
    def __init__(self, typename, val):
        self.typename = typename
        self.val      = val
    def to_string(self):
        return self.__class__.__name__

class CRTP(_Basic):
    PATTERN = r'^vw::math::(\w+)Base<.*>$'
    # If we cast to the impl(), it still contains the reference to the full
    # crtp class. That means the default printer will cause infinite recursion.
    # This checks to make sure a prettyprinter is defined for a class before
    # trying to cast to impl.
    @staticmethod
    def can_use(val):
        new = val.cast(val.type.template_argument(0))
        get = resolve(new)
        return get is not None

    def to_string(self):
        return self.val.cast(self.val.type.template_argument(0))

class VectorBinaryFunc(_Basic):
    PATTERN = r'vw::math::VectorBinaryFunc<.*>'
    def display_hint(self):
        return 'map'
    def children(self):
        return iter([
            ('', 'lhs'),  ('', self.val['v1']),
            ('', 'rhs'),  ('', self.val['v2']),
            ('', 'func'), ('', self.val['func']),
        ])

class VectorUnaryFunc(_Basic):
    PATTERN = r'vw::math::VectorUnaryFunc<.*>'
    def display_hint(self):
        return 'map'
    def children(self):
        return iter([
            ('', 'v'),    ('', self.val['v']),
            ('', 'func'), ('', self.val['func']),
        ])

class _Vector(_Basic):
    def __init__(self, *args, **kw):
        super(_Vector, self).__init__(*args, **kw)
        m = re.compile(self.PATTERN).search(self.typename)
        assert m
        self.size = int(m.group(2))
    def display_hint(self):
        return 'array'
    def children(self):
        return [('',self.get_ptr()[i]) for i in range(self.get_size())]
    def to_string(self):
        size = self.get_size()
        name = self.__class__.__name__.replace('Dynamic', 'X').replace('Static', str(size))
        if size == 0:
            return '(empty) ' + name
        return name

class _Matrix(_Vector):
    def __init__(self, *args, **kw):
        super(_Matrix, self).__init__(*args, **kw)
        m = re.compile(self.PATTERN).search(self.typename)
        assert m
        self.rows = int(m.group(2))
        self.cols = int(m.group(3))

class VectorProxyStatic(_Vector):
    PATTERN = r'^vw::math::VectorProxy<(\w+), ([1-9]+\d*)>$'
    def get_size(self):
        return self.size
    def get_ptr(self):
        return self.val['m_ptr']

class VectorProxyDynamic(_Vector):
    PATTERN = '^vw::math::VectorProxy<(\w+), (0)>$'
    def get_size(self):
        return self.val['m_size']
    def get_ptr(self):
        return self.val['m_ptr']

class VectorStatic(_Vector):
    PATTERN = r'^vw::math::Vector<(\w+), ([1-9]+\d*)>$'
    def get_size(self):
        return self.size
    def get_ptr(self):
        return self.val['core_']['elems']

class VectorDynamic(_Vector):
    PATTERN = '^vw::math::Vector<(\w+), (0)>$'
    def get_size(self):
        return self.val['core_']['m_size']
    def get_ptr(self):
        return self.val['core_']['m_data']['px']

class MatrixProxyStatic(_Matrix):
    PATTERN = r'^vw::math::MatrixProxy<(\w+), ([1-9]+\d*), ([1-9]+\d*)>$'
    def get_size(self):
        return self.rows * self.cols
    def get_ptr(self):
        return self.val['m_ptr']

class MatrixProxyDynamic(_Matrix):
    PATTERN = '^vw::math::MatrixProxy<(\w+), (0), (0)>$'
    def get_size(self):
        return self.val['m_rows'] * self.val['m_cols']
    def get_ptr(self):
        return self.val['m_ptr']

class MatrixStatic(_Matrix):
    PATTERN = r'^vw::math::Matrix<(\w+), ([1-9]+\d*), ([1-9]+\d*)>$'
    def get_size(self):
        return self.rows * self.cols
    def get_ptr(self):
        return self.val['core_']['elems']

class MatrixDynamic(_Matrix):
    PATTERN = '^vw::math::Matrix<(\w+), (0), (0)>$'
    def get_size(self):
        return self.val['m_rows'] * self.val['m_cols']
    def get_ptr(self):
        return self.val['core_']['m_data']['px']


class BBox(_Basic):
    PATTERN = '^vw::math::BBox<(.+)>$'
    def display_hint(self):
        return 'map'
    def children(self):
        min = resolve(self.val['m_min'])
        max = resolve(self.val['m_max'])
        def calc(idx):
            try:
                return max.get_ptr()[idx] - min.get_ptr()[idx]
            except RuntimeError, e:
                return 'Failed to calc: %s' % e

        return iter([
            ('', 'min'),    ('', min.val),
            ('', 'max'),    ('', max.val),
            ('', 'width'),  ('', calc(0)),
            ('', 'height'), ('', calc(1)),
        ])
