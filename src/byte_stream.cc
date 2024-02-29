#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  return closed_;
}

void Writer::push( string data )
{
  if (available_capacity() == 0 || data.empty() ) {
    return ;  
  }
  auto const n = min( available_capacity(), data.size());
  if (n < data.size() ) {
    data = data.substr(0, n);
  }
  data_queue_.push_back(std::move(data));
  view_queue_.emplace_back(data_queue_.back().c_str(), n);
  num_bytes_buffered_ += n;
  num_bytes_pushed_ += n;
  return;
}

void Writer::close()
{
  closed_ = true;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - num_bytes_buffered_;
}

uint64_t Writer::bytes_pushed() const
{
  return num_bytes_pushed_;
}

bool Reader::is_finished() const
{
  return closed_ && num_bytes_buffered_ == 0;
}

uint64_t Reader::bytes_popped() const
{
  return num_bytes_popped_;
}

string_view Reader::peek() const
{
  if (view_queue_.empty()) {
    return {};
  }
  return view_queue_.front();
}

void Reader::pop( uint64_t len )
{
  auto n = min(len, num_bytes_buffered_);
  while (n > 0)
  {
    auto sz = view_queue_.front().size();
    if(n < sz)
    {
      view_queue_.front().remove_prefix(n);
      num_bytes_buffered_ -= n;
      num_bytes_popped_ += n;
      return ;
    }
    view_queue_.pop_front();
    data_queue_.pop_front();
    n -= sz;
    num_bytes_buffered_ -= sz;
    num_bytes_popped_ += sz;
  }
}

uint64_t Reader::bytes_buffered() const
{
  return num_bytes_buffered_;
}
