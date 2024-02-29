#pragma once

#include "byte_stream.hh"
#include <set>

class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  explicit Reassembler( ByteStream&& output )
    : output_( std::move( output ) )
    , bytes_unassembled( 0 )
    , waiting_substring( {} )
    , flag_eof( false )
    , pos_eof( 0 )
  {}
  struct Node
  {
    std::string data;
    size_t index;
    Node( std::string s, size_t x ) : data( s ), index( x ) {}
    bool operator<( const struct Node b ) const { return this->index < b.index; }
  };
  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;

  // Access output stream reader
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  const Writer& writer() const { return output_.writer(); }

private:
  ByteStream output_;
  uint64_t bytes_unassembled;
  std::set<struct Node> waiting_substring;
  bool flag_eof;
  uint64_t pos_eof;

  void insert_waiting_substring( struct Node& node );
};