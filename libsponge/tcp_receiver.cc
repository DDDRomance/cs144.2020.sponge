#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;


bool TCPReceiver::segment_received(const TCPSegment &seg) {
    if (syn_received == false){
        if (seg.header().syn == false) return false;
        syn_received = true;
        _isn = seg.header().seqno;
        if (seg.length_in_sequence_space() > 1) {
            // 数据部分的绝对序列号从 ISN+1 开始，对应 stream index 0
            std::string data = seg.payload().copy();
            _reassembler.push_substring(data, 0, seg.header().fin);
        }
        return true;
    }

    absolute_ackno = _reassembler.stream_out().bytes_written() + 1 + _reassembler.stream_out().input_ended();
    

    size_t window_L = absolute_ackno;
    size_t window_R = window_L + window_size();
    // if (window_L == window_R) window_R ++;
    size_t seq_L = unwrap(seg.header().seqno, _isn, absolute_ackno);
    size_t seq_R = seq_L + seg.length_in_sequence_space();

    if (window_R <= seq_L || seq_R <= window_L) return false;
    Buffer payload = seg.payload();
    string data = payload.copy();
    size_t start = unwrap(seg.header().seqno, _isn, absolute_ackno) + seg.header().syn - 1;
    _reassembler.push_substring(data, start, seg.header().fin);

    return true;

}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!syn_received) return nullopt;
    uint64_t abs_ack = _reassembler.stream_out().bytes_written() + 1 + _reassembler.stream_out().input_ended();
    return wrap(abs_ack, _isn);
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }