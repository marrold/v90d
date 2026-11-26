# v90d/cfg: configuration, status and settings over HTTP

Four small pieces for the telephony daemon, each working on caller-owned
structs with no allocation, no globals and no floating point:

| File | What |
|---|---|
| `config.h`, `config.c` | configuration file format, strict loader, canonical writer, atomic save |
| `status.h`, `status.c` | the status struct the daemon fills in, HTML and JSON renderers |
| `settings.h`, `settings.c` | the settings form: HTML page and decoder of the posted form |
| `httpd.h`, `httpd.c` | non-blocking HTTP/1.0 server for the status views and the settings form |
| `sbuf.h` | bounded string builder used by all four (internal) |
| `v90d.conf.example` | the example file below, loaded by the tests |

Portable C99 and POSIX sockets, written from the RFCs (1945 for HTTP, 7617
for Basic authentication, 8259 for JSON). `make test` runs the host tests, `make test-arm` the same tests
cross-built for the ARM926 target under qemu, `make clean` removes `build/`.

## Configuration file

Plain ASCII, line oriented:

- `# comment` or `; comment`: a whole line. There are no trailing comments,
  everything after the `=` is the value.
- `[section]` opens one of `network`, `ppp`, `modem`, `line`, `status`,
  `log`.
- `key = value` sets a key of the open section. Blanks around names and
  values are ignored.
- Every key may appear once, except `user` (up to 16 times).
- Lines are at most 640 characters, the file at most 16384 bytes, line ends
  are LF or CR LF.

The loader is strict: an unknown section or key, a repeated key, a malformed
or out-of-range value, a byte outside printable ASCII, all stop the load with
a message of the form `file:line: [section] key: what is wrong`, for example

    /etc/v90d.conf:12: [modem] max_downstream: 99999 is out of range 300..56000
    /etc/v90d.conf:7: [ppp] auth: chap needs at least one user
    /etc/v90d.conf:9: [ppp] user: name 'alice' already defined on line 8
    /etc/v90d.conf:3: unknown key 'adress' in [network]

After a failed load the struct holds the defaults, never half a file. A file
that does not exist is not an error: `cfg_load` returns `CFG_DEFAULTS` and
the defaults apply. Whether to run on defaults after a real error is the
daemon's decision.

### Keys

