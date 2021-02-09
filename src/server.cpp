#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <natsmd/ev_nats_client.h>
#include <sassrv/ev_rv.h>
#include <raikv/mainloop.h>

using namespace rai;
using namespace natsmd;
using namespace sassrv;
using namespace kv;

struct Args : public MainLoopVars { /* argv[] parsed args */
  int rv_port;
  const char * user,
             * passwd,
             * auth_token;
  Args() : rv_port( 0 ), user( 0 ), passwd( 0 ), auth_token( 0 ) {}
};

struct MyListener : public EvRvListen, public EvNatsClient {
  void * operator new( size_t, void *ptr ) { return ptr; }
  MyListener( kv::EvPoll &p ) : EvRvListen( p ), EvNatsClient( p ) {}
  char svc_buf[ 16 ];

  virtual int start_host( void ) noexcept final {
    /*uint8_t * rcv = (uint8_t *) (void *) &this->mcast.recv_ip[ 0 ],
            * snd = (uint8_t *) (void *) &this->mcast.send_ip,
            * hst = (uint8_t *) (void *) &this->mcast.host_ip;*/
    size_t len = this->service_len;
    if ( len > sizeof( this->svc_buf ) - 1 ) {
      fprintf( stderr, "service too long\n" );
      this->EvNatsClient::name = NULL;
    }
    else {
      ::memcpy( this->svc_buf, this->service, this->service_len );
      this->svc_buf[ this->service_len ] = '\0';
      this->EvNatsClient::name = this->svc_buf;
    }
    printf( "start_network:        service %.*s, \"%.*s\"\n",
            (int) this->service_len, this->service, (int) this->network_len,
            this->network );
    if ( ! this->EvNatsClient::connect( "127.0.0.1", 4222 ) ) {
      printf( "failed\n" );
      return -1;
    }
    return 0;
  }
  virtual void on_connect( void ) noexcept final {
    printf( "connected\n" );
    this->EvRvListen::start_host();
  }
  virtual void on_shutdown( uint64_t bytes_lost ) noexcept final {
    if ( bytes_lost != 0 )
      printf( "bytes_lost %lu\n", bytes_lost );
    printf( "shutdown\n" );
  }
  virtual int stop_host( void ) noexcept final {
    printf( "stop_network:         service %.*s, \"%.*s\"\n",
            (int) this->service_len, this->service, (int) this->network_len,
            this->network );
    this->EvRvListen::stop_host();
    this->EvNatsClient::do_shutdown();
    return 0;
  }
};

struct Loop : public MainLoop<Args> {
  Loop( EvShm &m,  Args &args,  int num, bool (*ini)( void * ) ) :
    MainLoop<Args>( m, args, num, ini ) {}

 MyListener * rv_sv;

  bool rv_init( void ) {
    if ( ! Listen<MyListener>( 0, this->r.rv_port, this->rv_sv,
                               this->r.tcp_opts ) )
      return false;
    this->rv_sv->lang       = "C";
    this->rv_sv->version    = kv_stringify( NATSRV_VER );
    this->rv_sv->user       = this->r.user;
    this->rv_sv->pass       = this->r.passwd;
    this->rv_sv->auth_token = this->r.auth_token;
    return true;
  }
  bool init( void ) {
    printf( "natsrv_version:       " kv_stringify( NATSRV_VER ) "\n" );
    printf( "rv_daemon:            %d\n", this->r.rv_port );
    return this->rv_init();
  }
  static bool initialize( void *me ) noexcept {
    return ((Loop *) me)->init();
  }
};


int
main( int argc, const char *argv[] )
{
  #define NATS_USER_ENV  "NATS_USER"
  #define NATS_PASS_ENV  "NATS_PASS"
  #define NATS_TOKEN_ENV "AUTH_TOKEN"
  EvShm shm;
  Args  r;

  r.no_threads   = true;
  r.no_reuseport = true;
  r.no_map       = true;
  r.no_default   = true;
  r.all          = true;
  r.add_desc( "  -p rv    = listen rv port        (7500)" );
  r.add_desc( "  -u user  = NATS user             (" NATS_USER_ENV ")" );
  r.add_desc( "  -x pass  = NATS password         (" NATS_PASS_ENV ")" );
  r.add_desc( "  -a auth  = NATS auth_token       (" NATS_TOKEN_ENV ")" );
  if ( ! r.parse_args( argc, argv ) )
    return 1;
  if ( shm.open( r.map_name, r.db_num ) != 0 )
    return 1;
  shm.print();
  r.rv_port    = r.parse_port( argc, argv, "-p", "7500" );
  r.user       = r.get_arg( argc, argv, 1, "-u", NULL, NATS_USER_ENV );
  r.passwd     = r.get_arg( argc, argv, 1, "-x", NULL, NATS_PASS_ENV );
  r.auth_token = r.get_arg( argc, argv, 1, "-a", NULL, NATS_TOKEN_ENV );
  Runner<Args, Loop> runner( r, shm, Loop::initialize );
  if ( r.thr_error == 0 )
    return 0;
  return 1;
}

