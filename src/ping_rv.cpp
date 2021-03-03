#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <hdr_histogram.h>
#include <natsrv/ping.h>
#include <sassrv/ev_rv_client.h>

using namespace rai;
using namespace kv;
using namespace natsrv;
using namespace sassrv;

static const char *
get_arg( int argc, char *argv[], int b, const char *f, const char *def )
{
  for ( int i = 1; i < argc - b; i++ )
    if ( ::strcmp( f, argv[ i ] ) == 0 )
      return argv[ i + b ];
  return def; /* default value */
}

int
main( int argc, char **argv )
{
  SignalHandler sighndl;
  const char * de = get_arg( argc, argv, 1, "-d", "7500" ),
             * ne = get_arg( argc, argv, 1, "-n", 0 ),
             * sv = get_arg( argc, argv, 1, "-s", "7500" ),
             * ct = get_arg( argc, argv, 1, "-c", 0 ),
             * re = get_arg( argc, argv, 0, "-r", 0 ),
             /** bu = get_arg( argc, argv, 0, "-b", 0 ),*/
             * he = get_arg( argc, argv, 0, "-h", 0 );
  uint64_t count = 0;
  
  if ( he != NULL ) {
    fprintf( stderr,
             "%s [-d daemon] [-n network] [-s service] [-r] [-b] [-c count]\n"
             "  -d daemon  = daemon port to connect\n"
             "  -n network = network to ping\n"
             "  -s service = service to ping\n"
             "  -r         = reflect pings\n"
             "  -b         = busy wait\n"
             "  -c count   = number of pings\n", argv[ 0 ] );
    return 1;
  }
  if ( ct != NULL ) {
    if ( atoll( ct ) <= 0 ) {
      fprintf( stderr, "count should be > 0\n" );
      return 1;
    }
    count = (uint64_t) atoll( ct );
  }

  EvPoll poll;
  poll.init( 5, false );

  EvRvClientParameters parm( "127.0.0.1", ne, sv, atoi( de ) );
  Ping<EvRvClientParameters, EvRvClient> conn( poll, parm );

  bool reflect = ( re != NULL );
  conn.init_endpoint( reflect, "TEST", "_TIC.TEST", ::getpid(), count );
  conn.do_connect();
  sighndl.install();
  /*if ( bu != NULL )
    conn.EvRvClient::push( EV_BUSY_POLL );*/
  struct hdr_histogram * histogram = NULL;
  if ( ! reflect )
    hdr_init( 1, 1000000, 3, &histogram );
  uint64_t delta;
  while ( conn.loop( delta ) ) {
    if ( ! reflect && delta != 0 )
      hdr_record_value( histogram, delta );
    if ( sighndl.signaled ) {
      conn.is_shutdown = true;
      poll.quit++;
    }
  }
  /*conn.close();*/
  if ( ! reflect )
    hdr_percentiles_print( histogram, stdout, 5, 1000.0, CLASSIC );
  return 0;
}