| Section | Key | Values | Default |
|---|---|---|---|
| network | `lan` | `dhcp`, `static` | `dhcp` |
| network | `address` | LAN address of the box (static mode) | `192.168.1.2` |
| network | `netmask` | contiguous, /1 to /30 (static mode) | `255.255.255.0` |
| network | `ppp_local` | our end of the PPP link | `10.64.64.1` |
| network | `ppp_peer` | address handed to the dial-in peer | `10.64.64.2` |
| network | `dns1`, `dns2` | DNS servers offered in IPCP, or `none` | `9.9.9.9`, `1.1.1.1` |
| network | `nat` | `on`, `off` | `on` |
| ppp | `auth` | `none`, `pap`, `chap` | `none` |
| ppp | `mru` | 68..1500 | `1500` |
| ppp | `echo_interval` | seconds, 0..3600, 0 = off | `30` |
| ppp | `compression` | `on`, `off`: PPP compression (CCP) with callers that ask for it, Deflate (RFC 1979, Linux pppd) or MPPC (RFC 2118, Windows); independent of `v42bis` and useful together with it | `on` |
| ppp | `encryption` | `on`, `off`: require 128-bit MPPE (RFC 3078) on port 1. `on` forces MS-CHAPv2 (so a `user` is needed), drops plain compression, and refuses a caller that will not encrypt | `off` |
| ppp | `encryption2` | the same for port 2. Encryption is set per port (unlike the rest of `[ppp]`); the `user` list is shared | `off` |
| ppp | `user` | `NAME SECRET`, up to 16 lines | none |
| modem | `modulations` | comma list out of `v90`, `v34`, `v32bis`, `v22bis`, `v22`, `v21`, `v23` (1200/75 bit/s asymmetric FSK, untried against real hardware) | all but `v23` |
| modem | `max_downstream` | bit/s, 300..56000 | `56000` |
| modem | `max_upstream` | bit/s, 300..48000; above 33600 is reached only with V.92 (`v92 = on`) | `33600` |
| modem | `tx_level` | dBm0, -30..-6 | `-12` |
| modem | `v42` | `on`, `off` | `on` |
| modem | `v42bis` | `on`, `off` (on needs `v42 = on`) | `on` |
| modem | `v92` | `on`, `off`: offer V.92 PCM upstream to callers whose modem asks for it (has an effect only with `v90` in `modulations` and `v42 = on`; the upstream goes above 33333 bit/s only with `max_upstream` raised, up to 48000; a caller that does not ask gets V.90) | `on` |
| modem | `rings` | rings of ringback before we answer, 1..10 | `1` |
| modem | `law` | `ulaw`, `alaw` | `ulaw` |
| line | `number` | the number callers must dial: 1..20 characters from `0-9 * #`, or `any` | `any` |
| line | `tones` | `us`, `eu`: dial tone, ringback and busy cadences | `us` |
| line | `busy_chance` | 0..100, percent of calls that get a busy signal instead of an answer | `0` |
| line | `drop_mean` | 0..10080, mean minutes of connect time before the line drops at random, 0 = never | `0` |
| line | `session_limit` | 0..10080 minutes, hard limit of one call, 0 = none | `0` |
| line | `idle_timeout` | 0..10080 minutes without IP traffic before we hang up, 0 = none | `0` |
| line | `service` | `internet`, `archive`, `dreamcast`, `saturn`: what a caller on this port gets (below) | `internet` |
| line | `dreamcast_now` | `on`, `off`: with `service = dreamcast`, report the game being played to the Dreamcast Now site | `off` |
| line | `dreamcast_lan` | `on`, `off`: with `service = dreamcast`, the console is a host on the network with an address of its own (DHCP) | `off` |
| line | `dreamcast_upnp` | `on`, `off`: with `service = dreamcast`, ask the network's router to pass the games' ports on (UPnP) | `off` |
| line | `saturn_matchmaking` | `on`, `off`: with `service = saturn`, pair players through saturn.dreampipe.net (off = direct only) | `off` |
| status | `listen` | `lan`, `off` | `lan` |
| status | `port` | 1..65535 | `80` |
| status | `admin_password` | 0..64 octets, written like a user secret; empty = settings page open | empty |
| log | `level` | `error`, `warn`, `info`, `debug` | `info` |

Numbers are plain decimal without leading zeros; addresses are dotted quads
without leading zeros. Addresses must be usable unicast host addresses; in
static mode the LAN address may not be the network or broadcast address of
its netmask, and the three addresses must differ where that matters.

