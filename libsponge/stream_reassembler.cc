#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

#include <iostream>
using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), Buf() {}

void StreamReassembler::insert_into_buf (Segment seg_new){
    while (1){
        auto itR = Buf.lower_bound (seg_new);
        if (itR != Buf.end()){
            if (seg_new.end >= itR->start - 1){
                seg_new = Seg_Merge (seg_new, *itR);
                unsolved_bytes -= itR->end - itR->start + 1;
                Buf.erase (itR);
            }
            else break;
        }
        else break;
    }
    while (1){
        auto itL = Buf.lower_bound (seg_new);
        if (itL != Buf.begin()){
            itL --;
            if (itL->end >= seg_new.start - 1){
                seg_new = Seg_Merge (*itL, seg_new);
                unsolved_bytes -= itL->end - itL->start + 1;
                Buf.erase (itL);
            }
            else break;
        }
        else break;
    }
    unsolved_bytes += seg_new.end - seg_new.start + 1;
    Buf.insert (seg_new);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    int length = data.length();
    if (length == 0) return eof ? _output.end_input() : void();
    if (index + length - 1 < solved_bytes) return ;
    Segment seg_new(index, index + length - 1, data, eof);
    if (index < solved_bytes){
        seg_new.data = data.substr (solved_bytes - index,index + length - solved_bytes);
        seg_new.start = solved_bytes;
    }
    insert_into_buf (seg_new);
    if (!Buf.empty() && Buf.begin()->start == solved_bytes){
        auto it = Buf.begin();
        _output.write (it->data);
        if (it->eof) _output.end_input ();
        solved_bytes = it->end + 1;
        unsolved_bytes -= it->end - it->start + 1;
        Buf.erase (it);
    }
}

size_t StreamReassembler::unassembled_bytes() const { return unsolved_bytes; }

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }
