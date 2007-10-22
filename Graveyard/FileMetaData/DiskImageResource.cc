// __BEGIN_LICENSE__
// 
// Copyright (C) 2006 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration
// (NASA).  All Rights Reserved.
// 
// Copyright 2006 Carnegie Mellon University. All rights reserved.
// 
// This software is distributed under the NASA Open Source Agreement
// (NOSA), version 1.3.  The NOSA has been approved by the Open Source
// Initiative.  See the file COPYING at the top of the distribution
// directory tree for the complete NOSA document.
// 
// THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY
// KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT
// LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO
// SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT
// THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT
// DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE SUBJECT SOFTWARE.
// 
// __END_LICENSE__

/// \file DiskImageResource.cc
/// 
/// An abstract base class referring to an image on disk.
/// 

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4996)
#endif

#include <vw/config.h>

#include <iostream>
#include <map>
#include <boost/algorithm/string.hpp>

// For RunOnce
#include <vw/Core/Thread.h>

#include <vw/FileIO/PropertyMultiMap.h>
#include <vw/FileIO/PropertySetManager.h>

#include <vw/FileIO/DiskImageResource.h>
#include <vw/FileIO/DiskImageResourcePDS.h>

#if defined(VW_HAVE_PKG_PNG) && VW_HAVE_PKG_PNG==1
#include <vw/FileIO/DiskImageResourcePNG.h>
#endif

#if defined(VW_HAVE_PKG_JPEG) && VW_HAVE_PKG_JPEG==1
#include <vw/FileIO/DiskImageResourceJPEG.h>
#endif

#if defined(VW_HAVE_PKG_TIFF) && VW_HAVE_PKG_TIFF==1
#include <vw/FileIO/DiskImageResourceTIFF.h>
#endif

#if defined(VW_HAVE_PKG_OPENEXR) && VW_HAVE_PKG_OPENEXR==1
#if ! ( defined(VW_NO_EXCEPTIONS) && VW_NO_EXCEPTIONS==1 )
#include <vw/FileIO/DiskImageResourceOpenEXR.h>
#endif
#endif

#if defined(VW_HAVE_PKG_GDAL) && VW_HAVE_PKG_GDAL==1
#include <vw/FileIO/DiskImageResourceGDAL.h>
#endif


namespace {
  typedef vw::PropertyMultiMap<std::string,vw::DiskImageResource::construct_open_func,std::string> OpenMapType;
  typedef vw::PropertyMultiMap<std::string,vw::DiskImageResource::construct_create_func,std::string> CreateMapType;
  OpenMapType *open_map = 0;
  CreateMapType *create_map = 0;
  vw::PropertySetManager<std::string,std::string> metadata_property_manager;
}

void vw::DiskImageResource::register_metadata_type( std::string const& disk_image_resource_type, std::string const& metadata_type ) {
  //std::cout << "DiskImageResource " << disk_image_resource_type << " supports metadata " << metadata_type << std::endl;
  metadata_property_manager.set_property( disk_image_resource_type, metadata_type );
}

bool vw::DiskImageResource::supports_metadata_type( std::string const& disk_image_resource_type, std::string const& metadata_type ) {
  return metadata_property_manager.property_is_set( disk_image_resource_type, metadata_type );
}

vw::DiskImageResource::MetadataProperties const* vw::DiskImageResource::metadata_properties( std::string const& disk_image_resource_type ) {
  return metadata_property_manager.property_set( disk_image_resource_type );
}

void vw::DiskImageResource::register_file_type( std::string const& extension,
                                                std::string const& disk_image_resource_type,
                                                vw::DiskImageResource::construct_open_func open_func,
                                                vw::DiskImageResource::construct_create_func create_func )
{
  if( ! open_map ) open_map = new OpenMapType();
  if( ! create_map ) create_map = new CreateMapType();
  //std::cout << "REGISTERING DiskImageResource " << disk_image_resource_type << " for extension " << extension << std::endl;
  vw::DiskImageResource::MetadataProperties const* prop = metadata_property_manager.property_set( disk_image_resource_type, true );
  open_map->insert( std::make_pair( extension, open_func ), prop );
  create_map->insert( std::make_pair( extension, create_func ), prop );
}

static std::string file_extension( std::string const& filename ) {
  std::string::size_type dot = filename.find_last_of('.');
  if (dot == std::string::npos)
    vw_throw( vw::IOErr() << "DiskImageResource: Cannot infer file format from filename with no file extension." );
  std::string extension = filename.substr( dot );
  boost::to_lower( extension );
  return extension;
}

static vw::RunOnce rdft_once = VW_RUNONCE_INIT;

