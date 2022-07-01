#ifndef __rai_natsrv__ping_h__
#define __rai_natsrv__ping_h__

#include <raikv/ev_net.h>
#include <raikv/util.h>

namespace rai {
namespace natsrv {

static const size_t PING_MSG_SIZE = 32; /* 8 * 4 */
struct PingMsg {
  char     ping_hdr[ 8 ];
  uint64_t ping_src,
           time_sent,
           seqno_sent;
};

struct PingEndpoint : public kv::EvSocket, public kv::EvConnectionNotify {
  kv::RoutePublish    & sub_route;
  kv::EvTimerCallback & cb;
  double                reconnect_time;
  uint16_t              reconnect_timeout_secs;
  bool                  is_reconnecting,
                        is_shutdown,
                        is_reflect;
  PingMsg               last_msg;
  const char          * sub,
                      * pub;
  size_t                sublen,
                        publen;
  uint32_t              sub_h,
                        pub_h;
  uint64_t              my_id,
                        seqno,
                        ping_ival,
                        ping_timer,
                        ping_count,
                        idle_count;

  PingEndpoint( kv::EvPoll &p, kv::EvTimerCallback &c );
  int init_endpoint( bool reflect,  const char *sub_subject,
                     const char *pub_subject,  uint64_t id,
                     uint64_t cnt ) noexcept;
  bool loop( uint64_t &delta ) noexcept;
  void start_sub( void ) noexcept;
  /* EvSocket */
  virtual bool on_msg( kv::EvPublish &pub ) noexcept;
  virtual void write( void ) noexcept;
  virtual void read( void ) noexcept;
  virtual void process( void ) noexcept;
  virtual void process_close( void ) noexcept;
  virtual void release( void ) noexcept;
  bool recv_ping( uint64_t &src, uint64_t &stamp, uint64_t &num ) noexcept;
  bool send_ping( uint64_t src,  uint64_t stamp,  uint64_t num ) noexcept;

  void connect_failed( void ) noexcept;
  void setup_reconnect( void ) noexcept;

  virtual void on_connect( kv::EvSocket &conn ) noexcept;
  virtual void on_shutdown( kv::EvSocket &conn,  const char *,
                            size_t ) noexcept;
};

template <class Parameters, class Protocol>
struct Ping : public Protocol, public PingEndpoint, public kv::EvTimerCallback {
  Parameters & parameters;

  Ping( kv::EvPoll &p,  Parameters &parm )
    : Protocol( p ), PingEndpoint( p, *this ), parameters( parm ) {}

  bool do_connect( void ) {
    if ( ! this->Protocol::connect( this->parameters, this ) ) {
      this->PingEndpoint::connect_failed();
      return false;
    }
    return true;
  }
  virtual bool timer_cb( uint64_t, uint64_t ) noexcept {
    if ( this->PingEndpoint::is_reconnecting ) {
      this->PingEndpoint::is_reconnecting = false;
      if ( ! this->PingEndpoint::is_shutdown )
        this->do_connect();
    }
    return false;
  }
};
}
}
#endif
