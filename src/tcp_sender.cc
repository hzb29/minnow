#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <algorithm>

using namespace std;

RetransmissionTimer& RetransmissionTimer::active() noexcept
{
  is_active_ = true;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::timeout() noexcept
{
  RTO_ <<= 1;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::reset() noexcept
{
  time_passed_ = 0;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::tick( uint64_t ms_since_last_tick ) noexcept
{
  time_passed_ += is_active_ ? ms_since_last_tick : 0;
  return *this;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  Reader& bytes_reader = input_.reader();
  fin_flag_ |= bytes_reader.is_finished(); // 允许刚建立就关闭流
  if ( !sent_syn_ ) {                      // 什么都没发，先发个连接请求过去
    const auto& msg = outstanding_bytes_.emplace( make_message( next_seqno_, {}, true, fin_flag_ ) );
    num_bytes_in_flight_ += msg.sequence_length();
    next_seqno_ += msg.sequence_length();
    timer_.active();
    transmit( msg );
    sent_syn_ = true;
    return;
  } else if ( sent_fin_ )
    return; // 已经结束了，什么都不该再发

  const size_t window_size = wnd_size_ == 0 ? 1 : wnd_size_;
  // 不断组装并发送分组数据报，直到达到窗口上限或没数据读，并且在 FIN 发出后不再尝试组装报文
  for ( string payload {}; num_bytes_in_flight_ < window_size && !sent_fin_; payload.clear() ) {
    string_view bytes_view = bytes_reader.peek();
    // 流为空且不需要发出 FIN，直接跳过报文发送
    if ( bytes_view.empty() && !fin_flag_ )
      break;

    // 从流中读取数据并组装报文，直到达到报文长度限制或窗口上限
    while ( payload.size() + num_bytes_in_flight_ < window_size
            && payload.size() < TCPConfig::MAX_PAYLOAD_SIZE ) { // 负载上限
      if ( bytes_view.empty() || fin_flag_ )
        break; // 没数据读了，或者流关闭了

      // 如果当前读取的字节分组长度超过限制
      if ( const uint64_t available_size = min( TCPConfig::MAX_PAYLOAD_SIZE - payload.size(),
                                                window_size - ( payload.size() + num_bytes_in_flight_ ) );
           bytes_view.size() > available_size ) // 那么这个分组需要被截断
        bytes_view.remove_suffix( bytes_view.size() - available_size );

      payload.append( bytes_view );
      bytes_reader.pop( bytes_view.size() );
      // 从流中弹出字符后要检查流是否关闭
      fin_flag_ |= bytes_reader.is_finished();
      bytes_view = bytes_reader.peek();
    }

    // 原地组装报文后立刻推入缓冲区
    auto& msg = outstanding_bytes_.emplace( make_message( next_seqno_, move( payload ), syn_flag_, fin_flag_ ) );

    // 检查 FIN 字节能否在此次报文传送中发送出去
    if ( fin_flag_ && msg.sequence_length() - syn_flag_ > window_size )
      msg.FIN = false;    // 如果窗口大小不足以容纳 FIN，则不发送
    else if ( fin_flag_ ) // 否则发送
      sent_fin_ = true;

    num_bytes_in_flight_ += msg.sequence_length() - syn_flag_;
    next_seqno_ += msg.sequence_length() - syn_flag_;
    transmit( msg );
    if (msg.sequence_length() != 0)
      timer_.active();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return make_message( next_seqno_, {}, false );
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( !msg.ackno.has_value() ) {
    if ( msg.window_size == 0 )
      input_.set_error();
    return;
  }
  // 对方所期待的下一个字节序号
  const uint64_t excepting_seqno = msg.ackno->unwrap( isn_, next_seqno_ );
  if ( excepting_seqno > next_seqno_ ) // 收到了没发出去的字节的确认
    return;                            // 不接受这个确认报文
  wnd_size_ = msg.window_size;

  bool is_acknowledged = false; // 用于判断确认是否发生
  while ( !outstanding_bytes_.empty() ) {
    auto& buffered_msg = outstanding_bytes_.front();
    // 对方期待的下一字节不大于队首的字节序号，或者队首分组只有部分字节被确认
    if ( const uint64_t final_seqno = acked_seqno_ + buffered_msg.sequence_length() - buffered_msg.SYN;
         excepting_seqno <= acked_seqno_ || excepting_seqno < final_seqno )
      break; // 这种情况下不会更改缓冲队列

    is_acknowledged = true; // 否则就表示有字节被确认
    // 如果队首的分组没有负载且没有收到 SYN 确认，说明这个分组一定是传输 SYN 的分组
    if ( buffered_msg.payload.empty() && !syn_flag_ ) { // 这个分组要特殊处理
      num_bytes_in_flight_ -= buffered_msg.sequence_length();
      acked_seqno_ += buffered_msg.sequence_length();
    } else {
      num_bytes_in_flight_ -= buffered_msg.sequence_length() - syn_flag_;
      acked_seqno_ += buffered_msg.sequence_length() - syn_flag_;
    } // 最后检查 syn 是否被确认
    syn_flag_ = sent_syn_ ? syn_flag_ : excepting_seqno <= next_seqno_;
    outstanding_bytes_.pop();
  }

  if ( is_acknowledged ) {
    // 如果全部分组都被确认，那就停止计时器
    if ( outstanding_bytes_.empty() )
      timer_ = RetransmissionTimer( initial_RTO_ms_ );
    else // 否则就只重启计时器
      timer_ = move( RetransmissionTimer( initial_RTO_ms_ ).active() );
    retransmission_cnt_ = 0; // 因为要重置 RTO 值，故直接更换新对象
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( timer_.tick( ms_since_last_tick ).is_expired() ) {
    transmit( outstanding_bytes_.front() ); // 只传递队首元素
    if ( wnd_size_ == 0 )
      timer_.reset();
    else
      timer_.timeout().reset();
    ++retransmission_cnt_;
  }
}

TCPSenderMessage TCPSender::make_message( uint64_t seqno, string payload, bool SYN, bool FIN ) const
{
  TCPSenderMessage ret;
  ret.SYN = SYN;
  ret.FIN = FIN;
  ret.RST = input_.reader().has_error();
  ret.seqno = Wrap32::wrap( seqno, isn_ );
  ret.payload = move(payload);
  return ret;
}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return num_bytes_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmission_cnt_;
}
