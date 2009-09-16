#ifndef __VW_PLATE_PLATEFILE_H__
#define __VW_PLATE_PLATEFILE_H__

#include <vw/Math/Vector.h>
#include <vw/Image.h>
#include <vw/Mosaic/ImageComposite.h>

#include <vw/Plate/Index.h>
#include <vw/Plate/Blob.h>

#include <vector>
#include <fstream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>

namespace fs = boost::filesystem;

namespace vw {
namespace platefile {

  // -------------------------------------------------------------------------
  //                            TEMPORARY TILE FILE
  // -------------------------------------------------------------------------

  // A scoped temporary file object that store a tile under /tmp.  The
  // file is destroyed when this object is deleted.
  class TemporaryTileFile {

    std::string m_filename;

    // Define these as private methods to enforce TemporaryTileFile'
    // non-copyable semantics.
    TemporaryTileFile() {}
    TemporaryTileFile(TemporaryTileFile const&) {}
    TemporaryTileFile& operator=(TemporaryTileFile const&) { return *this; }
  
  public:

    static std::string unique_tempfile_name(std::string file_extension) {
      char base_name[100] = "/tmp/vw_plate_tile_XXXXXXX";
      std::string name = mktemp(base_name);
      return name + "." + file_extension;
    }

    /// This constructor assumes control over an existing file on disk,
    /// and deletes it when the TemporaryTileFile object is de-allocated.
    TemporaryTileFile(std::string filename) : m_filename(filename) {
      vw_out(DebugMessage, "plate::tempfile") << "Assumed control of temporary file: " 
                                              << m_filename << "\n";

    }

    /// This constructor assumes control over an existing file on disk,
    /// and deletes it when the TemporaryTileFile object is de-allocated.
    template <class ViewT>
    TemporaryTileFile(ImageViewBase<ViewT> const& view, std::string file_extension) : 
      m_filename(unique_tempfile_name(file_extension)) {
      write_image(m_filename, view);
      vw_out(DebugMessage, "plate::tempfile") << "Created temporary file: " 
                                              << m_filename << "\n";
    }

    ~TemporaryTileFile() {
      int result = unlink(m_filename.c_str());
      if (result)
        vw_out(ErrorMessage, "plate::tempfile") 
          << "WARNING: unlink() failed in ~TemporaryTileFile() for filename \"" 
          << m_filename << "\"\n";
      vw_out(DebugMessage, "plate::tempfile") << "Destroyed temporary file: " 
                                              << m_filename << "\n";
    }

    std::string file_name() const { return m_filename; }

    /// Opens the temporary file and determines its size in bytes.
    int64 file_size() const {
      std::ifstream istr(m_filename.c_str(), std::ios::binary);
      
      if (!istr.is_open())
        vw_throw(IOErr() << "TempPlateFile::file_size() -- could not open \"" 
                 << m_filename << "\".");
      istr.seekg(0, std::ios_base::end);
      return istr.tellg();
    }

    template <class PixelT> 
    ImageView<PixelT> read() const {
      ImageView<PixelT> img;
      read_image(img, m_filename);
      return img;
    }
          
  };


  // -------------------------------------------------------------------------
  //                            PLATE FILE
  // -------------------------------------------------------------------------

  
  class PlateFile {
    std::string m_plate_name;
    int m_default_block_size;;
    std::string m_default_file_type;
    boost::shared_ptr<Index> m_index;
    FifoWorkQueue m_queue;

    // -------------------------------------------------------------------------
    //                       PLATE FILE TASKS
    // -------------------------------------------------------------------------
    
    template <class ViewT>
    class WritePlateFileTask : public Task {
      PlateFile *m_parent;
      ViewT const& m_view;
      int m_i, m_j, m_depth;
      BBox2i m_bbox; 
      
    public:
      WritePlateFileTask(PlateFile *parent, int i, int j, int depth, 
                         ImageViewBase<ViewT> const& view, BBox2i bbox) : 
        m_parent(parent), m_i(i), m_j(j), m_depth(depth), m_view(view.impl()), m_bbox(bbox) {}
      
