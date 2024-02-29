#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const uint32_t target_ip = next_hop.ipv4_numeric();
  auto iter = mapping_table_.find( target_ip );
  if ( iter == mapping_table_.end() ) {
    dgrams_waiting_addr_.emplace( target_ip, dgram );
    // 判断五秒内是否发送过相同的地址解析请求
    if ( arp_recorder_.find( target_ip ) == arp_recorder_.end() ) {
      // 仅在没有记录的情况下发送一个地址解析请求
      transmit( make_frame( EthernetHeader::TYPE_ARP,
                            serialize( make_arp_message( ARPMessage::OPCODE_REQUEST, target_ip ) ) ) );
      arp_recorder_.emplace( target_ip, 0 );
    }
  } else
    transmit( make_frame( EthernetHeader::TYPE_IPv4, serialize( dgram ), iter->second.get_ether() ) );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ )
    return; // 丢弃目的地址既不是广播地址也不是本接口的数据帧

  switch ( frame.header.type ) {
    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram ip_dgram;
      if ( !parse( ip_dgram, frame.payload ) )
        return;
      // 解析后推入 datagrams_received_
      datagrams_received_.emplace( move( ip_dgram ) );
    } break;

    case EthernetHeader::TYPE_ARP: {
      ARPMessage arp_msg;
      if ( !parse( arp_msg, frame.payload ) )
        return;
      mapping_table_.insert_or_assign( arp_msg.sender_ip_address,
                                       address_mapping( arp_msg.sender_ethernet_address ) );
      switch ( arp_msg.opcode ) {
        case ARPMessage::OPCODE_REQUEST: {
          // 和当前接口的 IP 地址一致，发送 ARP 响应
          if ( arp_msg.target_ip_address == ip_address_.ipv4_numeric() )
            transmit( make_frame( EthernetHeader::TYPE_ARP,
                                  serialize( make_arp_message( ARPMessage::OPCODE_REPLY,
                                                               arp_msg.sender_ip_address,
                                                               arp_msg.sender_ethernet_address ) ),
                                  arp_msg.sender_ethernet_address ) );
        } break;

        case ARPMessage::OPCODE_REPLY: {
          // 遍历队列发出旧数据帧
          auto [head, tail] = dgrams_waiting_addr_.equal_range( arp_msg.sender_ip_address );
          for ( auto iter = head; iter != tail; ++iter )
            transmit(
              make_frame( EthernetHeader::TYPE_IPv4, serialize( iter->second ), arp_msg.sender_ethernet_address ) );
          if ( head != tail )
            dgrams_waiting_addr_.erase( head, tail );
        } break;

        default:
          break;
      }
    } break;

    default:
      break;
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  constexpr size_t ms_mappings_ttl = 30'000, ms_resend_arp = 5'000;
  // 刷新数据表，删掉超时项
  auto flush_timer = [&ms_since_last_tick]( auto& datasheet, const size_t deadline ) -> void {
    for ( auto iter = datasheet.begin(); iter != datasheet.end(); ) {
      if ( ( iter->second += ms_since_last_tick ) > deadline )
        iter = datasheet.erase( iter );
      else
        ++iter;
    }
  };
  flush_timer( mapping_table_, ms_mappings_ttl );
  flush_timer( arp_recorder_, ms_resend_arp );
}

ARPMessage NetworkInterface::make_arp_message( const uint16_t option,
                                               const uint32_t target_ip,
                                               optional<EthernetAddress> target_ether ) const
{
  ARPMessage msg;
  msg.sender_ethernet_address = ethernet_address_;
  msg.sender_ip_address = ip_address_.ipv4_numeric();
  msg.target_ip_address = target_ip;
  if ( target_ether.has_value() )
    msg.target_ethernet_address = move( *target_ether );
  msg.opcode = option;

  return msg;
}

EthernetFrame NetworkInterface::make_frame( const uint16_t protocol,
                                            std::vector<std::string> payload,
                                            optional<EthernetAddress> dst ) const
{
  EthernetFrame frame;
  if ( dst.has_value() )
    frame.header.dst = move( *dst );
  else
    frame.header.dst = ETHERNET_BROADCAST;
  frame.header.src = ethernet_address_;
  frame.header.type = protocol;
  frame.payload = move( payload );

  return frame;
}

NetworkInterface::address_mapping& NetworkInterface::address_mapping::tick( const size_t ms_time_passed ) noexcept
{
  timer_ += ms_time_passed;
  return *this;
}
