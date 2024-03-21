#pragma once

#include <map>
#include <memory>
#include <optional>
#include <compare>

#include "exception.hh"
#include "network_interface.hh"

// \brief A router that has multiple network interfaces and
// performs longest-prefix-match routing between them.
class Router
{
public:
  // Add an interface to the router
  // \param[in] interface an already-constructed network interface
  // \returns The index of the interface after it has been added to the router
  size_t add_interface( std::shared_ptr<NetworkInterface> interface )
  {
    _interfaces.push_back( notnull( "add_interface", std::move( interface ) ) );
    return _interfaces.size() - 1;
  }

  // Access an interface by index
  std::shared_ptr<NetworkInterface> interface( const size_t N ) { return _interfaces.at( N ); }

  // Add a route (a forwarding rule)
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  // Route packets between the interfaces
  void route();

private:
  struct prefix_info
  {
    uint32_t mask_; // 子网掩码
    uint32_t netID_;
    explicit prefix_info( const uint8_t prefix_length, const uint32_t prefix )
      : mask_ { ~( UINT32_MAX >> ( prefix_length ) ) }, netID_ { prefix & mask_ } // 只记录网络号
    {}
    auto operator<=>( const prefix_info& other ) const
    {
      return other.mask_ != mask_ ? // 先按前缀长度降序，再按网络号的数值大小降序
               mask_ <=> other.mask_
                                  : netID_ <=> other.netID_;
    }
  };
  // 路由表的数据结构类型
  using routerT = std::multimap<prefix_info, std::pair<size_t, std::optional<Address>>, std::greater<prefix_info>>;
  routerT::const_iterator find_export( const uint32_t target_dst ) const;
  // 路由表，（按子网掩码长度）降序排序所有路由条目
  routerT router_table_ {};

  // The router's collection of network interfaces
  std::vector<std::shared_ptr<NetworkInterface>> _interfaces {};
};