#include "reassembler.hh"
#include <iostream>

#include <set>
using namespace std;

void Reassembler::insert_waiting_substring( struct Node& node )
{
    if ( waiting_substring.empty() ) {
        waiting_substring.insert( node );
        bytes_unassembled += node.data.size();
        return;
    }
    auto it = waiting_substring.lower_bound( node ); // lower_bound返回不小于目标值的第一个对象的迭代器
    // 若node的左边有节点，考察是否能与左边的节点合并
    if ( it != waiting_substring.begin() ) {
        it--;                                             // 找到前面的节点
        if ( node.index < it->index + it->data.size() ) { // 若node与左边相交（相邻不算）或被包含
            if ( node.index + node.data.size() <= it->index + it->data.size() )
                return; // 如果被包含，直接丢弃
            else {    // 若相交，将node与左边节点合并，并删除左边节点
                node.data = it->data + node.data.substr( it->index + it->data.size() - node.index );
                node.index = it->index;
                bytes_unassembled -= it->data.size();
                waiting_substring.erase( it++ );
            }
        } else {
            it++;
        }
    }
    // it指向node的右边节点
    // 考察是否能与右边的节点合并，可能与多个节点合并
    while ( it != waiting_substring.end() && node.index + node.data.size() > it->index ) {
        if ( node.index >= it->index && node.index + node.data.size() < it->index + it->data.size() )
            return;
        if ( node.index + node.data.size() < it->index + it->data.size() ) {
            node.data = node.data + it->data.substr( node.index + node.data.size() - it->index );
        }
        bytes_unassembled -= it->data.size();
        waiting_substring.erase( it++ );
    }
    waiting_substring.insert( node );
    bytes_unassembled += node.data.size();
}
void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
    struct Node node
    {
    data, first_index
};

    size_t first_unassembled = output_.writer().bytes_pushed();
    size_t first_unacceptable = first_unassembled + writer().available_capacity();

    if ( first_index + data.size() < first_unassembled || first_index >= first_unacceptable )
        return; // 舍弃所有不合要求的数据
    if ( first_index + data.size() > first_unacceptable )
        node.data = node.data.substr( 0, first_unacceptable - first_index ); // 去尾
    if ( first_index <= first_unassembled && first_index + data.size() > first_unassembled ) {
        node.data = node.data.substr( first_unassembled - first_index );
        node.index = first_unassembled;
    } // 掐头

    if ( node.index == first_unassembled ) {
        // 若可以直接写入
        output_.writer().push( node.data );
        first_unassembled = first_unassembled + node.data.size();
        // 检查缓冲区中的子串能否继续写入
        auto it = waiting_substring.begin();
        while ( it->index <= first_unassembled ) { // 处理缓冲区中应该被写入的数据
            if ( it->index + it->data.size() > node.index + node.data.size() ) {
                // 对于应该被写入的数据掐头并push
                output_.writer().push( it->data.substr( first_unassembled - it->index ) );
                bytes_unassembled -= it->data.size();
                first_unassembled += ( it->data.substr( first_unassembled - it->index ) ).size();
                waiting_substring.erase( it++ );
            } else {
                // 对于已经push的数据删除
        bytes_unassembled -= it->data.size();
        waiting_substring.erase( it++ );
      }
    }
  } else {
    insert_waiting_substring( node );
  }
  if ( is_last_substring ) {
    flag_eof = true;
    pos_eof = first_index + data.size();
  }

  if ( flag_eof && first_unassembled == pos_eof )
    output_.writer().close();
}

uint64_t Reassembler::bytes_pending() const
{
  // Your code here.
  return bytes_unassembled;
}
