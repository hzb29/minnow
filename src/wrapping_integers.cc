#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  Wrap32 seqno( zero_point + static_cast<uint32_t>( n ) );
  (void)n;
  (void)zero_point;
  return seqno;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint64_t abs_seqno = static_cast<uint64_t>( this->raw_value_ - zero_point.raw_value_ );
  // 现在的abs_seqno是mod之后的值，还需要把它还原回mod之前的
  // 由于还原之后的abs_seqno需要离checkpoint最近，因此先找出checkpoint前后的两个可还原的abs_seqno，那个近选哪个就好
  // checkpoint / 2^32得到商，也即mod 2^32的次数
  uint64_t times_mod = checkpoint >> 32; // >>32等价于除以2^32
  // mod 2^32的余数,<<32实现截断前面32位，>>32实现保留低32位
  uint64_t remain = checkpoint << 32 >> 32; // 总体等效于%2^32，
  uint64_t bound;
  // 先取得离checkpoint最近的边界的mod次数（times_mod是左边界mod次数）
  if ( remain < 1UL << 31 ) // remain属于[0,2^32-1]，mid=2^31(即1UL << 31)
    bound = times_mod;
  else
    bound = times_mod + 1;
  // 以该边界的左右边界作为base，还原出2个mod之前的abs_seqno值
  // <<32等价于乘上2^32
  uint64_t abs_seqno_l = abs_seqno + ( ( bound == 0 ? 0 : bound - 1 ) << 32 ); // 注意bound=0的特殊情况
  uint64_t abs_seqno_r = abs_seqno + ( bound << 32 );
  // 判断checkpoint离哪个abs_seqno值近就取那个
  if ( checkpoint < ( abs_seqno_l + abs_seqno_r ) / 2 )
    abs_seqno = abs_seqno_l;
  else
    abs_seqno = abs_seqno_r;
  (void)zero_point;
  (void)checkpoint;
  return abs_seqno;
  return {};
}
