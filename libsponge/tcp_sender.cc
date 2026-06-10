#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , RXT (RXTimer (retx_timeout))
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _recv_ackno; }

void TCPSender::fill_window() {
    if (_send_fin == true) return ;
    size_t effective_window = _window_size == 0 ? 1 : _window_size;
    while (1){
        if (_send_syn == false){
            TCPSegment tcp;
            tcp.header().seqno = wrap(_next_seqno, _isn);
            tcp.header().syn = true;
            _next_seqno += tcp.length_in_sequence_space();
            _segments_out.push (tcp);
            undone_data.push (tcp);
            _send_syn = true;
            if (RXT.running() == false){
                RXT.reset_RTO();
                RXT.startup();
            }
            continue;
        }
        if (_recv_ackno + effective_window < _next_seqno) break;
        size_t available_window = effective_window - (_next_seqno - _recv_ackno);
        if (_send_fin == false && _stream.input_ended() && _stream.buffer_size() == 0){
            if (available_window > 0){
                TCPSegment tcp;
                tcp.header().seqno = wrap(_next_seqno, _isn);
                tcp.header().fin = true;
                _next_seqno += tcp.length_in_sequence_space();
                _segments_out.push (tcp);
                undone_data.push (tcp);
                _send_fin = true;
                if (RXT.running() == false){
                    RXT.reset_RTO();
                    RXT.startup();
                }
                continue;
            }
            else break;
        }
        TCPSegment tcp;
        tcp.header().seqno = wrap(_next_seqno, _isn);
        size_t payload_size = min ({TCPConfig::MAX_PAYLOAD_SIZE, _stream.buffer_size(), available_window});
        if (payload_size == 0) break;
        string data = _stream.read (payload_size);
        tcp.payload() = Buffer (move(data));
        if (_send_fin == false && _stream.input_ended() && _stream.buffer_size() == 0 && available_window > payload_size){
            tcp.header().fin = true;
            _send_fin = true;
        }
        _segments_out.push (tcp);
        undone_data.push (tcp);
        _next_seqno += tcp.length_in_sequence_space();
        if (RXT.running() == false){
            RXT.reset_RTO();
            RXT.startup();
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // DUMMY_CODE(ackno, window_size);
    uint64_t abs_ackno = unwrap (ackno, _isn, _next_seqno);
    if (abs_ackno > _next_seqno) return false;
    rx_times = 0;
    RXT.reset_RTO();
    _recv_ackno = abs_ackno;
    _window_size = window_size;
    while (!undone_data.empty()){
        TCPSegment it = undone_data.front();
        uint64_t abs_seqno = unwrap(it.header().seqno, _isn, _next_seqno);
        if (abs_seqno + it.length_in_sequence_space() <= abs_ackno){
            undone_data.pop ();
        }
        else break;
    }
    if (!undone_data.empty()) RXT.startup();
    else RXT.shutup();
    fill_window();
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    RXT.time_plus (ms_since_last_tick);
    if (RXT.outofdate()){
        if (!undone_data.empty()){
            TCPSegment beginning = undone_data.front();
            _segments_out.push (beginning);
            rx_times ++;
            RXT.double_RTO ();
            RXT.startup ();
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return rx_times; }

void TCPSender::send_empty_segment() {
    TCPSegment tcp;
    tcp.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push (tcp);
}