User names (1 to 63 octets) and secrets (0 to 64 octets) are bare words
(printable ASCII without blanks, `"` or `\`) or double-quoted strings. Inside
quotes the escapes are `\"`, `\\` and `\xHH` (two hex digits, not `00`), so
any octet except NUL can be written. Names must be unique. `auth = pap` or
`chap` needs at least one user.

`admin_password` is one such word. It is the only key that may have nothing
after the `=`, which is the empty password, the same as `""`. While it is
empty, anyone who reaches the server can change the settings; otherwise the
settings page asks for the user name `admin` and this password.

`service` is what a caller on the port gets, and each port has its own:

- `internet`: the Internet as it is, through the box's NAT.
- `archive`: the caller's web (HTTP), FTP and name questions go to the
  gateway on the box (`arch/`), which fetches them from Protoweb, the web as
  it was. Mail, chat and secure sites do not work for that port.
- `dreamcast`: for a Sega Dreamcast. The console dials the port's `number`
  and logs in as one of the `user`s, like any caller. PPP compression and
  V.92 are not offered, whatever `compression` and `v92` say; the console
  is given the name servers of the game revival projects (46.101.91.123 and
  209.50.50.129) instead of `dns1` and `dns2`; and its packets leave with a
  hop count of 64, an opening of a connection from the ports 3200 to 3205
  that is repeated within a second being passed on once (`srv/console.h`).
  Connections of the games to servers that are gone are turned to the ones
  that stand in for them today (a list fetched when the service starts),
  the ports the games need are let in from the network to the console, and
  an opening of a connection that carries the push flag is mended. Three
  switches, each off unless asked for:
  - `dreamcast_now`: every 15 seconds while the console is connected, the
    site dcnow-2016.appspot.com is told a hash of the box's hardware address
    and a hash of the last name the console looked up, which is how its
    "who is playing" page works. Nothing is sent with the switch off.
  - `dreamcast_lan`: the box asks the network's DHCP server for a second
    address and gives it to the console, answering for it on the network
    (proxy ARP); the console is then not behind the box's NAT. The address
    is renewed at half its lease and given up when the lease runs out.
  - `dreamcast_upnp`: the network's router is asked to pass the games'
    ports on (to the console with `dreamcast_lan`, else to the box), again
    every half hour, and to stop when the daemon ends. Only the router's own
    answer to the search is believed.
  The facts behind it are in `refs/dreampi-facts.md`. It has not been tried
  with a real console.
- `saturn`: for two consoles that would telephone each other (the Sega
  Saturn with its NetLink modem). Each player dials into a box of their
  own, and the boxes carry the octets of the two modems to each other over
  the network, in the words of the tunnel the DreamPi project uses, so the
  other player may have one of those. What is dialled is an instruction:
  `0` or `00` waits for the other player (two minutes), `#a*b*c*d#` or `#`
  and twelve digits and `#` calls the player at that IPv4 address. The
  port's `number`, the users and the PPP settings play no part; error
  control is not offered. The waiting side must be reachable on TCP port
  65432 and UDP port 20001, the calling side on UDP port 20002. The numbers
  that ask a matchmaking service for an opponent are not understood. Not
  tried with a real console (`srv/saturn.h`).

Settings written before the ports had a service each may hold `archive = on`
in `[network]`. That still reads, and means `service = archive` for every
port whose section does not name a service; it is not written back.

In the struct the number `any` is the empty string: `line_number[0] == 0`
means every number connects. What the daemon does with a wrong number, a
busy draw or a drop is its business; this code stores and checks the values.

### Example

This is `v90d.conf.example`:

```
# v90d example configuration. Every key is shown; a key left out keeps the
# default, which is the value shown here except where noted.

[network]
# dhcp (default) or static. address and netmask are used in static mode.
lan = static
address = 192.168.1.2
netmask = 255.255.255.0
gateway = none
# our end of the PPP link, and the address handed to the caller
ppp_local = 10.64.64.1
ppp_peer = 10.64.64.2
# DNS servers offered to the caller; `none` to leave one out
dns1 = 9.9.9.9
dns2 = 1.1.1.1
# masquerade the caller behind the LAN address
nat = on

[ppp]
# none (default), pap or chap. pap and chap need at least one user.
auth = chap
mru = 1500
# seconds between LCP echo requests, 0 = off
echo_interval = 30
# PPP compression (CCP) with callers that ask for it: Deflate (Linux pppd)
# or MPPC (Windows). Works on top of v42bis; off = never negotiated.
compression = on
# require 128-bit MPPE encryption, set per port. on = force MS-CHAPv2 (a user
# is needed), drop plain compression, refuse a caller that will not do MPPE.
encryption = off
encryption2 = off
# up to 16 users: name, then secret. Quote anything with blanks; inside
# quotes use \" \\ and \xHH.
user = alice wonderland
user = "bob smith" "correct horse \"battery\" staple"

[modem]
modulations = v90, v34, v32bis, v22bis, v22, v21
max_downstream = 56000
max_upstream = 33600
# transmit level in dBm0
tx_level = -12
v42 = on
v42bis = on
# on: offer V.92 PCM upstream to V.92 callers (then max_upstream may go up
# to 48000). off (default): V.90, upstream is V.34.
v92 = on
# rings before answer
rings = 1
# ulaw (default) or alaw
law = ulaw

[line]
# the number callers must dial: 1 to 20 characters out of 0-9 * #, or `any`
# (default): every number connects.
number = 6387825442
# us (default) or eu: dial tone, ringback and busy cadences
tones = us
# percent of calls that get a busy signal instead of an answer
busy_chance = 0
# mean minutes of connect time before the line drops at random, 0 = never
drop_mean = 0
# hard limit of one call in minutes, 0 = none
session_limit = 0
# minutes without IP traffic before we hang up, 0 = none
idle_timeout = 0
# what a caller on this port gets: internet (as it is), archive (web and
# FTP from Protoweb, the web as it was), dreamcast (for the game console:
# no PPP compression, no V.92, the game revival name servers) or saturn
# (two consoles that telephone each other, joined over the network: dial 0
# to wait, #a*b*c*d# to call the player at that address). The last two have
# not been tried with a real console.
service = internet
# for service = dreamcast, all off unless asked for: tell the "Dreamcast
# Now" site which game is being played; make the console a host of its own
# on the network (an address by DHCP) instead of hiding it behind the box;
# ask the network's router to pass the games' ports on (UPnP); keep an
# OpenVPN tunnel up to the network the IWANGO games' servers are in (needs
# a profile, given on the Dreamcast page)
#dreamcast_now = off
#dreamcast_lan = off
#dreamcast_upnp = off
#dreamcast_iwango = off

[status]
# lan (default) or off
listen = lan
port = 80
# password of the settings page (user name `admin`), quoted like a user
# secret. Empty: anyone on the LAN can change the settings.
admin_password = ""

[log]
# error, warn, info (default) or debug
level = info
```

### Writing the config back

`cfg_write(c, with_secrets, buf, cap)` prints every key in a fixed order.
With secrets the text loads back to an identical struct (the tests check the
round trip and that writing is a fixed point). Without, every secret is
printed as the bare word `<redacted>`, which the loader refuses, so a
redacted dump cannot be mistaken for a config file. The admin password is
redacted the same way when one is set; an empty one is printed as `""`, so
the dump tells whether the settings page is protected. `GET /config` serves
the redacted form.

`cfg_save(c, path, err, errsz)` stores a config so that a crash or power cut
leaves either the old file or the new one: the text with secrets is written
to `path.tmp` (mode 0600), synced with `fsync`, and renamed over `path`. A
config that does not pass `cfg_validate` is not written. On any failure the
temporary is removed, `path` is untouched and `err` says what went wrong.
The new file is mode 0600 whatever the old one was. The directory itself is
not synced: after a power cut right after the call the file is the old one
or the new one, never a torn one.

## API

```c
/* config.h */
void cfg_default(cfg_t *c);
int  cfg_validate(const cfg_t *c, char *err, size_t errsz);
int  cfg_parse(cfg_t *c, const char *name, const char *text, size_t len,
               char *err, size_t errsz);
int  cfg_load(cfg_t *c, const char *path, char *err, size_t errsz);
int  cfg_write(const cfg_t *c, int with_secrets, char *buf, size_t cap);
int  cfg_save(const cfg_t *c, const char *path, char *err, size_t errsz);

/* status.h */
void status_init(status_t *s);
void status_add_call(status_t *s, const st_call_t *call);
const st_call_t *status_call(const status_t *s, unsigned i);
int  status_render_html(const status_t *s, char *buf, size_t cap);
int  status_render_json(const status_t *s, char *buf, size_t cap);

/* settings.h */
int  settings_render_html(const cfg_t *c, int saved, const char *error,
                          char *buf, size_t cap);
int  settings_apply(const cfg_t *cur, const char *body, size_t len, cfg_t *out,
                    char *err, size_t errsz);

/* httpd.h */
void httpd_init(httpd_t *h, cfg_t *cfg, const status_t *status);
void httpd_on_save(httpd_t *h, int (*save)(void *ctx, const cfg_t *newcfg),
                   void *ctx);
int  httpd_listen(httpd_t *h, uint32_t bind_addr, uint16_t port, const char *ifname);
int  httpd_adopt(httpd_t *h, int fd, uint32_t now_ms);
void httpd_poll(httpd_t *h, uint32_t now_ms);
int  httpd_active(const httpd_t *h);
void httpd_close(httpd_t *h);
```

`cfg_parse`, `cfg_load`, `cfg_validate` and `cfg_save` return `CFG_OK` (0)
or `CFG_ERR` (-1); `cfg_load` also `CFG_DEFAULTS` (1). The writers and
renderers return the text length, or -1 if the buffer is too small; they
never write past `cap` and always NUL terminate. `CFG_TEXT_MAX`,
`CFG_TEXT_REDACTED_MAX`, `STATUS_HTML_MAX`, `STATUS_JSON_MAX` and
`SETTINGS_HTML_MAX` are sufficient buffer sizes for any content.
`settings_apply` returns `SETTINGS_OK` (0), `SETTINGS_INVALID` (1) or
`SETTINGS_BAD` (-1). IPv4 addresses in the structs are in host byte order.

Use in the daemon:

```c
static cfg_t cfg; static status_t st; static httpd_t web;
char err[CFG_ERR_MAX];

/* the settings form was posted: newcfg is valid, cfg is still the old one */
static int save(void *ctx, const cfg_t *newcfg)
{
    char err[CFG_ERR_MAX];
    if (cfg_save(newcfg, "/etc/v90d.conf", err, sizeof err) != CFG_OK) {
        log_error(err);
        return -1;                         /* the browser gets a 500 */
    }
    /* ... apply what differs between cfg and *newcfg ... */
    return 0;                              /* httpd now copies *newcfg into cfg */
}

if (cfg_load(&cfg, "/etc/v90d.conf", err, sizeof err) == CFG_ERR)
    die(err);                              /* or log it and run on defaults */
status_init(&st);
httpd_init(&web, &cfg, &st);
httpd_on_save(&web, save, NULL);
if (cfg.status_listen == CFG_LISTEN_LAN)
    httpd_listen(&web, lan_address, (uint16_t)cfg.status_port, "eth0");

for (;;) {
    /* ... modem ticks, PPP, keep st up to date ... */
    if (ten_ms_passed) httpd_poll(&web, now_ms);
}
```

`cfg_load` blocks on the file and belongs before the sample clock starts.
`httpd_poll` never blocks by itself; with nothing to do it is one `poll()`
system call with a zero timeout, so call it every 10 to 20 ms, not per
sample. The save callback runs inside `httpd_poll`, and `cfg_save` in it
blocks for a write, an `fsync` and a rename. A daemon that cannot afford
that while a call is up can copy `*newcfg` in the callback, return 0 and
store it when the line is idle.

`httpd_init` takes the config as `cfg_t *`, not `const`: after the callback
has returned 0 the server copies the new config into that struct, so the
form, `/config` and the daemon all see the new values. That copy is the only
write the server ever makes to it. Without a callback the form is shown but
cannot be posted (403).

## Two ports

The box has two telephone ports. Port 1 is configured by `[line]` and
`[modem]`, port 2 by `[line2]` and `[modem2]`, with the same keys, value
ranges and defaults (so each port has its own `number`, tones, rings, limits,
modulations, rate ceilings and `v92` switch). `[network] ppp_peer2` (default `10.64.64.3`)
is the address handed to the caller on port 2, and `[ppp] encryption2` is
port 2's MPPE requirement. Network, the rest of PPP (auth, mru, the user
list) status and log settings are shared. `modulations` takes, fastest
first: `v90, v34, v32bis, v22bis, v22, v21, v23`.

