#pragma once
#include <boost/beast/http/message.hpp>

template <bool isRequest, typename Body, typename Fields>
struct fmt::formatter<boost::beast::http::message<isRequest, Body, Fields>> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const boost::beast::http::message<isRequest, Body, Fields>& msg,
              FormatContext& ctx) {
    boost::beast::http::serializer<isRequest, Body, Fields> sr{msg};
    boost::beast::error_code ec;
    auto outputIt = ctx.out();
    do {
      sr.next(ec, [&sr, &outputIt](boost::beast::error_code ec, auto buffers) {
        ec = {};
        std::size_t bytes_transferred = 0;
        for (auto b : buffers_range(buffers)) {
          outputIt = format_to(
              outputIt, "{}",
              std::string_view{reinterpret_cast<const char*>(b.data()), b.size()});

          bytes_transferred += b.size();
        }
        sr.consume(bytes_transferred);
      });

    } while (!sr.is_done());

    return outputIt;
  }
};

struct json_body_reader {
  using const_buffers_type = boost::asio::const_buffer;

  boost::json::value& body;
  boost::json::parser p;
  template <bool isRequest, class Fields>
  json_body_reader(boost::beast::http::header<isRequest, Fields>& /*h*/,
                   boost::json::value& body)
      : body(body) {}

  void init(boost::optional<std::uint64_t> const& /* content_length */,
            boost::system::error_code& ec) {
    p.start();
    ec.assign(0, ec.category());
  }

  template <class ConstBufferSequence>
  std::size_t put(ConstBufferSequence const& buffers,
                  boost::system::error_code& ec) {
    // The specification requires this to indicate "no error"
    ec = {};
    for (auto i = boost::asio::buffer_sequence_begin(buffers);
         i != boost::asio::buffer_sequence_end(buffers); i++) {
      p.write(reinterpret_cast<const char*>(i->data()), i->size(), ec);
      if (ec) {
        return 0;
      }
    }

    return boost::asio::buffer_size(buffers);
  }
  void finish(boost::system::error_code& ec) {
    p.finish(ec);
    if (ec) {
      return;
    }
    body = p.release();
  }
};

struct json_body_writer {
  const boost::json::value& j;
  boost::json::serializer s;
  boost::beast::flat_static_buffer<1024> buf;
  using const_buffers_type =
      boost::beast::flat_static_buffer<1024>::const_buffers_type;

  template <bool isRequest, class Fields>
  json_body_writer(
      boost::beast::http::header<isRequest, Fields> const& /*  h */,
      boost::json::value const& body)
      : j(body) {}

  void init(boost::system::error_code& ec) {
    ec = {};
    s.reset(j);
  }

  boost::optional<std::pair<const_buffers_type, bool>> get(
      boost::system::error_code& ec) {
    ec = {};
    buf.consume(buf.size());
    decltype(buf)::mutable_buffers_type bufseq =
        buf.prepare(buf.capacity() - buf.size());

    if (s.is_done()) {
      return boost::none;
    }

    for (auto i = boost::asio::buffer_sequence_begin(bufseq);
         i != boost::asio::buffer_sequence_end(bufseq); i++) {
      size_t n = s.read(reinterpret_cast<char*>(i->data()), i->size());
      buf.commit(n);
      if (s.is_done()) {
        break;
      }
    }

    return {{buf.data(), !s.is_done()}};
  }
};

struct json_body {
  using value_type = boost::json::value;
  using reader = json_body_reader;
  using writer = json_body_writer;
};