#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + ISN_.has_value();
  if ( message.RST ) {
    reassembler_.reader().set_error();
  } else if ( checkpoint > 0 && checkpoint <= UINT32_MAX && message.seqno == ISN_ )
    return; // 拦截非法的序列号
  if ( !ISN_.has_value() ) {
    if ( !message.SYN )
      return;
    ISN_ = message.seqno;
  }
  const uint64_t abso_seqno_ = message.seqno.unwrap( *ISN_, checkpoint );
  reassembler_.insert( abso_seqno_ == 0 ? abso_seqno_ : abso_seqno_ - 1, move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + ISN_.has_value();
  const uint64_t capacity = reassembler_.writer().available_capacity();
  const uint16_t wnd_size = capacity > UINT16_MAX ? UINT16_MAX : capacity;
  if ( !ISN_.has_value() )
    return { {}, wnd_size, reassembler_.writer().has_error() };
  return { Wrap32::wrap( checkpoint + reassembler_.writer().is_closed(), *ISN_ ),
           wnd_size,
           reassembler_.writer().has_error() };
}
