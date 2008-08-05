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

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4996)
#endif

#include <stdlib.h>
#include <iostream>
#include <fstream>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <vw/Core/Cache.h>
#include <vw/Core/ProgressCallback.h>
#include <vw/Math/Matrix.h>
#include <vw/Image/Transform.h>
#include <vw/Image/Palette.h>
#include <vw/FileIO/DiskImageResource.h>
#include <vw/FileIO/DiskImageResourceJPEG.h>
#include <vw/FileIO/DiskImageResourceGDAL.h>
#include <vw/FileIO/DiskImageView.h>
#include <vw/Cartography/GeoReference.h>
#include <vw/Cartography/GeoTransform.h>
#include <vw/Cartography/FileIO.h>
#include <vw/Mosaic/ImageComposite.h>
#include <vw/Mosaic/KMLQuadTreeGenerator.h>
using namespace vw;
using namespace vw::math;
using namespace vw::cartography;
using namespace vw::mosaic;


int main( int argc, char *argv[] ) {

  std::vector<std::string> image_files;
  std::string output_file_name;
  std::string output_file_type;
  GeoReference output_georef;
  double north_lat=90.0, south_lat=-90.0;
  double east_lon=180.0, west_lon=-180.0;
  double proj_lat=0, proj_lon=0, proj_scale=1;
  unsigned utm_zone;
  int patch_size, patch_overlap;
  float jpeg_quality;
  unsigned cache_size;
  int max_lod_pixels;
  double nudge_x=0, nudge_y=0;
  std::string palette_file;
  float palette_scale=1.0, palette_offset=0.0;
  int draw_order_offset;
  float pixel_scale=1.0, pixel_offset=0.0;
  double lcc_parallel1, lcc_parallel2;
  int aspect_ratio=0;
  int total_resolution = 1024;

  po::options_description general_options("General Options");
  general_options.add_options()
    ("output-name,o", po::value<std::string>(&output_file_name)->default_value("output"), "Specify the base output filename")
    ("quiet,q", "Quiet output")
    ("verbose,v", "Verbose output")
    ("cache", po::value<unsigned>(&cache_size)->default_value(1024), "Cache size, in megabytes")
    ("help", "Display this help message");

  po::options_description projection_options("Projection Options");
  projection_options.add_options()
    ("north", po::value<double>(&north_lat), "The northernmost latitude in degrees")
    ("south", po::value<double>(&south_lat), "The southernmost latitude in degrees")
    ("east", po::value<double>(&east_lon), "The easternmost longitude in degrees")
    ("west", po::value<double>(&west_lon), "The westernmost longitude in degrees")
    ("force-wgs84", "Assume the input images' geographic coordinate systems are WGS84, even if they're not (old behavior)")
    ("sinusoidal", "Assume a sinusoidal projection")
    ("mercator", "Assume a Mercator projection")
    ("transverse-mercator", "Assume a transverse Mercator projection")
    ("orthographic", "Assume an orthographic projection")
    ("stereographic", "Assume a stereographic projection")
    ("lambert-azimuthal", "Assume a Lambert azimuthal projection")
    ("lambert-conformal-conic", "Assume a Lambert Conformal Conic projection")
    ("utm", po::value<unsigned>(&utm_zone), "Assume UTM projection with the given zone")
    ("proj-lat", po::value<double>(&proj_lat), "The center of projection latitude (if applicable)")
    ("proj-lon", po::value<double>(&proj_lon), "The center of projection longitude (if applicable)")
    ("proj-scale", po::value<double>(&proj_scale), "The projection scale (if applicable)")
    ("std-parallel1", po::value<double>(&lcc_parallel1), "Standard parallels for Lambert Conformal Conic projection")
    ("std-parallel2", po::value<double>(&lcc_parallel2), "Standard parallels for Lambert Conformal Conic projection")
    ("nudge-x", po::value<double>(&nudge_x), "Nudge the image, in projected coordinates")
    ("nudge-y", po::value<double>(&nudge_y), "Nudge the image, in projected coordinates");
    
  po::options_description input_options("Input Options");
  input_options.add_options()
    ("pixel-scale", po::value<float>(&pixel_scale)->default_value(1.0), "Scale factor to apply to pixels")
    ("pixel-offset", po::value<float>(&pixel_offset)->default_value(0.0), "Offset to apply to pixels")
    ("normalize", "Normalize input images so that their full dynamic range falls in between [0,255].");

  po::options_description output_options("Output Options");
  output_options.add_options()
    ("file-type", po::value<std::string>(&output_file_type)->default_value("auto"), "Output file type")
    ("jpeg-quality", po::value<float>(&jpeg_quality)->default_value(0.75), "JPEG quality factor (0.0 to 1.0)")
    ("palette-file", po::value<std::string>(&palette_file), "Apply a palette from the given file")
    ("palette-scale", po::value<float>(&palette_scale), "Apply a scale factor before applying the palette")
    ("palette-offset", po::value<float>(&palette_offset), "Apply an offset before applying the palette")
    ("patch-size", po::value<int>(&patch_size)->default_value(256), "Patch size, in pixels")
    ("patch-overlap", po::value<int>(&patch_overlap)->default_value(0), "Patch overlap, in pixels (must be even)")
    ("patch-crop", "Crop output patches")
    ("max-lod-pixels", po::value<int>(&max_lod_pixels)->default_value(1024), "Max LoD in pixels, or -1 for none")
    ("draw-order-offset", po::value<int>(&draw_order_offset)->default_value(0), "Set an offset for the KML <drawOrder> tag for this overlay")
    ("composite-overlay", "Composite images using direct overlaying (default)")
    ("composite-multiband", "Composite images using multi-band blending")
    ("aspect-ratio", po::value<int>(&aspect_ratio)->default_value(1),"Pixel aspect ratio (for polar overlays; should be a power of two)");

  po::options_description hidden_options("");
  hidden_options.add_options()
    ("input-file", po::value<std::vector<std::string> >(&image_files));

  po::options_description options("Allowed Options");
  options.add(general_options).add(projection_options).add(input_options).add(output_options).add(hidden_options);

  po::positional_options_description p;
  p.add("input-file", -1);

  po::variables_map vm;
  po::store( po::command_line_parser( argc, argv ).options(options).positional(p).run(), vm );
  po::notify( vm );

  std::ostringstream command_line;
  for( int i=0; i<argc; ++i ) {
    command_line << argv[i];
    if( i < argc-1 ) command_line << " ";
  }

  std::ostringstream usage;
  usage << "Usage: image2kml [options] <filename>..." << std::endl << std::endl;
  usage << general_options << std::endl;
  usage << output_options << std::endl;
  usage << projection_options << std::endl;

  if( vm.count("help") ) {
    std::cout << usage.str();
    return 1;
  }

  if( vm.count("input-file") < 1 ) {
    std::cout << "Error: Must specify at least one input file!" << std::endl << std::endl;
    std::cout << usage.str();
    return 1;
  }

  if( patch_size <= 0 ) {
    std::cerr << "Error: The patch size must be a positive number!  (You specified " << patch_size << ".)" << std::endl << std::endl;
    std::cout << usage.str();
    return 1;
  }
    
  if( patch_overlap<0 || patch_overlap>=patch_size || patch_overlap%2==1 ) {
    std::cerr << "Error: The patch overlap must be an even nonnegative number" << std::endl;
    std::cerr << "smaller than the patch size!  (You specified " << patch_overlap << ".)" << std::endl << std::endl;
    std::cout << usage.str();
    return 1;
  }
  
  TerminalProgressCallback tpc;
  const ProgressCallback *progress = &tpc;
  if( vm.count("verbose") ) {
    set_debug_level(VerboseDebugMessage);
    progress = &ProgressCallback::dummy_instance();
  }
  else if( vm.count("quiet") ) {
    set_debug_level(WarningMessage);
  }

  DiskImageResourceJPEG::set_default_quality( jpeg_quality );
  Cache::system_cache().resize( cache_size*1024*1024 );

  // For image stretching
  float lo_value = ScalarTypeLimits<float>::highest();
  float hi_value = ScalarTypeLimits<float>::lowest();

  // Read in georeference info and compute total resolution
  bool manual = vm.count("north") || vm.count("south") || vm.count("east") || vm.count("west");
  std::vector<GeoReference> georeferences;
  for( unsigned i=0; i<image_files.size(); ++i ) {
    std::cout << "Adding file " << image_files[i] << std::endl;
    DiskImageResourceGDAL file_resource( image_files[i] );
    if( vm.count("normalize") ) {
      float no_data_value = file_resource.get_no_data_value(0);
      DiskImageView<PixelRGBA<float> > min_max_file(image_files[i]);
      float new_lo, new_hi;
      min_max_channel_values(min_max_file, new_lo, new_hi, no_data_value);
      lo_value = std::min(new_lo,lo_value);
      hi_value = std::max(new_hi,hi_value);
      std::cout << "Pixel range for \"" << image_files[i] << "\": [" << new_lo << " " << new_hi << "]    Output dynamic range: [" << lo_value << " " << hi_value << "]\n";
    }
    GeoReference input_georef;
    read_georeference( input_georef, file_resource );

    if(vm.count("force-wgs84"))
      input_georef.set_well_known_geogcs("WGS84");
    if ( input_georef.proj4_str() == "" ) input_georef.set_well_known_geogcs("WGS84");
    if( manual || input_georef.transform() == identity_matrix<3>() ) {
      if( image_files.size() == 1 ) {
        vw_out(InfoMessage) << "No georeferencing info found.  Assuming Plate Carree WGS84: " 
                            << east_lon << " to " << west_lon << " E, " << south_lat << " to " << north_lat << " N." << std::endl;
        input_georef = GeoReference();
        input_georef.set_well_known_geogcs("WGS84");
        Matrix3x3 m;
        m(0,0) = (east_lon - west_lon) / file_resource.cols();
        m(0,2) = west_lon;
        m(1,1) = (south_lat - north_lat) / file_resource.rows();
        m(1,2) = north_lat;
        m(2,2) = 1;
        input_georef.set_transform( m );
        manual = true;
      }
      else {
        vw_out(ErrorMessage) << "Error: No georeferencing info found for input file \"" << image_files[i] << "\"!" << std::endl;
        vw_out(ErrorMessage) << "(Manually-specified bounds are only allowed for single image files.)" << std::endl;
        exit(1);
      }
    }
    else if( vm.count("sinusoidal") ) input_georef.set_sinusoidal(proj_lon);
    else if( vm.count("mercator") ) input_georef.set_mercator(proj_lat,proj_lon,proj_scale);
    else if( vm.count("transverse-mercator") ) input_georef.set_transverse_mercator(proj_lat,proj_lon,proj_scale);
    else if( vm.count("orthographic") ) input_georef.set_orthographic(proj_lat,proj_lon);
    else if( vm.count("stereographic") ) input_georef.set_stereographic(proj_lat,proj_lon,proj_scale);
    else if( vm.count("lambert-azimuthal") ) input_georef.set_lambert_azimuthal(proj_lat,proj_lon);
    else if( vm.count("lambert-conformal-conic") ) input_georef.set_lambert_azimuthal(lcc_parallel1, lcc_parallel2, proj_lat, proj_lon);
    else if( vm.count("utm") ) input_georef.set_UTM( utm_zone );

    if( vm.count("nudge-x") || vm.count("nudge-y") ) {
      Matrix3x3 m = input_georef.transform();
      m(0,2) += nudge_x;
      m(1,2) += nudge_y;
      input_georef.set_transform( m );
    }
    
    georeferences.push_back( input_georef );

    // Just need a WGS84 georeference for computing resolution.
    output_georef.set_well_known_geogcs("WGS84");
    GeoTransform geotx( input_georef, output_georef );
    Vector2 center_pixel( file_resource.cols()/2, file_resource.rows()/2 );
    // Calculate the best resolution at 5 different points in the image,
    // as occasionally there's a singularity at the center pixel that 
    // makes it extremely tiny (such as in pole-centered images).
    int cols = file_resource.cols();
    int rows = file_resource.rows();
    Vector2 res_pixel[5];
    res_pixel[0] = Vector2( cols/2, rows/2 );
    res_pixel[1] = Vector2( cols/2 + cols/4, rows/2 );
    res_pixel[2] = Vector2( cols/2 - cols/4, rows/2 );
    res_pixel[3] = Vector2( cols, rows/2 + rows/4 );
    res_pixel[4] = Vector2( cols, rows/2 - rows/4 );
    int resolution[5];
    for(int i=0; i < 5; i++) {
        resolution[i] = output::kml::compute_resolution( geotx, res_pixel[i] );
        if( resolution[i] > total_resolution) total_resolution = resolution[i];
    }
  }
  // Now that we know the best resolution, we can get our output_georef.
  int xresolution = total_resolution / aspect_ratio, yresolution = total_resolution;
  output_georef = output::kml::get_output_georeference(xresolution,yresolution);

  // Configure the composite
  ImageComposite<PixelRGBA<uint8> > composite;

  // Add the transformed input files to the composite
  for( unsigned i=0; i<image_files.size(); ++i ) {
    GeoTransform geotx( georeferences[i], output_georef );
    ImageViewRef<PixelRGBA<uint8> > source = DiskImageView<PixelRGBA<uint8> >( image_files[i] );
    if( pixel_scale != 1.0 || pixel_offset != 0.0 ) 
      source = channel_cast_rescale<uint8>( DiskImageView<PixelRGBA<float> >( image_files[i] ) * pixel_scale + pixel_offset );

    if( vm.count("normalize") )
      source = channel_cast_rescale<uint8>( normalize_retain_alpha(DiskImageView<PixelRGBA<float> >( image_files[i] ), lo_value, hi_value, 0.0, 1.0) );

    
    if( vm.count("palette-file") ) {
      DiskImageView<float> disk_image( image_files[i] );
      if( vm.count("palette-scale") || vm.count("palette-offset") ) {
        source = per_pixel_filter( disk_image*palette_scale+palette_offset, PaletteFilter<PixelRGBA<uint8> >(palette_file) );
      }
      else {
        source = per_pixel_filter( disk_image, PaletteFilter<PixelRGBA<uint8> >(palette_file) );
      }
    }

    BBox2i bbox = geotx.forward_bbox( BBox2i(0,0,source.cols(),source.rows()) );
    
    // Constant edge extension is better for transformations that 
    // preserve the rectangularity of the image.  At the moment we 
    // only do this for manual transforms, alas.
    if( manual ) {
      // If the image is being super-sampled the computed bounding 
      // box may be missing a pixel at the edges relative to what 
      // you might expect, which can create visible artifacts if 
      // it happens at the boundaries of the coordinate system.
      if( west_lon == -180 ) bbox.min().x() = 0;
      if( east_lon == 180 ) bbox.max().x() = xresolution;
      if( north_lat == 90 ) bbox.min().y() = yresolution/4;
      if( south_lat == -90 ) bbox.max().y() = 3*yresolution/4;
      source = crop( transform( source, geotx, ConstantEdgeExtension() ), bbox );
    }
    else {
      source = crop( transform( source, geotx ), bbox );
    }
    composite.insert( source, bbox.min().x(), bbox.min().y() );
    // Images that wrap the date line must be added to the composite on both sides.
    if( bbox.max().x() > xresolution ) {
      composite.insert( source, bbox.min().x()-xresolution, bbox.min().y() );
    }
  }

  // Compute a tighter Google Earth coordinate system aligned bounding box
  BBox2i bbox = composite.bbox();
  bbox.crop( BBox2i(0,0,xresolution,yresolution) );
  int dim = 2 << (int)(log( (std::max)(bbox.width(),bbox.height()) )/log(2));
  if ( dim > total_resolution ) dim = total_resolution;
  BBox2i total_bbox( (bbox.min().x()/dim)*dim, (bbox.min().y()/dim)*dim, dim, dim );
  if ( ! total_bbox.contains( bbox ) ) {
    if( total_bbox.max().x() == xresolution ) total_bbox.min().x() -= dim;
    else total_bbox.max().x() += dim;
    if( total_bbox.max().y() == yresolution ) total_bbox.min().y() -= dim;
    else total_bbox.max().y() += dim;
  }

  // Prepare the composite
  if( vm.count("composite-multiband") ) {
    std::cout << "Preparing composite..." << std::endl;
    composite.prepare( total_bbox, *progress );
  }
  else {
    composite.set_draft_mode( true );
    composite.prepare( total_bbox );
  }
  BBox2i data_bbox = composite.bbox();
  data_bbox.crop( BBox2i(0,0,total_bbox.width(),total_bbox.height()) );

  // Prepare the quadtree
  BBox2 ll_bbox( -180.0 + (360.0*total_bbox.min().x())/xresolution, 
                 180.0 - (360.0*total_bbox.max().y())/yresolution,
                 (360.0*total_bbox.width())/xresolution,
                 (360.0*total_bbox.height())/yresolution );
  KMLQuadTreeGenerator<PixelRGBA<uint8> > quadtree( output_file_name, composite, ll_bbox );
  quadtree.set_metadata( "<generatedBy><![CDATA[" + command_line.str() + "]]></generatedBy>" );
  quadtree.set_max_lod_pixels(max_lod_pixels);
  quadtree.set_crop_bbox( data_bbox );
  quadtree.set_draw_order_offset( draw_order_offset );
  if( vm.count("crop") ) quadtree.set_crop_images( true );
  quadtree.set_output_image_file_type( output_file_type );

  // Generate the composite
  vw_out(InfoMessage) << "Generating KML Overlay..." << std::endl;
  quadtree.generate( *progress );

  return 0;
}