In C, `cfg_t` holds port 1 in its old fields and port 2 in `port2`;
`cfg_for_port(c, 2, &view)` gives a `cfg_t` whose line and modem fields are
port 2's (applying it twice gives the original back).

Pages: `/line` and `/modem` for port 1, `/line2` and `/modem2` for port 2
(the forms carry a hidden `port=2`), with a Port 1 / Port 2 switch above the
form. The dashboard `/` shows both ports' state side by side and the tables
of port 1; `/port2` shows the tables of port 2. `/status.json` is port 1,
`/status2.json` port 2 (404 when the daemon serves one line only).

`/calls` (the Calls tab) is the call history of the box: the newest 20
finished calls of both ports in one table, newest first, each row saying
the port the call came in on, then when it began, how long it lasted, what
was dialed, the result, the modulation, the two rates and how it ended.
Every port keeps its last 20 calls (`STATUS_MAX_CALLS`); the history is in
memory only, so a restart of the box empties it.

## Status views

- `GET /` is a page of three tables (line, PPP, counters; the calls are on `/calls`) with
  the menu of the settings pages. Nothing from elsewhere (the box's one
  script, /ui.js, and inline style), a `meta refresh` every 5 seconds.
- `GET /status.json` is one object: `uptime_s`, `service`, `v92`,
  `ppp_compression` and `ppp_encryption` (the settings `service` of the port,
  `v92`, `[ppp] compression` and `[ppp] encryption`,
  `true` or `false`), `line`, `modulation` (`none`,
  `v90`, `v34`, `v32bis`, `v22bis`, `v22`, `v21`, `v23` or `v92`),
  `down_bps`, `up_bps`, `rtd_ms`, `rx_level_dbm0`, `snr_db`, `v42`,
  `compression` (the modem's: V.42bis or V.44 in use), `ppp` (`phase`,
  `peer_name`, `peer_ip`, `local_ip`, and for PPP compression in this call
  `comp_tx` and `comp_rx`: `none`, `deflate`, `mppc` or `mppe` (encryption)
  towards and from the
  caller, `comp_tx_in` / `comp_tx_out`: octets of IP given to the compressor
  and octets sent for them, `comp_rx_in` / `comp_rx_out`: octets received
  compressed and the IP in them),
  `counters` (`rx_octets`, `tx_octets`, `rx_packets`, `tx_packets`,
  `rx_errors`, `tx_errors`), `calls_total` and `calls`, newest first, each
  with `start` (ISO 8601 UTC or `null` when the clock was not set),
  `start_unix`, `duration_s`, `dialed`, `tel_result`, `modulation`,
  `down_bps`, `up_bps`, `end`.
