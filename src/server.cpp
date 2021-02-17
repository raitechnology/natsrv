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
  int          rv_port,
               nats_port;
  const char * user,
             * passwd,
             * auth_token;
  Args() : rv_port( 0 ), nats_port( 0 ),
           user( 0 ), passwd( 0 ), auth_token( 0 ) {}
};

struct MyListener : public EvRvListen, public EvNatsClient,
                    public EvNatsClientNotify {
  void * operator new( size_t, void *ptr ) { return ptr; }
  int             nats_port;
  char            svc_buf[ 32 ];
  EvNatsClient ** clients;
  char         ** users;
  size_t          client_cnt,
                  connect_cnt,
                  start_cnt,
                  stop_cnt,
                  network_cnt;
  uint64_t        total_bytes_lost;

  MyListener( kv::EvPoll &p ) : EvRvListen( p ), EvNatsClient( p ),
                                nats_port( 0 ), clients( 0 ), users( 0 ),
                                client_cnt( 0 ), connect_cnt( 0 ),
                                start_cnt( 0 ), stop_cnt( 0 ), network_cnt( 0 ),
                                total_bytes_lost( 0 ) {}
  /* EvRvListen */
  virtual int start_host( void ) noexcept final {
    /*uint8_t * rcv = (uint8_t *) (void *) &this->mcast.recv_ip[ 0 ],
            * snd = (uint8_t *) (void *) &this->mcast.send_ip,
            * hst = (uint8_t *) (void *) &this->mcast.host_ip;*/
    size_t len = this->service_len;
    if ( len > sizeof( this->svc_buf ) - 1 ) {
      fprintf( stderr, "service too long\n" );
      this->EvNatsClient::name = NULL;
      if ( this->EvNatsClient::user == this->svc_buf )
        this->EvNatsClient::user = NULL;
    }
    else {
      ::memcpy( this->svc_buf, this->service, this->service_len );
      this->svc_buf[ this->service_len ] = '\0';
      this->EvNatsClient::name = this->svc_buf;
      if ( this->EvNatsClient::user == NULL )
        this->EvNatsClient::user = this->svc_buf;
    }
    printf( "start_network:        service %.*s",
            (int) this->service_len, this->service );
    if ( this->network_len > 0 ) {
      printf( ", \"%.*s\"", (int) this->network_len,
            this->network );
    }
    printf( "\n" );

    uint32_t mcast_ip[ RvMcast::MAX_RECV_MCAST + 1 ];
    uint8_t  overlay[ RvMcast::MAX_RECV_MCAST + 1 ];
    size_t   i, ip_cnt = 0;

    this->connect_cnt      = 0;
    this->start_cnt        = 0;
    this->stop_cnt         = 0;
    this->network_cnt      = 0;
    this->total_bytes_lost = 0;

    if ( this->mcast.recv_cnt > 0 && this->mcast.recv_ip[ 0 ] != 0 ) {
      bool send_overlaps_recv = false;
      for ( i = 0; i < this->mcast.recv_cnt; i++ ) {
        mcast_ip[ ip_cnt ] = this->mcast.recv_ip[ i ];
        overlay[ ip_cnt ] = 1;
        if ( mcast_ip[ ip_cnt ] == this->mcast.send_ip ) {
          overlay[ ip_cnt ] |= 2;
          send_overlaps_recv = true;
        }
        ip_cnt++;
      }
      if ( ! send_overlaps_recv ) {
        mcast_ip[ ip_cnt ] = this->mcast.send_ip;
        overlay[ ip_cnt ] = 2;
        ip_cnt++;
      }
    }
    if ( ip_cnt != 0 ) {
      if ( ip_cnt > this->client_cnt ) {
        this->clients = (EvNatsClient **) ::realloc( this->clients,
                                   ip_cnt * sizeof( this->clients[ 0 ] ) );
        this->users = (char **) ::realloc( this->users,
                                   ip_cnt * sizeof( this->users[ 0 ] ) );

        for ( ; this->client_cnt < ip_cnt; this->client_cnt++ ) {
          this->clients[ this->client_cnt ] =
            create_nats_client( this->EvRvListen::poll );
          this->users[ this->client_cnt ]   = NULL;
        }
      }
      for ( i = 0; i < ip_cnt; i++ ) {
        char buf[ 32 ];
        uint8_t * q;
        int len;
        q = (uint8_t *) &mcast_ip[ i ];
        len = ::snprintf( buf, sizeof( buf ), "%u.%u.%u.%u:%.*s",
                          q[ 0 ], q[ 1 ], q[ 2 ], q[ 3 ],
                          (int) this->service_len, this->service );
        this->users[ i ] = (char *) ::realloc( this->users[ i ], len + 1 );
        ::strcpy( this->users[ i ], buf );
        this->clients[ i ]->user = this->users[ i ];
        if ( ( overlay[ i ] & 2 ) != 0 )
          this->clients[ i ]->fwd_all_msgs = true;
        else
          this->clients[ i ]->fwd_all_msgs = false;
        if ( ( overlay[ i ] & 1 ) != 0 )
          this->clients[ i ]->fwd_all_subs = true;
        else
          this->clients[ i ]->fwd_all_subs = false;
      }
      for ( i = 0; i < ip_cnt; i++ ) {
        if ( ! this->clients[ i ]->connect( "127.0.0.1", this->nats_port,
                                            this ) )
          break;
        this->connect_cnt++;
        this->network_cnt++;
      }
      if ( i != ip_cnt ) {
        for ( size_t j = 0; j < i; j++ )
          this->clients[ i ]->idle_push( EV_SHUTDOWN );
        return -1;
      }
    }
    else {
      /*printf( "host_parameters:      " );
      this->mcast.print();*/
      if ( ! this->EvNatsClient::connect( "127.0.0.1", this->nats_port,
                                          this ) ) {
        printf( "failed\n" );
        return -1;
      }
      this->connect_cnt++;
    }
    return 0;
  }
  virtual int stop_host( void ) noexcept final {
    printf( "stop_network:         service %.*s",
            (int) this->service_len, this->service );
    if ( this->network_len > 0 ) {
      printf( ", \"%.*s\"", (int) this->network_len,
            this->network );
    }
    printf( "\n" );
    this->EvRvListen::stop_host();
    if ( this->network_cnt == 0 )
      this->EvNatsClient::do_shutdown();
    else {
      for ( size_t i = 0; i < this->network_cnt; i++ )
        this->clients[ i ]->do_shutdown();
    }
    return 0;
  }
  /* EvNatsClientNotify */
  virtual void on_connect( void ) noexcept final {
    if ( ++this->start_cnt == this->connect_cnt ) {
      printf( "connected\n" );
      this->EvRvListen::start_host();
    }
  }
  virtual void on_shutdown( uint64_t bytes_lost ) noexcept final {
    this->total_bytes_lost += bytes_lost;
    if ( ++this->stop_cnt == this->connect_cnt ) {
      if ( this->total_bytes_lost != 0 )
        printf( "bytes_lost %lu\n", this->total_bytes_lost );
      printf( "shutdown\n" );
    }
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
    this->rv_sv->nats_port  = this->r.nats_port;
    return true;
  }
  bool init( void ) {
    printf( "natsrv_version:       " kv_stringify( NATSRV_VER ) "\n" );
    printf( "rv_daemon:            %d\n", this->r.rv_port );
    printf( "nats_daemon:          %d\n", this->r.nats_port );
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
  r.add_desc( "  -n nats  = nats port             (4222)" );
  r.add_desc( "  -u user  = NATS user             (" NATS_USER_ENV ")" );
  r.add_desc( "  -x pass  = NATS password         (" NATS_PASS_ENV ")" );
  r.add_desc( "  -a auth  = NATS auth_token       (" NATS_TOKEN_ENV ")" );
  if ( ! r.parse_args( argc, argv ) )
    return 1;
  if ( shm.open( r.map_name, r.db_num ) != 0 )
    return 1;
  shm.print();
  r.rv_port    = r.parse_port( argc, argv, "-p", "7500" );
  r.nats_port  = r.parse_port( argc, argv, "-n", "4222" );
  r.user       = r.get_arg( argc, argv, 1, "-u", NULL, NATS_USER_ENV );
  r.passwd     = r.get_arg( argc, argv, 1, "-x", NULL, NATS_PASS_ENV );
  r.auth_token = r.get_arg( argc, argv, 1, "-a", NULL, NATS_TOKEN_ENV );
  Runner<Args, Loop> runner( r, shm, Loop::initialize );
  if ( r.thr_error == 0 )
    return 0;
  return 1;
}

