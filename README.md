# v90d: a dial-up server in a Grandstream HT802

v90d is replacement firmware for the Grandstream HT802 analogue telephone
adapter. It turns the box into a small dial-up internet provider: plug an
old computer's modem into one of the two phone ports, dial the number, and
the modem connects (up to V.90 / V.92 speeds) and gets a PPP session that is
routed out of the box's Ethernet port. No phone line, no SIP, no PC in
between.

It also gives bulletin boards their telephone numbers back: dial
**227 835 3422** from a terminal program and the box joins the call to
Telehack on the internet, the way boards were called before there was a
web. More than a thousand boards have a number, and **1 800 BBS DIAL** is
the operator.

It installs from the stock firmware's own upgrade page with one file, and
the stock firmware stays in the box, one click away.

## What it does

- Answers a modem on either phone port: V.92, V.90, V.34, V.32bis, V.22bis,
  V.22, V.21, Bell 212A and Bell 103 (and V.23, off by default), with V.42 error correction and V.42bis / V.44
  compression.
- Can also offer the two pre-V.90 56k schemes to a modem that speaks one:
  x2 (U.S. Robotics, 1997) and K56flex (1996, experimental). Both are
  answering only, off by default, and share one call at a time; see "Modems".
- PPP server with users and passwords, MPPC compression and optional
  MS-CHAPv2 with 128-bit MPPE encryption for Windows callers, NAT to the
  network on the Ethernet port, DNS handed to the
  caller.
- Telephony the way an exchange does it: dial tone, tone or pulse dialling,
  ringing tone, busy, a number per port (North American or European tones).
- A service per port:
  - **Internet**: the plain connection.
  - **Archive**: web names are steered to Protoweb, so period browsers get
    period sites.
  - **Dreamcast**: what a Dreamcast's built-in modem needs: no compression,
    the DNS servers of the Dreamcast Live community, the packet fixes, the
    games' dead server addresses turned to today's, the games' ports let
    in. Optional switches (per port, off unless you turn them on): the
    console as a host of its own on the network (DHCP), port mapping on the
    router by UPnP, reporting to "Dreamcast Now", and a VPN for the IWANGO
    games (below).
  - **Saturn**: two consoles that telephone each other (NetLink), each
    dialling into its own box, joined over the network. Dial `0` to wait,
    `#a*b*c*d#` to call the player at that address. An optional matchmaking
    mode (off by default) pairs players who dial the same game through a
    match server and works through home routers. Speaks the DreamPi
    tunnel's protocol, so the other side can be a DreamPi.

  The two console services are written from the DreamPi project's
  published behaviour and tested against simulations; neither has been
  tried with a real console yet. The IWANGO games (Sega Tetris, Aero
  Dancing, Golf Shiyouyo 2, Daytona USA) reach their servers through an
  OpenVPN tunnel: turn the switch on and paste your own `.ovpn` profile on
  the Consoles page. v90d ships no profile or keys of its own; bring one
  from whoever runs that network. Not done: DreamPi's closed helper programs.
- A telephone number for every bulletin board and **1 800 BBS DIAL**
  (1 800 227 3425), on every port, whatever its service: see below.
- Numbers that connect straight to a service, raw or over telnet: your own
  and Netstalgia's (a game server, a text service): see below.
- A web interface ("Nestaliga") for all of it, in four sections: Status
  (dashboard, calls), Phone (line, modem, users), Services (bulletin boards,
  numbers to services) and System (network, system, firmware), with a
  Consoles page (the
  Dreamcast switches and the Saturn matchmaking switch) when a port uses one
  of those services. The pages follow the box: both phone ports on an HT802,
  a single port on a one-port box.
- A plain-text diagnostics page at **`/log`** (behind the same login): the
  box's recent log and the kernel's, to read or save and attach to a bug
  report, so a board that misbehaves need not be opened for a serial console.
  Nothing leaves the box on its own; the owner chooses to share the file.
- An optional update check (off by default, under System on the Settings
  page): once a day the box asks GitHub whether a newer release exists and,
  if so, offers it on the Firmware page with one-click install. It contacts
  no one until you turn it on, and installs nothing without a click. See
  [FLASHING.md](FLASHING.md).

## 1 800 BBS DIAL

Bulletin boards are still running, hundreds of them, reached over telnet
today instead of a telephone line. The box puts the telephone line back,
and gives every board a number:

- **227 and seven digits** is a board. 227 is BBS on the keys of a
  telephone, the seven digits are the board's name on the same keys, with
  zeros behind a short name: `telehack` is **227 835 3422**, `agency` is
  227 243 6290. Dial it and you are on the board.