- `GET /config` is the redacted canonical config as plain text.

A call record (`st_call_t`) carries, next to what the modem and PPP made of
the call, how it went at the telephone level: `dialed`, the digits the
caller dialed (up to 20, empty if not known), and `tel_result`:

| `tel_result` | JSON | HTML | Meaning |
|---|---|---|---|
| `ST_TEL_ANSWERED` (0) | `answered` | answered | we answered; `end` tells the rest |
| `ST_TEL_BUSY` | `busy` | busy | got the busy signal (`busy_chance`) |
| `ST_TEL_WRONG_NUMBER` | `wrong_number` | wrong number | dialed something else than `number` |
| `ST_TEL_ABANDONED` | `abandoned` | abandoned | caller hung up before we answered |
| `ST_TEL_DROPPED` | `dropped` | line dropped | simulated line drop (`drop_mean`) |
| `ST_TEL_SESSION_LIMIT` | `session_limit` | session limit | `session_limit` reached |
| `ST_TEL_IDLE` | `idle` | idle timeout | `idle_timeout` reached |

The daemon fills both in and also adds calls that were never answered; the
counter on the page ("Calls") counts every `status_add_call`.

Both renderers emit pure ASCII whatever the struct holds. The peer name and
the dialed digits come from the caller: in HTML `& < > " '` become entities
and anything outside printable ASCII is shown as `\xHH`; in JSON it becomes
`\"`, `\\` or `\u00HH` (also for `< > &`). Enum fields outside their range
print as `unknown`, a corrupt call ring is clamped.

