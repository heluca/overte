# directory-probe

Checks which places in the directory (metaverse server) actually answer, as opposed to merely
checking in. A domain can accept the connection and send its settings while none of its
assignment clients (entity server, audio/avatar/messages mixers, asset server) reply to the
hole-punch pings. The client then shows an empty sky with a horizon. Nothing on an assignment
client answers an unauthenticated packet, so the only reliable probe is the real client: this
script drives one Interface session through each place and reads the verdict from its log.

    tools/directory-probe/probe-directory.py --list            # directory with heartbeat ages
    tools/directory-probe/probe-directory.py --live            # probe everything that checked in recently
    tools/directory-probe/probe-directory.py anime-avatars     # probe named places
    tools/directory-probe/probe-directory.py --live --json

Verdicts:

| verdict | meaning |
|---|---|
| `OK` | entity server connected, content arrives |
| `ASSIGNMENT-CLIENTS-UNREACHABLE` | domain server answered, no assignment client replied to pings (server NAT/firewall) |
| `NO-ENTITY-SERVER` | connected, sockets activated, but no entity server (domain misconfigured) |
| `NO-DOMAIN-SERVER` | the domain server itself never answered |
| `NO-RESULT` | the client never reported (crash, timeout) |

Interface must not already be running: a second instance only hands its URL to the running one.
Set `OVERTE_BIN` or pass `--bin` for a non-default binary. Each place gets `--dwell` seconds
(default 20); the client gives up on silent assignment clients after about 15.
