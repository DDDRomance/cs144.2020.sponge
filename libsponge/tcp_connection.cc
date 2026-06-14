#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_receivd; }

void TCPConnection::send_segments() {
    _sender.fill_window();
    bool sent = false;
    while (!_sender.segments_out().empty()) {
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (auto ackno = _receiver.ackno(); ackno.has_value()) {
            seg.header().ack = true;
            seg.header().ackno = ackno.value();
            seg.header().win = _receiver.window_size();
        }
        if (seg.header().fin) outbound_fin_sent = true;
        _segments_out.push(seg);
        sent = true;
    }
    if (_need_ack && !sent && _receiver.ackno().has_value()) {
        _sender.send_empty_segment();
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
        seg.header().win = _receiver.window_size();
        _segments_out.push(seg);
    }
    _need_ack = false;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_receivd = 0;
    _need_ack = false;

    bool in_listen = (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0);
    bool in_syn_sent = (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() > 0);

    // ---- RST ----
    if (seg.header().rst) {
        if (in_listen) {
            return;
        }
        if (in_syn_sent) {
            if (seg.header().ack && seg.header().ackno == _sender.next_seqno()) {
                _sender.stream_in().set_error();
                _receiver.stream_out().set_error();
            }
            return;
        }
        bool in_window = _receiver.segment_received(seg);
        if (!in_window && _receiver.ackno().has_value() && seg.header().seqno == _receiver.ackno().value()) {
            in_window = true;
        }
        if (in_window) {
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
        }
        return;
    }

    // ---- SYN_SENT ACK ----
    if (in_syn_sent) {
        if (seg.header().ack) {
            if (seg.header().ackno == _sender.next_seqno()) {
                if (seg.header().syn) {
                    _sender.ack_received(seg.header().ackno, seg.header().win);
                } else {
                    return;
                }
            } else {
                return;
            }
        } else if (!seg.header().syn) {
            return;
        }
    }

    // ---- LISTEN non-SYN ----
    if (in_listen && !seg.header().syn) {
        return;
    }

    // ---- normal processing (ESTABLISHED+) ----
    _need_ack = (seg.length_in_sequence_space() > 0);

    if (seg.header().ack && !in_syn_sent) {
        _need_ack |= !_sender.ack_received(seg.header().ackno, seg.header().win);
    }

    bool recv_ok = _receiver.segment_received(seg);
    bool at_expected_seqno = _receiver.ackno().has_value() && seg.header().seqno == _receiver.ackno().value();
    if (seg.length_in_sequence_space() > 0 || !at_expected_seqno) _need_ack |= !recv_ok;

    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    send_segments();
}
bool TCPConnection::active() const {
    if (_sender.stream_in().error() || _receiver.stream_out().error()) return false;

    bool inbound_done = (unassembled_bytes() == 0 && _receiver.stream_out().input_ended());
    bool outbound_done = (_sender.stream_in().eof() && outbound_fin_sent && bytes_in_flight() == 0);

    if (inbound_done && outbound_done) {
        if (!_linger_after_streams_finish) return false;
        if (time_since_last_segment_received() >= 10 * _cfg.rt_timeout) return false;
    }
    return true;
}

size_t TCPConnection::write(const string &data) {
    size_t writebytes = _sender.stream_in().write(data);
    send_segments();
    return writebytes;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _time_since_last_segment_receivd += ms_since_last_tick;
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _sender.send_empty_segment();
        TCPSegment seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        seg.header().rst = true;
        _segments_out.push(seg);
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }
    if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0 && !_sender.stream_in().eof()) {
        return;
    }
    send_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    send_segments();
}

void TCPConnection::connect() { send_segments(); }

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