The line settings (number, busy chance, drop mean) are not repeated in the
status views: the renderers see only `status_t`. They are in `/config` and
on the settings page.

## Settings form

`GET /settings` is one plain HTML form in the look of the status page:
tables of text inputs, a few selects, checkboxes and a Save button. No
script, no external resource, no automatic refresh; it works in a browser
of the dial-up era as well as in a current one. It covers

- the telephone line: phone number, tones, rings before answer, busy
  chance, line drop mean, session limit, idle timeout;
- the modem: modulations (one checkbox each), max downstream and upstream,
  and the V.92 switch (off, or on: PCM upstream for callers whose modem
  asks for it, for example `AT+PIG=0` on Conexant modems; a max upstream
  above 33600 lets it go faster);
- PPP login: authentication mode and the user table, 16 rows of name and
  password;
- the admin password of the page itself, and a checkbox to remove it.

Everything else in the config (addresses, MRU, V.42, the status port ...)
is not on the form and is carried over unchanged.

Passwords are never sent to the browser, not the users' and not the admin
password; the password inputs are always empty. Therefore:

- an empty password next to the name of a current user keeps that user's
  password (wherever the row has moved), a typed one replaces it;
- a name that is not a current user needs a password. An empty secret for
  a new user can only be set in the config file. This is deliberate: a
  rename with the field left empty would otherwise make an account with no
  password;
- a row with an empty name is dropped and the rows below close up; a
  password in a row without a name is an error;
- an empty admin password field keeps the admin password.

User names are octet strings. A name that is valid UTF-8 without control
characters is put into its input as it is, and the browser returns the
same octets. Any other name (Latin-1, control octets) would come back
changed, so its row shows the name as text with `\xHH` and has no name
input: such a user can get a new password here but is renamed or removed
in the config file only.

