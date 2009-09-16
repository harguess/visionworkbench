# __BEGIN_LICENSE__
# Copyright (C) 2006, 2007 United States Government as represented by
# the Administrator of the National Aeronautics and Space Administration.
# All Rights Reserved.
# __END_LICENSE__


########################################################################
# sources
########################################################################

if MAKE_MODULE_PLATE

include_HEADERS = Blob.h Index.h Tree.h PlateFile.h
libvwPlate_la_SOURCES = 
libvwPlate_la_LIBADD = @MODULE_PLATE_LIBS@
lib_LTLIBRARIES = libvwPlate.la

endif

if ENABLE_EXCEPTIONS

#enplate_SOURCES = enplate.cc
#enplate_LDADD    = @PKG_PLATE_LIBS@

image2plate_SOURCES = image2plate.cc
image2plate_LDADD    = @PKG_CARTOGRAPHY_LIBS@ @PKG_MOSAIC_LIBS@ @PKG_PLATE_LIBS@

plate2qtree_SOURCES = plate2qtree.cc
plate2qtree_LDADD    = @PKG_PLATE_LIBS@

bin_PROGRAMS = plate2qtree image2plate
noinst_PROGRAMS = 
endif

########################################################################
# general
########################################################################

AM_CPPFLAGS = @VW_CPPFLAGS@
AM_LDFLAGS = @VW_LDFLAGS@ -version-info @LIBTOOL_VERSION@

SUBDIRS = . tests

includedir = $(prefix)/include/vw/Plate

include $(top_srcdir)/config/rules.mak