      virtual ~WritePlateFileTask() {}
      virtual void operator() () { 
        std::cout << "\t    Generating block: [ " << m_j << " " << m_i 
                  << " @ level " <<  m_depth << "] " << m_bbox << "\n";

        m_parent->write(m_i, m_j, m_depth, crop(m_view, m_bbox)); }
    };

  public:
  
    PlateFile(std::string filename, int default_block_size = 256, 
              std::string default_file_type = "jpg") : 
      m_plate_name(filename), m_default_block_size(default_block_size),
      m_default_file_type(default_file_type) {

      // Plate files are stored as an index file and one or more data
      // blob files in a directory.  We create that directory here if
      // it doesn't already exist.
      if( !exists( fs::path( filename, fs::native ) ) ) {
        fs::create_directory(filename);
        m_index = boost::shared_ptr<Index>( new Index(default_block_size, default_file_type) );
        vw_out(DebugMessage, "platefile") << "Creating new plate file: \"" 
                                          << filename << "\"\n";

      // However, if it does exist, then we attempt to open the
      // platefile that is stored there.
      } else {
        try {
          m_index = boost::shared_ptr<Index>( new Index(filename + "/plate.index") );
          vw_out(DebugMessage, "platefile") << "Re-opened plate file: \"" 
                                            << filename << "\"\n";
          m_default_block_size = m_index->default_block_size();
          m_default_file_type = m_index->default_file_type();
        } catch (IOErr &e) {
          m_index = boost::shared_ptr<Index>( new Index(default_block_size, default_file_type) );
          vw_out(DebugMessage, "platefile") << "WARNING: Failed to re-open the plate file.  "
                                            << "Creating a new plate file from scratch.";
        }
      }

    }

    /// The destructor saves the platefile to disk. 
    ~PlateFile() { this->save(); }

    /// Force the Platefile to save its index to disk.
    void save() { m_index->save(m_plate_name + "/plate.index"); }

    /// Returns the name of the root directory containing the plate file.
    std::string name() const { return m_plate_name; }

    /// Returns the file type used to store tiles in this plate file.
    std::string default_file_type() const { return m_default_file_type; }

    // int tile_size() const {}// return m_index.tile_size(); }

    int depth() const { return m_index->max_depth(); }

    // Vector<int,2>  size_in_tiles() const  {}// return m_index.size(); }

    // Vector<long,2> size_in_pixels() const {}// return size_in_tiles() * tile_size(); }

    /// Read an image from the specified block location in the plate file.
    template <class PixelT>
    IndexRecord read(ImageView<PixelT> &view, int col, int row, int depth) {

      // 1. Call index read_request(col,row,depth).  Returns IndexRecord.
      IndexRecord record = m_index->read_request(col, row, depth);
      if (record.valid()) {
        std::ostringstream blob_filename;
        blob_filename << m_plate_name << "/plate_" << record.blob_id() << ".blob";

        // 2. Choose a temporary filename
        std::string tempfile = TemporaryTileFile::unique_tempfile_name(record.block_filetype());

        // 3. Call BlobIO read_as_file(filename, offset, size) [ offset, size from IndexRecord ]
        Blob blob(blob_filename.str());
        blob.read_to_file(tempfile, record.blob_offset(), record.block_size());
        TemporaryTileFile tile(tempfile);

        // 4. Read data from temporary file.
        view = tile.read<PixelT>();
      }
      return record;
    }

    /// Write an image to the specified block location in the plate file.
    template <class ViewT>
    void write(int col, int row, int depth, ImageViewBase<ViewT> const& view) {      

      // 1. Write data to temporary file. 
      TemporaryTileFile tile(view, m_default_file_type);
      std::string tile_filename = tile.file_name();
      int64 tile_size = tile.file_size();

      // 2. Make write_request(size) to index. Returns blob id.
      int blob_id = m_index->write_request(tile_size);
      std::ostringstream blob_filename;
      blob_filename << m_plate_name << "/plate_" << blob_id << ".blob";

      // 3. Create a blob and call write_from_file(filename).  Returns offset, size.
      Blob blob(blob_filename.str());
      int64 blob_offset;
      int32 block_size;
      blob.write_from_file(tile_filename, blob_offset, block_size);

      // 4. Call write_complete(col, row, depth, record)
      IndexRecord write_record(blob_id, blob_offset, block_size, m_default_file_type);
      m_index->write_complete(col, row, depth, write_record);
    }