`POST /settings` takes the form back as
`application/x-www-form-urlencoded`. A field that is absent keeps its
current value, so `curl -d busy_chance=50 http://box/settings` works. The
field names are the config keys (`number`, `tones`, `rings`, `busy_chance`,
`drop_mean`, `session_limit`, `idle_timeout`, `auth`, `max_downstream`,
`max_upstream`, `v92`, `compression`, `encryption`, `admin_password`), plus `user0`..`user15`, `pass0`..`pass15`,
`mod_v90`, `mod_v34`, `mod_v32bis`, `mod_v22bis`, `mod_v22`, `mod_v21`, `mod_v23` (1 or 0), `mods` (1: the
`mod_` fields present are the whole set, as with the browser's checkboxes)
and `admin_clear` (1: remove the admin password). The server decodes the
body into a candidate built from the current config and runs
`cfg_validate` on it:

| Outcome | Answer |
|---|---|
| saved: the callback returned 0 | 303 to `/settings?saved=1`, which shows "Settings saved." |
| a value is wrong (`rings=0`, a new user without password, `chap` without users ...) | 400 with the form again, the message of the config loader, and the values as posted |
| the body is malformed: bad percent escape, NUL octet, a field twice, an unknown field, a pair without `=` | 400, plain text |
| the callback returned nonzero | 500, plain text |

Nothing is changed unless the answer is 303. Only the first problem is
reported. A number field that is not a number shows its old value again.

All values are HTML-escaped where they are rendered, the error message
included.

## HTTP server behaviour

| Request | Answer |
|---|---|
| `GET` or `HEAD` on `/`, `/calls`, `/status.json`, `/config` (query string ignored) | 200 |
| `GET` or `HEAD` on `/settings` | 200, or 401 (below) |
| `POST` on `/settings` | 303, or one of the refusals below |
| any view, an admin password is set, no session cookie and no `Authorization` header | 303 to `/login` for the pages, 401 for `/status.json` and for posts |
| `POST /login` with the user name `admin` and the admin password | 303 to `/` with the session cookie; wrong: 401 with the page again, then 429 for a second |
| `GET /logout` | 303 to `/`, the session of that cookie is over |
| `POST /restart` (authorised, same origin) | 200, and `restart_asked` is set for the owner of the server |
| `GET` or `HEAD` on `/cgi-bin/login` | 302 to `/` (the stock firmware of the HT802 sent browsers there for good, and browsers remember) |
| `GET` or `HEAD` on any other path | 404 |
| any other method, and `POST` anywhere else | 405 with `Allow: GET, HEAD` (`GET, HEAD, POST` on `/settings`) |
| malformed request line, control or NUL byte in the head, target not starting with `/` | 400 |
| head larger than 2048 bytes | 431 |
| request (head and body) not complete 5 s after the connection was taken | 408 |
| fifth simultaneous connection | 503, closed at once |

Refusals of a `POST /settings`, in the order they are checked:

| Condition | Answer |
|---|---|
| an admin password is set and the request lacks the right credentials | 401 with `WWW-Authenticate: Basic realm="v90d settings"` |
| no save callback registered | 403 |
| an `Origin` header that does not name the host in `Host` | 403 |
| `Transfer-Encoding` present; `Content-Type` or `Content-Length` twice | 400 |
| `Content-Type` is not `application/x-www-form-urlencoded` (parameters allowed) | 415 |
| no `Content-Length` | 411 |
| `Content-Length` not a plain number | 400 |
| `Content-Length` above 8192 | 413, at once, the body is not read |
| malformed body, unacceptable value, failed save | 400, 400, 500 as in the table above |

- HTTP/1.0 answers with `Content-Length` and `Connection: close`; one
  request per connection, anything after the first request is discarded.
- The path is compared byte for byte with the paths served. It is
  never decoded and never reaches the file system, so there is nothing for
  `..`, `%2e` or NUL tricks to traverse.
- Fixed table of 4 connections, each with a 2048 byte request buffer and a
  response buffer (28 KiB) that holds the whole response, so a slow reader
  costs nothing but its slot, and only until the 10 s send deadline. The
  whole `httpd_t` is about 125 KB.
- The body of a post is collected in the connection's response buffer,
  which is idle until the answer is built; it is decoded into a config on
  the stack before the answer overwrites it. The largest form a browser
  can send (16 names of 63 and passwords of 64 octets, every octet
  percent-encoded) is about 6.8 KB.
- The 5 s receive deadline covers the whole request, head and body; it is
  not an idle timer: sending a byte now and then does not extend it.
- After the response the server shuts down its side and discards input
  until the client closes or 1 s has passed, so the close does not reset
  the connection under an unread response.
- Responses carry `Cache-Control: no-store`,
  `X-Content-Type-Options: nosniff` and a `Content-Security-Policy` that
  allows nothing but the page's inline style, forms that post to the server
  itself, and no framing by other pages.
- Request headers are matched by name in any case. A header the server
  acts on (`Authorization`, `Origin`, `Host`, `Content-Type`,
  `Content-Length`) must not appear twice.

### Who may change the settings

- With `admin_password` empty, anyone who can reach the server. That is
  the default, as on a box fresh from the factory; set a password on the
  form.
- With a password set, `/settings` needs HTTP Basic authentication (RFC
  7617), user name `admin`. Credentials are compared in constant time. The
  status page, the JSON and `/config` stay open. There is no limit on
  attempts. Basic credentials, like the form itself, cross the LAN
  unencrypted.
- After the admin password was changed through the form, the browser asks
  for the new one on the next page.
- Cross-site requests: a page on another site can make a browser post a
  form here. Browsers that do this send an `Origin` header, and a post
  whose `Origin` does not name the host it was sent to is refused. A
  request without `Origin` (old browsers, `curl`) is accepted; old browsers
  are covered only by the admin password.

### Keeping it on the LAN

Binding to the LAN address is not enough to keep the dial-in peer out:
Linux accepts a packet for any local address on any interface, so the peer
can reach `lan_address:port` through the PPP link. `httpd_listen` therefore
takes an interface name and sets `SO_BINDTODEVICE` where the platform has
it. A firewall rule on the PPP interface does the same job.

DNS rebinding (a page on another site whose name has been pointed at the
box's address) makes `Origin` and `Host` agree on that foreign name, so the
`Origin` check alone would let such a page read and post the settings. The
rule against it: while no admin password is set, `/settings` and `/config`
answer only when `Host` is a dotted address with an optional port, or when
there is no `Host` at all; anything else gets 403 and the hint to open the
page by address. With an admin password a name is accepted, since the
browser has no credentials for a rebound name. The status page and
`/status.json` stay readable either way; no secret is ever served.

## Tests

`make test` and `make test-arm` run:

- `test_config`: defaults, the full grammar, about 120 malformed files each
  checked for the right line number and message, limits (line length, file
  size, table size, name, secret and number length), write and reload round
  trips, redaction, the writer at every buffer size from 0 up, and
  `cfg_save` in a temporary directory (content, mode, stale temporary,
  invalid config, unwritable targets, nothing left behind).
- `test_status`: content of both views, a JSON syntax checker run over all
  output, hostile peer names and dialed digits (markup, quote breaking,
  every octet value, a missing NUL), out-of-range enums, the call ring with
  every telephone result, dates up to 2106, and both renderers at every
  buffer size from 0 up.
- `test_settings`: the page (every field, no secret and no password value
  in it, escaping, UTF-8 and non-UTF-8 names, out-of-range structs, every
  buffer size from 0 up), the decoder (every field, partial posts, the
  password rules, about 30 unacceptable values with their messages, about
  45 malformed bodies), a form-submitting stand-in for a browser that
  posts the rendered page back and must get the identical config, the
  largest possible post, and 5000 random bodies.
- `test_httpd`: the server driven through `socketpair()` ends with a clock
  that the test advances and that wraps during the run: the views,
  traversal and NUL paths, malformed request lines, other methods, every
  truncation and every split of a request, silent and slow senders,
  oversized heads, pipelined requests and garbage, table exhaustion,
  a reader that stalls (deterministic: the send buffer is filled before the
  server gets the socket), worst-case bodies, two instances, 3000 random
  and mutated requests; for the settings form: save and redirect, the
  callback's view of old and new config, refused values and bodies, a
  failing callback, the header checks, the size limit, a post split at
  every octet or cut short or stalled, four posts interleaved, same-site
  and cross-site `Origin`, right, wrong and malformed credentials, a
  password change through the form, and 4000 mutated posts; and a count of
  open descriptors at the end.

Not covered by the tests:

- `httpd_listen` and the `accept` branch of `httpd_poll`, because the test
  environment cannot listen on a socket. They are compiled for both targets
  but have not been run.
- A real browser. The form was checked by a stand-in that reads the
  rendered HTML and posts it as a browser would, and with a request head
  shaped like a current browser's; no actual browser, old or new, has
  loaded the page.
- That the comparison of credentials is constant-time: this is by
  construction (fixed-size buffers, no early exit), not by measurement.
- `fsync` durability in `cfg_save` across a real power cut, and a failing
  `write` (full disk).
