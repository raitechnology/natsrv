#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <natsrv/ping.h>
#include <raikv/ev_publish.h>
#include <raimd/md_types.h>

using namespace rai;
using namespace natsrv;
using namespace kv;
using namespace md;

PingEndpoint::PingEndpoint( EvPoll &p, EvTimerCallback &c )
            : EvSocket( p, p.register_type( "ping_endpoint" ) ),
              sub_route( p.sub_route ),
              cb( c ), reconnect_time( 0 ), reconnect_timeout_secs( 1 ),
              is_reconnecting( false ), is_shutdown( false )
{
  this->sock_opts = OPT_NO_POLL;
  ::memset( &this->last_msg, 0, sizeof( this->last_msg ) );
}

int
PingEndpoint::init_endpoint( bool reflect,  const char *sub_subject,
                             const char *pub_subject,  uint64_t id,
                             uint64_t cnt ) noexcept
{
  int status, pfd = this->poll.get_null_fd();

  this->PeerData::init_peer( pfd, NULL, "ping_endpoint" );
  if ( (status = this->poll.add_sock( this )) != 0 )
    return status;

  if ( reflect ) {
    this->sub = pub_subject;
    this->pub = sub_subject;
  }
  else {
    this->sub = sub_subject;
    this->pub = pub_subject;
  }
  this->is_reflect = reflect;
  this->sublen     = ::strlen( this->sub );
  this->sub_h      = kv_crc_c( this->sub, this->sublen, 0 );
  this->publen     = ::strlen( this->pub );
  this->pub_h      = kv_crc_c( this->pub, this->publen, 0 );
  this->ping_ival  = 1000000000,
  this->ping_timer = current_monotonic_time_ns() + ping_ival,
  this->seqno      = 0,
  this->my_id      = id;
  this->ping_count = cnt;
  this->idle_count = 0;

  return 0;
}

bool
PingEndpoint::loop( uint64_t &delta ) noexcept
{
  uint64_t src, stamp, num, now;

  delta = 0;
  if ( this->poll.quit >= 5 )
    return false;

  if ( this->poll.dispatch() == EvPoll::DISPATCH_IDLE )
    this->idle_count++;
  else
    this->idle_count = 0;

  if ( ! this->recv_ping( src, stamp, num ) ) {
    now = current_monotonic_time_ns();
    /* activly send pings every interval until response */
    if ( ! this->is_reflect && ! this->is_shutdown ) {
      if ( now >= this->ping_timer ) {
        if ( this->send_ping( this->my_id, now, this->seqno ) ) {
          this->seqno++;
          this->ping_timer = now + this->ping_ival;
          return true;
        }
      }
    }
    /* no activity, wait for pings */
    this->poll.wait( this->idle_count > 255 ?
                     ( this->ping_timer - now ) / 1000000 : 0 );
    return true;
  }
  /* if I recvd a ping that I sent */
  if ( src == this->my_id ) {
    now = current_monotonic_time_ns();
    if ( this->ping_count > 0 && --this->ping_count == 0 ) {
      this->is_shutdown = true;
      this->poll.quit++;
    }
    else if ( ! this->is_shutdown ) {
      if ( this->send_ping( this->my_id, now, this->seqno ) ) {
        this->seqno++;
        this->ping_timer = now + this->ping_ival;
      }
    }
    delta = now - stamp;
  }
  /* if not mine, reflect it */
  else {
    this->send_ping( src, stamp, num );
  }
  return true;
}

void
PingEndpoint::start_sub( void ) noexcept
{
  NotifySub nsub( this->sub, this->sublen, this->sub_h, this->fd, false, 'V' );
  this->sub_route.add_sub( nsub );
}

bool
PingEndpoint::on_msg( EvPublish &pub ) noexcept
{
  if ( pub.msg_len == sizeof( PingMsg ) )
    ::memcpy( &this->last_msg, pub.msg, sizeof( PingMsg ) );
  return true;
}

bool
PingEndpoint::recv_ping( uint64_t &src, uint64_t &stamp,
                         uint64_t &num ) noexcept
{
  if ( this->last_msg.ping_hdr[ 0 ] == 'P' ) {
    src   = this->last_msg.ping_src;
    stamp = this->last_msg.time_sent;
    num   = this->last_msg.seqno_sent;
    this->last_msg.ping_hdr[ 0 ] = 0;
    return true;
  }
  return false;
}

bool
PingEndpoint::send_ping( uint64_t src,  uint64_t stamp,
                         uint64_t num ) noexcept
{
  PingMsg  m;
  ::memcpy( m.ping_hdr, "PING1234", 8 );
  m.ping_src   = src;
  m.time_sent  = stamp;
  m.seqno_sent = num;
  EvPublish pub( this->pub, this->publen, NULL, 0, &m, sizeof( m ),
                 this->sub_route, this->fd, this->pub_h, MD_OPAQUE, 'v' );
  return this->sub_route.forward_msg( pub );
}

void
PingEndpoint::connect_failed( void ) noexcept
{
  fprintf( stderr, "create socket failed\n" );
  this->setup_reconnect();
}

void
PingEndpoint::on_connect( EvSocket &conn ) noexcept
{
  printf( "connected %s\n", conn.peer_address.buf );
  this->start_sub();
}

void
PingEndpoint::on_shutdown( EvSocket &conn,  const char *, size_t ) noexcept
{
  printf( "disconnected %s\n", conn.peer_address.buf );
  this->setup_reconnect();
}

void
PingEndpoint::setup_reconnect( void ) noexcept
{
  if ( this->is_reconnecting || this->is_shutdown )
    return;

  this->is_reconnecting = true;
  double now = current_monotonic_time_s();
  if ( this->reconnect_time != 0 && this->reconnect_time +
         (double) this->reconnect_timeout_secs * 2 > now ) {
    this->reconnect_timeout_secs *= 2;
    if ( this->reconnect_timeout_secs > 16 )
      this->reconnect_timeout_secs = 16;
  }
  else {
    this->reconnect_timeout_secs = 1;
  }
  this->reconnect_time = now;
  printf( "reconnect in %u seconds\n", this->reconnect_timeout_secs );
  this->poll.timer.add_timer_seconds( this->cb, this->reconnect_timeout_secs,
                                      0, 0 );
}

void PingEndpoint::write( void ) noexcept {}
void PingEndpoint::read( void ) noexcept {}
void PingEndpoint::process( void ) noexcept {}
void PingEndpoint::release( void ) noexcept {}