    /// Add an image to the plate file.
    template <class ViewT>
    void insert(ImageViewBase<ViewT> const& image) {

      // chop up the image into small chunks
      std::vector<BBox2i> bboxes = image_blocks( image.impl(), 
                                                 m_default_block_size, 
                                                 m_default_block_size);
      
      // Compute the dimensions (in blocks) of the image so that we can
      // reshape the vector of bboxes into a "matrix" of bboxes in the
      // code below.
      int block_cols = ceilf(float(image.impl().cols()) / m_default_block_size);
      int block_rows = ceilf(float(image.impl().rows()) / m_default_block_size);
      int nlevels = ceilf(log(std::max(block_rows, block_cols))/log(2));
      std::cout << "\t--> Rows: " << block_rows << " Cols: " << block_cols 
                << "   (Block size: " << m_default_block_size << ")\n";
      std::cout << "\t--> Number of mipmap levels = " << nlevels << "\n";
      
      // And save each block to the PlateFile
      std::vector<BBox2i>::iterator block_iter = bboxes.begin();
      for (int j = 0; j < block_rows; ++j) {
        for (int i = 0; i < block_cols; ++i) {
          m_queue.add_task(boost::shared_ptr<Task>(new WritePlateFileTask<ViewT>(this, 
                                                                                 i, j, nlevels,
                                                                                 image, 
                                                                                 *block_iter)));
          //          this->write(i, j, nlevels, crop(image, *block_iter));
          
          // For debugging
          //  this->print();
          
          ++block_iter;
        }
        std::cout << "\n";
      }      
      m_queue.join_all();
    }


    void mipmap_helper(int col, int row, int depth) {

      // Termination conditions: 
      try {
        IndexRecord record = m_index->read_request(col, row, depth);
        // (1) the record is already valid
        if (record.valid())
          return;
      } catch (TileNotFoundErr &e) {
        // (2) the record does not exist at all.
        return;
      }

      // If none of the termination conditions are met, then we must
      // be at an invalid record that needs to be regenerated.
      std::vector<ImageView<PixelRGBA<uint8> > > imgs(4);
      for (unsigned i = 0; i < 4; ++i) {
        IndexRecord record;
        try {
          record = this->read(imgs[i], col*2+(i%2), row*2+(i/2), depth+1);
        } catch (TileNotFoundErr &e) { 
          // Do nothing... record is not found, and therefore invalid. 
        }
         
        // If the record for the child is invalid, we attempt to
        // (recursively) generate it.  Then we try reading it again.
        if (!record.valid()) {
          mipmap_helper(col*2+(i%2), row*2+(i/2), depth+1);
          try {
            record = this->read(imgs[i], col*2+(i%2), row*2+(i/2), depth+1);
          } catch (TileNotFoundErr &e) {} // Do nothing... record is not found, and therefore invalid. 
        }
      }

      std::cout << "\t    Generating " << col << " " << row << " @ " << depth << "\n";

      
      // Piece together the tiles from the four children if this node.
      mosaic::ImageComposite<PixelRGBA<uint8> > composite;
      composite.set_draft_mode(true);
      composite.insert(imgs[0], 0, 0);
      composite.insert(imgs[1], m_default_block_size, 0);
      composite.insert(imgs[2], 0, m_default_block_size);
      composite.insert(imgs[3], m_default_block_size, m_default_block_size);
      composite.prepare();
      
      // Subsample the image, and then write the new tile.
      ImageView<PixelRGBA<uint8> > new_tile = subsample(gaussian_filter(composite, 1.0), 2);
      this->write(col, row, depth, new_tile);
    }

    void mipmap() { 
      std::cout << "\t--> Building mipmap levels\n";
      this->mipmap_helper(0,0,0); 
    }

    int depth() { 
      return m_index->max_depth();
    }

    void print() {
      m_index->print();
    }

  };


}} // namespace vw::plate

#endif // __VW_PLATE_PLATEFILE_H__