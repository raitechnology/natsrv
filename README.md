# Rai NATS RV Bridge

[![Copr build status](https://copr.fedorainfracloud.org/coprs/injinj/gold/package/natsrv/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/injinj/gold/package/natsrv/)

1. [Description of the NATS RV Bridge](#description-of-the-nats-rv-bridge)
2. [A NATS RV Bridge use case](#a-nats-rv-bridge-use-case)
3. [The NATS RV Bridge limitations and issues](#the-nats-rv-bridge-limitations-and-issues)
4. [Installing the NATS RV Bridge](#installing-the-nats-rv-bridge)

## Description of the NATS RV Bridge

Natsrv is a server that bridges RV over [NATS](https://nats.io) networking.  It
does this by listening to RV clients on a host and transforming the
publish/subscribe protocol into the NATS publish/subscribe protocol.  This
allows legacy RV software to work in an environment without multicast
networking, such as a cloud service.  The
[nats-server clustering](https://docs.nats.io/nats-server/configuration/clustering)
is designed to route subject addressed messages throughout a network by
interconnecting nodes and maintaining the subscription interest of the
cluster.  The subject address design and the wildcard support are compatible
with RV design.

## A NATS RV Bridge use case

A RV network does not need much configuration, the primary resource usage that
an organization must allocate to RV are the multicast addresses that RV uses
to communicate from node to node.  This model of multicast addresses that
underlies a network is maintained with the NATS RV Bridge.  The multicast
address and the service is used as the user authentication method within
NATS [multi tenancy](https://docs.nats.io/nats-server/configuration/securing_nats/accounts)
accounts.

This segregation of NATS RV Bridge services can be important, since the NATS RV
Bridge routes messages in a binary format that other NATS clients will not
understand without special codecs (this is the
[codec](https://github.com/raitechnology/raimd/) used).

There are [example](config) configurations for a 2 node cluster.  This diagram
shows how the nodes would communicate over the NATS network.

![natsrv](natsrv.svg)

The multicast networks used are 225.5.5.5 and 226.6.6.6 over service 7000.  The
accounts in the example configuration have this defined:

```console
accounts {
  A: {
    users: [
      { user: "225.5.5.5:7000" }
    ]
  }
  B: {
    users: [
      { user: "226.6.6.6:7000" }
    ]
  }
}
```

The RV clients that use this network would use this command to connect to the
natsrv_server:

```console
# On host 192.168.25.22
$ sass_rv_client -daemon 7500 -network '192.168.25.0;225.5.5.5;226.6.6.6' -service 7000 -subject RSF.REC.INTC.O
```

The RV server that uses this network would use this command to connect to the
natsrv_server:

```console
# On host 192.168.25.21
$ sass_rv_server -daemon 7500 -network '192.168.25.0;225.6.6.6;226.5.5.5' -service 7000 -subject RSF.REC.INTC.O
```

This use case separates the send and recv networks, which is common in the RV
world.  It is designed to separate the publishers from the subscribers,
where the server is routing messages from a few high rate publishers
through to a larger network of low rate subscribers.  This allows the
publishers to scale, since each can have it's own logical network independent
of the underlying physical network.  Multicast forces all nodes that are
subscribed to see all of the traffic published.  By segregating the multicast
each publisher does not interfere with the other publishers or the subscribers.
For example, a publisher might use a network like this:

```console
$ sass_rv_publisher -daemon 7500 -network '192.168.25.0;225.5.5.5;224.4.4.4' -service 7000
```

And the router might use a network like this:

```console
$ sass_rv_router -daemon 7500 -network '192.168.25.0;226.6.6.6,224.4.4.4;225.5.5.5' -service 7000
```

The router now has 2 recv networks (226.6.6.6,224.4.4.4) for handling an
additional publisher and the subscribers on separate logical networks.

A simpler configuration for a NATS RV Brisge would be to not use a network
parameter at all.  In this case the accounts configuration simply uses the
service parameter:

```console
accounts {
  A: {
    users: [
      { user: "7000" }
    ]
  }
}
```

The RV commands would simply use this:

```console
$ sass_rv_client -daemon 7500 -service 7000 -subject RSF.REC.INTC.O
```

The NATS RV Bridge does not preconfigure the parameters used by the clients,
since they are supplied on the command line of the client, and the NATS Server
does not need to be configured with accounts.  The traffic in that case would
route through the network without any traffic segregation.

## The NATS RV Bridge limitations and issues

These are issues that the NATS RV Bridge has in order of importance:

1. (fixed Feb 19 2021). The NATS Server will disconnect if the consumer falls
   to far behind, configured in seconds.  This needs to be transformed into a
   _RV.ERROR.SYSTEM.DATALOSS... message if messages are lost and the Bridge
   needs to reconnect after a timeout.

2.  (fixed Feb 17 2021) Deduplicate multiple overlapping subscriptions.  If '>'
    and TEST are subscribed then the NATS server will publish messages for each
    subscripton using the
    [SID](https://docs.nats.io/nats-protocol/nats-protocol#sub) used for the
    subsciption.  The NATS RV Bridge does not deduplicate these messages and
    forwards them.  This causes the subscribers of '>' and TEST to see two
    messages published when a TEST message is published.

3. If multiple networks are used and no accounts are created to segregate the
   traffic, loops will occur bacause the subscribers will see traffic that
   the publishers send.  There should be a loop check on startup to make sure
   that loops are not present.  This could be done by publishing to the
   DAEMON inbox, since this is always subscribed.

4. The NATS Bridge only allows one service per process to be attached.  The
   workaround for this is to use multiple daemons for each service defined.
   This may remain, since it is a good idea to separate the processes anyway.

     - natsrv_server -p 7500 --
       rv_client -service 5000 -daemon 7500
     - natsrv_server -p 7501 --
       rv_client -service 5001 -daemon 7501

5. Since each NATS Bridge is identified with a DAEMON.iphex, if multiple
   Bridges are attached to the same service networks, there will be duplicate
   DAEMON identifiers.  These are used as endpoints for subscription management
   and as HOST.STATUS.iphex publishes.  The internal subscription routing will
   work but any RV service that depends on unique DAEMON identifiers may not
   work correctly.  The iphex are constructed resolved from the network
   interface or the hostname in the -network argument, for example 192.168.0.1
   is iphex C0A80001.

6. NATS does not allow wildcards to be published but RV does.  The RV
   LISTEN.START messages are the primary source of wildcard publishes, when
   a wildcard subscription is started.  The encoding of these subjects over
   the NATS network are translated '*' as '+' and '>' as '<':

     - RV client subscribes to TEST.*.SUBJECT.>
     - Bridge publishes a listen start to the NATS network: _RV.INFO.SYSTEM.LISTEN.START.TEST.+.SUBJECT.<

   These strings will be interpreted as encoded wildcards subjects:

     - TEST.> as TEST.<
     - TEST.*.SUBJECT as TEST.+.SUBJECT
     - TEST.* as TEST.+
     - *.TEST as +.TEST

## Installing the NATS RV Bridge

Binary / RPM installation will come with a release (not ready yet).  Compiling
can be done by cloning with the submodules and making, using CentOS 7 or later.
Ubuntu and Debian had also been known to work but the primary dev work is done
under CentOS 8.

```console
$ git clone https://github.com/raitechnology/natsrv
$ cd natsrv
$ git submodule update --init --recursive
# need one of: dnf_depend, yum_depend, apt_depend
$ make dnf_depend
sudo dnf -y install make gcc-c++ git redhat-lsb openssl-devel pcre2-devel chrpath
Last metadata expiration check: 0:47:09 ago on Mon 15 Feb 2021 08:53:17 PM PST.
Package make-1:4.2.1-10.el8.x86_64 is already installed.
Package gcc-c++-8.3.1-5.1.el8.x86_64 is already installed.
Package git-2.27.0-1.el8.x86_64 is already installed.
Package redhat-lsb-4.1-47.el8.x86_64 is already installed.
Package openssl-devel-1:1.1.1g-12.el8_3.x86_64 is already installed.
Package pcre2-devel-10.32-2.el8.x86_64 is already installed.
Package chrpath-0.16-7.el8.x86_64 is already installed.
$ make
# lots of output, the final binary will be in a build directory
$ RH8_x86_64/bin/natsrv_server -help
```
