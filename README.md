# UDP reverse tunnel

## What kind of problem does this solve?

### Background story

I have a wireguard server running at home to be able to enter my home network from the outside. Wireguard is using UDP. Unfortunately I can not just open an IPv4 UDP port in my fritz box like it used to work in the old days because my provider has put me behind CG-NAT, I have to use IPv6 but IPv6 is not yet usable in my workplace.

So at home I have IPv6 and at work I have IPv4 and no way to reach my home network.

I also have a VPS with a public IPv4 to which I could establish reverse ssh tunnels for TCP traffic. Unfortunately the ssh tunnels only support TCP and one should not do VPN connections over TCP anyways, which is the reason wireguard is using only UDP.

### Problem

````
VPN-Server                                 VPN-Client
  (UDP)         |        |                 (IPv4 UDP)
    |           |        | not possible        |
     -----------|--------| <-------------------
                |        |
               NAT     CG-NAT

````

### Solution
````
VPN-Server                                 VPN-Client
  (UDP)                                    (IPv4 UDP)
    |                                          |
    |                                          |
inside agent                              outside agent
   tunnel                                    tunnel
    ||          |         |                    ||
    ||     punch|    punch|                    ||
    | --------->|-------->|-------------------- |
     -----------|<--------|<-------------------- 
                |         |
                |         |
               NAT     CG-NAT
````

The outside agent is running on a VPS with public IPv4

The inside agent will send UDP datagrams to the public IP and port of the outside agent, this will punch holes into both NATs, the outside agent will receive these datagrams and learn from their source address and port how to send datagrams back to the inside agent.

The VPN client can also send UDP to the outside agent and these datagrams will be forwarded to the inside agent and from there to the VPN server, the VPN server can reply to the inside agent, these will be forwarded to the outside agent and from there to the VPN client.

The outside agent will appear as the VPN server to the client and the inside agent will appear as the VPN client to the server.

Multiple client connections are possible because it will open a new socket on the inside agent for every new client connecting on the outside, so to the server it will appear as if multiple clients were running on the inside agent.

## Usage

Compile the code on both machines. A simple call of make will be sufficient because the code is so simple it has no dependencies other than the standard library.

The binary `udp-tunnel` can operate in two modes, depending on the options it will either be the inside agent or the outside agent.

Now as an example consider the following scenario:

* LAN: wireguard.fritz.box on port 1111
* VPS: jumphost.example.com on port 2222

Start the outside agent on your VPS:
````
udp-tunnel -l 2222
````
Start the inside agent somewhere in your LAN
````
udp-tunnel -o jumphost.example.com:2222 -s wireguard.fritz.box:1111
````
At this point you should be able to point your wireguard client to jumphost.example.com:2222 and get a connection immedtately.

## Beware

This code still has a known security issue. There is no authentication yet, an atacker could easily pose as the inside agent and thereby divert the incoming UDP traffic to his own machine. As long as you are using it only for encrypted VPN this will only result in temporary service disruptions without compromise of private data, but if you are planning to send unencrypted UDP over this tunnel you should be aware that your datagrams can be intercepted.

It is planned to use a pre shared secret and HMAC for the keepalive packets, so an attacker could not spoof them anymore. Until then this entire thing should only be regarded as a quick and dirty proof of concept and not be used in production.