static void register_default_file_types() {
  vw::DiskImageResource::register_file_type( ".img", vw::DiskImageResourcePDS::type_static(), &vw::DiskImageResourcePDS::construct_open, &vw::DiskImageResourcePDS::construct_create );
  vw::DiskImageResource::register_file_type( ".pds", vw::DiskImageResourcePDS::type_static(), &vw::DiskImageResourcePDS::construct_open, &vw::DiskImageResourcePDS::construct_create );
  vw::DiskImageResource::register_file_type( ".lbl", vw::DiskImageResourcePDS::type_static(), &vw::DiskImageResourcePDS::construct_open, &vw::DiskImageResourcePDS::construct_create );

#if defined(VW_HAVE_PKG_PNG) && VW_HAVE_PKG_PNG==1
  vw::DiskImageResource::register_file_type( ".png", vw::DiskImageResourcePNG::type_static(), &vw::DiskImageResourcePNG::construct_open, &vw::DiskImageResourcePNG::construct_create );
#endif
#if defined(VW_HAVE_PKG_GDAL) && VW_HAVE_PKG_GDAL==1
  vw::DiskImageResource::register_file_type( ".png", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
#endif

#if defined(VW_HAVE_PKG_JPEG) && VW_HAVE_PKG_JPEG==1
  vw::DiskImageResource::register_file_type( ".jpg", vw::DiskImageResourceJPEG::type_static(), &vw::DiskImageResourceJPEG::construct_open, &vw::DiskImageResourceJPEG::construct_create );
  vw::DiskImageResource::register_file_type( ".jpeg", vw::DiskImageResourceJPEG::type_static(), &vw::DiskImageResourceJPEG::construct_open, &vw::DiskImageResourceJPEG::construct_create );
#endif
#if defined(VW_HAVE_PKG_GDAL) && VW_HAVE_PKG_GDAL==1
  vw::DiskImageResource::register_file_type( ".jpg", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
  vw::DiskImageResource::register_file_type( ".jpeg", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
#endif

#if defined(VW_HAVE_PKG_GDAL) && VW_HAVE_PKG_GDAL==1
  vw::DiskImageResource::register_file_type(".jp2", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );

  vw::DiskImageResource::register_file_type(".j2k", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
#endif

#if defined(VW_HAVE_PKG_TIFF) && VW_HAVE_PKG_TIFF==1
  vw::DiskImageResource::register_file_type( ".tif", vw::DiskImageResourceTIFF::type_static(), &vw::DiskImageResourceTIFF::construct_open, &vw::DiskImageResourceTIFF::construct_create );
  vw::DiskImageResource::register_file_type( ".tiff", vw::DiskImageResourceTIFF::type_static(), &vw::DiskImageResourceTIFF::construct_open, &vw::DiskImageResourceTIFF::construct_create );
#endif
#if defined(VW_HAVE_PKG_GDAL) && VW_HAVE_PKG_GDAL==1
  vw::DiskImageResource::register_file_type( ".tif", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
  vw::DiskImageResource::register_file_type( ".tiff", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
#endif

#if defined(VW_HAVE_PKG_OPENEXR) && VW_HAVE_PKG_OPENEXR==1
  vw::DiskImageResource::register_file_type( ".exr", vw::DiskImageResourceOpenEXR::type_static(), &vw::DiskImageResourceOpenEXR::construct_open, &vw::DiskImageResourceOpenEXR::construct_create );
#endif

#if defined(VW_HAVE_PKG_GDAL) && VW_HAVE_PKG_GDAL==1
  vw::DiskImageResource::register_file_type( ".grd", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
  vw::DiskImageResource::register_file_type( ".dem", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
  vw::DiskImageResource::register_file_type( ".bil", vw::DiskImageResourceGDAL::type_static(), &vw::DiskImageResourceGDAL::construct_open, &vw::DiskImageResourceGDAL::construct_create );
#endif
}

vw::DiskImageResource* vw::DiskImageResource::open( std::string const& filename,
                                                    FileMetadataCollection const& fmeta /*= FileMetadataCollection::create()*/ ) {
  rdft_once.run( register_default_file_types );
  if( open_map ) {
    std::pair<std::string,construct_open_func> item;
    bool found_func;
    std::list<std::string> metadata_types;
    const FileMetadata* m;
    bool is_readable = true;
    FileMetadataCollection::FileMetadataCollectionIterator i;
    while( (m = fmeta.file_metadata_const( &is_readable, &i ) ) ) {
      if( is_readable )
        metadata_types.push_back( m->metadata_type() );
    }
    found_func = open_map->find( item, file_extension( filename ), &metadata_types, &OpenMapType::score_priority );
    if( found_func )
      return item.second( filename );
  }
  vw_throw( NoImplErr() << "Unsuppported file format: " << filename );
  return 0; // never reached
}

vw::DiskImageResource* vw::DiskImageResource::create( std::string const& filename, ImageFormat const& format, FileMetadataCollection const& fmeta /*= FileMetadataCollection::create()*/ ) {
  rdft_once.run( register_default_file_types );
  if( create_map ) {
    std::pair<std::string,construct_create_func> item;
    bool found_func;
    std::list<std::string> metadata_types;
    const FileMetadata* m;
    bool is_readable = true;
    FileMetadataCollection::FileMetadataCollectionIterator i;
    while( (m = fmeta.file_metadata_const( &is_readable, &i ) ) )
      metadata_types.push_back( m->metadata_type() );
    found_func = create_map->find( item, file_extension( filename ), &metadata_types, &CreateMapType::score_priority );
    if( found_func )
      return item.second( filename, format );
  }
  vw_throw( NoImplErr() << "Unsuppported file format: " << filename );
  return 0; // never reached
}