- **1 800 227 3425** (1 800 BBS DIAL) is the operator. After "ANSI
  colour? Y/n" you get the BBS directory, the same list as the terminal
  menu's (below), with a `BBS:` line to type at. Type a board's name
  (`telehack`, `20forbeers`, ...) or its number and press Enter, and you
  are put through. In colour the list narrows to what you type as you type
  it; without colour it follows when you press Enter, which also opens a
  board's page when what you typed is only part of a name. The arrow keys
  (or Ctrl-E, Ctrl-X, `<` and `>`) move through the list, Esc clears the
  line, and Esc on an empty line hangs up.

Either with or without a 1 in front, on any phone port of the box, whatever
its service, from anything that can dial: HyperTerminal, Telix, Procomm, a
Commodore or Amiga terminal. The modem connects, with error correction if
the modem has it, and without PPP: the computer needs no TCP/IP at all. The
telnet negotiation stays inside the box; the terminal sees the board, ANSI
and all. When the board logs you off, a board's own number drops the line,
as it always was; through 1 800 BBS DIAL you are back in the directory.

The **BBS** page of the web interface (under **Services**) lists the boards with their numbers,
a hundred to a page, with a search by name or number.
The boards are those of the [Telnet BBS Guide](https://www.telnetbbsguide.com),
more than a thousand of them: a box that uses the numbers fetches the list
once a day and stays current without a firmware release. It comes from
[Netstalgia](https://netstalgia.online), which serves the guide's list
ready for v90d and signs it: a box takes only a list Netstalgia signed.
They go by the login names 2600.network uses for the same boards
(`20forbeers` for "20 For Beers BBS"). Where two names begin alike, the
second board takes the next free number, so look a number up on the page or
with `?` before you put it in a phone book. The list as it was at the release
([lists/bbs.txt](lists/bbs.txt)) is in the image and serves until the
first fetch or when the box has no way out. Only boards of the list can be called. The port's own
number is still the dial-up connection (a ten-digit one also with a 1 in
front, the way Windows dials it).

The idea is that of [2600.network](https://2600.network), which does this
for callers on the real telephone network; here it is one box and your own
telephone line to it.

## 1 776 869 3200: PROTOWEB

**776 869 3200** (PROTOWEB on the keypad, with or without the 1) is the old
web on any port whose service is the plain internet: the port's own dial-up
connection with its login, but every web, FTP and name lookup of that call
goes to the archive gateway (Protoweb's web as it was), as on a port set to
Archive. The port's own number stays the internet as it is. Netstalgia
answers the same number the same way.

## 1 303 494 4774: the time

**303 494 4774** (with or without the 1), the number of NIST's Automated
Computer Time Service, gives the time on any port in ACTS's own format, so
the time-setting programs of the day and the NTP ACTS driver, which come set
to that number, work unchanged against the box. The box answers it while
its clock is set (NTP), at up to 9600 bit/s and on (V.32bis and below, as
ACTS) with no error control or compression: the banner, then a line a
second (each two seconds at 300 bit/s) with the Modified Julian Date, the
date and time in UTC, the US daylight time code and the on-time marker,
sent 45 ms ahead of its second. A program that echoes the marker has the
advance measured for its line, and the marker turns from `*` to `#` once
three measurements agree; `?` gives a short help. The call ends after 55
seconds. The number is fixed. Netstalgia answers the same number the same
way. The marker is within about 5 ms of its time: the box works in 10 ms
blocks.

## Numbers to services

A number can also connect a caller straight to one service on the internet,
with no PPP and no prompt. **Raw** passes what the modems send both ways as
it is, for a game or program with its own protocol: the AOL client of
Neverwinter Nights dials **1 385 596 1775** and reaches the revived server.
**Telnet** says `Calling` and the name, then joins a board or text service.
The call ends when the service hangs up.

Put up to 16 numbers of your own under **Services**, on the **Numbers** page (number, host,
port, raw or telnet, a name); a host on your own network is fine. The box
also answers the numbers in [Netstalgia](https://netstalgia.online)'s
signed list of services, which it fetches once a day (switch it off on the
same tab and it never asks). Your numbers come first. The list as it was
at the release ([lists/services.txt](lists/services.txt)) is in the image.

## The callers' names

A dial-up caller's name server is the box itself (`[network] dns_relay`, on
by default): the box asks the DNS servers of the Network settings, or with
both empty the ones your network gave it, and passes the answers on. It
answers a question for IPv6 addresses (AAAA) itself, with none: the dial-up
link carries IPv4 only, and a caller with an IPv6 stack (Windows XP with
IPv6 installed, on a network that is mostly IPv6) would otherwise be handed
addresses it cannot reach. With it off the callers are given the two
servers to ask themselves, as before. A Dreamcast port keeps its own name
servers, and a call in archive mode keeps the archive's.

## IPv6

The box takes an IPv6 address on its LAN port when your network's router
offers one (Network page, IPv6, on by default), and with it the router's
IPv6 name servers. Its own connections to boards and services then go over
IPv6 or IPv4, whichever the name has and works. The dial-up callers stay on
IPv4 (their name server answers them no IPv6 addresses, above); on a
network with IPv6 alone their names still resolve, through the router's
name servers.

On a network that has no IPv4 at all, the box still carries the callers'
IPv4 out over IPv6: it translates each caller's packets to IPv6 (through
your network's NAT64) and back, so an old machine with no IPv6 of its own
reaches the IPv4 internet. A site that has only an IPv6 address is reached
too, through a stand-in IPv4 address the box's name server hands out for
it. None of this is set up or seen by the caller, and where the network
has IPv4 the callers' IPv4 goes out as IPv4 as before.

## The terminal menu

With **Terminal menu** on (Services, Numbers page), a caller who dials a
port's Internet number with a terminal program (HyperTerminal, Telix,
minicom) gets Netstalgia's main menu instead of silence: the planet and
the wordmark in ANSI art, "ANSI colour? Y/n" (n: plain ASCII), then a
menu with a highlight bar. **1 Start PPP** hands the same call over to PPP,
**2 Who's online** shows what the box's lines are doing, **3 BBS
directory** lists every board of 1-800-BBS-DIAL, 15 a page, with a search
over names, places, software and descriptions and a page for each board
(location, software, nodes, the login for new users, the guide's
description), and **C** there connects to the board on the same call;
logging off the board comes back to the directory. **G** says goodbye.
Dial-Up Networking callers never see it: their first PPP frame takes them
straight to PPP, also when the program sends something else first (a chat
script's CR after CONNECT); a frame does not while the caller is on a board. The board details come from the Telnet BBS Guide, by way
of a second signed list from Netstalgia; until the box has it, the
directory shows names and numbers. Off by default: a PPP program that waits
for the box to speak first needs it off. The menu is shared with
Netstalgia's own dial-in (`term/`).

## What it is made of

Linux, busybox and a C library are used as they are. Everything else in the
image is written here from the ITU-T recommendations and the RFCs: the board
support and drivers for the HT802's chips, the telephony, the modem itself
(the signal processing of every modulation above, in fixed-point C99), the
link layers, the PPP server, the telnet side of the bulletin board calls,
the web interface, the loader and the build.
The one other outside part is OpenVPN (with mbed TLS), built unchanged from
source for the optional IWANGO tunnel. There is no Grandstream code or
binary in the image and no closed blob.

The modem is [libv90modem](https://github.com/georgica/libv90modem)
(GPL-2.0-only), a library in a repository of its own that v90d builds
against. `fw/setup.sh` fetches a pinned release of it (0.5.0, checked by
SHA-256, like the other dependencies) and unpacks it next to this tree; to
build against a working copy instead, say where it is with `LIBV90M=`
(`libv90m.mk`).

`DESIGN.md` says how it is built and why, `HARDWARE.md` what was learned
about the box, `PROGRESS.md` what was measured and when, with the test
output.

## Status

Tried on one unit: HT802 hardware V2.5A, part number 9610003725A, coming
from stock firmware 1.0.65. The loader refuses to start on a board whose
part number it does not know and hands the box back to the stock firmware,
so an untried revision should end up where it started; that path has been
tested on models only. Three HT801 part numbers are accepted from what their
flash contents say (one phone port, found by the line driver); V1.0A
(`9610003610A`) has been confirmed by its owner. Other HT802 revisions are
not listed. See [BOARDS.md](BOARDS.md).

The **HT802V2** is a different, newer product that runs signed firmware
(stock 1.0.15.x, `SecureStatus: 3`). Its stock upgrader refuses v90d, because
the install relies on the upgrader taking an unsigned image, so the normal
install path is closed. Whether v90d can still be booted another way (its
kernel and userspace written past the upgrader, under the genuine signed
U-Boot) is an open question, not yet decided. See [BOARDS.md](BOARDS.md).

v90d also runs on the **Obihai OBi200** (one phone port), with its own Linux
6.6 kernel on top of the community firmware: a V.92 call at 54 666 bit/s with
PPP and the internet through it has been made on a unit. Its v90d file (`v90d-obi200.fw` in each
release) is installed on the OBi's update page; see
[fw/obi/README.md](fw/obi/README.md).

Tested with three modems:

| modem | what connects |
|---|---|
| Conexant CX93001 (USB) | every modulation in the list; V.90 about 50666 down / 28800 up; V.92 about 50666 / 25333 (`AT+PIG=0`) |
| U.S. Robotics 5686 (external, V.92) | V.90 54666 / 26400; V.92 54666 / 40000 (it asks for PCM upstream by itself) |
| U.S. Robotics 56K of 1998 (x2 with the V.90 upgrade) | V.90 54666 to 56000 / 28800; x2 56000 / 28800 when x2 is offered |

From Linux pppd and from Dial-Up Networking in Windows 98 SE and Windows XP
with MPPC compression: V.90 in both, V.92 in Windows 98 SE with the 5686.
Two calls at once, one on each port, have been made at V.90 and at V.92.

V.92 is offered by default (`v92 = off` to stop it) and used when the
caller's modem asks for PCM upstream; a caller that does not ask gets V.90,
and so does a V.92 call whose start-up does not succeed (with the Conexant
about one call in seven). An upgraded box keeps the setting it had.

The two 56k schemes from before V.90, x2 (U.S. Robotics, 1997) and K56flex
(1996), can be offered to a modem that speaks one. Both are answering only,
need V.34 in the modulation list, are mu-law only, and are off by default;
turn either on per port on the Modem page (`x2 = on`, `k56 = on`). They are
heavy in data mode, so the box serves only one such call at a time across
both ports, whichever scheme it is.

x2 is tested: the U.S. Robotics 56K of 1998 above connected at x2 56000 /
28800 with a PPP session that held for 3:50 (the caller hung up), clean,
once libv90modem 0.5.0's error-control fixes were in. A caller that does not
take x2 up is left with V.90 or V.34 as before.

K56flex is experimental and has not been tried on a real K56flex modem yet
(a Zoom 2948 is on the way); it has only been proven against Motorola's SM56
soft modem run under emulation, which connects at 32000 to 52000 down and
28800 up. Leave it off unless you are testing it. Neither scheme was ever
published; see libv90modem/DESIGN.md, 4.2b.

The bulletin board calls were made with the Conexant: a board's own
number from Linux (227 835 3422 reached Telehack), and 1 800 BBS DIAL from
HyperTerminal in Windows XP at V.90 with error correction. The box fetched
the Telnet BBS Guide's list by itself, 1073 boards that day.

Not tested yet: other modems, the HT801 V1.1C, a real Dreamcast or Saturn,
Windows 95 and Windows Me, and a file transfer (Zmodem) on a bulletin
board call.

## Installing

Download `v90d-ht802fw.bin` (or `v90d-ht801fw.bin` for an HT801) from the releases page and see
[FLASHING.md](FLASHING.md). In short: log in to the stock web page, wait two
minutes after the box has started, upload the file on the firmware upload
page, wait three minutes. The box comes back on the same address with the
v90d pages (user `admin`, password `admin`; change it). An existing v90d is
upgraded the same way from its own Firmware page, and the settings are kept.

## Building

A Linux PC with the usual build tools (gcc, make, python3, curl, bc, flex,
bison) is enough.

```
sh fw/setup.sh          # fetches libv90modem, the cross toolchain, Linux, busybox, OpenVPN and mbed TLS (checked by SHA-256) and builds them
make -C fw release      # fw/build/release/v90d-ht802fw.bin and v90d-ht801fw.bin
```

Tests (no box needed):

```
make check              # everything on the PC (libv90modem's suite first)
make check-arm          # the same code built for the box, under qemu-arm (needs qemu-user-static)
make -C fw test-release # the firmware file, the flash tools and the loader against models of the box
```

## Layout

| directory | what |
|---|---|
| `ppp/` | the PPP server |
| `tel/` | tones, dialling, the call's life |
| `dce/` | modem, link layer and PPP tied into one call |
| `srv/` | the daemon: calls, network, NAT, the per-port services, the bulletin board and service calls |
| `term/` | the terminal menu: screen, art, the BBS directory and the Telnet BBS Guide's details (shared with Netstalgia) |
| `lists/` | Netstalgia's signed lists in the image: the bulletin boards, the numbers to services |
| `cfg/` | settings, status and the web interface |
| `arch/` | the Archive service's helpers |
| `fw/` | the firmware: kernel board support and drivers, loader, flash tools, image and release build |
| `test/`, `rig/` | the test harness, real-modem test rigs for a PC |
| `live/` | the bench scripts used for the measurements in `PROGRESS.md` |
| `refs/` | notes on the hardware, as facts |

## Licence

GPL-2.0-only, see [LICENSE](LICENSE). Contributions are welcome, see
[CONTRIBUTING.md](CONTRIBUTING.md).

This project is not affiliated with Grandstream. Flashing third-party
firmware is at your own risk.
