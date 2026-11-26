/* v90d/cfg - configuration loader. Format and API: see config.h. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "sbuf.h"

#define MSG_MAX 128     /* message without the "file:line: " prefix */

enum { S_NETWORK, S_PPP, S_MODEM, S_LINE, S_STATUS, S_LOG, S_MODEM2, S_LINE2, S_SYSTEM, S_SERVICES, S_COUNT };
static const char *const sec_names[S_COUNT] = { "network", "ppp", "modem", "line", "status", "log", "modem2", "line2", "system", "services" };

enum {
    K_LAN, K_ADDRESS, K_NETMASK, K_GATEWAY, K_PPP_LOCAL, K_PPP_PEER, K_DNS1, K_DNS2, K_NAT,
    K_AUTH, K_MRU, K_ECHO, K_USER,
    K_MODS, K_MAXDOWN, K_MAXUP, K_TXLEVEL, K_V42, K_V42BIS, K_V92, K_X2, K_K56, K_RINGS, K_LAW,
    K_NUMBER, K_TONES, K_BUSY, K_DROP, K_SESSION, K_IDLE, K_SERVICE, K_DCNOW, K_DCLAN, K_DCUPNP, K_DCIWANGO, K_SATMATCH,
    K_LISTEN, K_PORT, K_ADMINPW,
    K_LEVEL,
    K_PPP_PEER2, K_ARCHIVE, K_COMPRESSION, K_ENCRYPTION, K_ENCRYPTION2, K_UPDATE_CHECK, K_DNS_RELAY, K_IPV6,
    K_SVC_NETSTALGIA, K_SVC_MENU, K_SVC,
    /* port 2: the keys of [modem] and [line] again, in the same order */
    K2_MODS, K2_MAXDOWN, K2_MAXUP, K2_TXLEVEL, K2_V42, K2_V42BIS, K2_V92, K2_X2, K2_K56, K2_RINGS, K2_LAW,
    K2_NUMBER, K2_TONES, K2_BUSY, K2_DROP, K2_SESSION, K2_IDLE, K2_SERVICE, K2_DCNOW, K2_DCLAN, K2_DCUPNP, K2_DCIWANGO, K2_SATMATCH,
    K_COUNT
};
#define K2_OFF (K2_MODS - K_MODS)
static const struct { int sec; const char *name; } keydefs[K_COUNT] = {
    { S_NETWORK, "lan" }, { S_NETWORK, "address" }, { S_NETWORK, "netmask" }, { S_NETWORK, "gateway" },
    { S_NETWORK, "ppp_local" }, { S_NETWORK, "ppp_peer" },
    { S_NETWORK, "dns1" }, { S_NETWORK, "dns2" }, { S_NETWORK, "nat" },
    { S_PPP, "auth" }, { S_PPP, "mru" }, { S_PPP, "echo_interval" }, { S_PPP, "user" },
    { S_MODEM, "modulations" }, { S_MODEM, "max_downstream" }, { S_MODEM, "max_upstream" },
    { S_MODEM, "tx_level" }, { S_MODEM, "v42" }, { S_MODEM, "v42bis" }, { S_MODEM, "v92" }, { S_MODEM, "x2" }, { S_MODEM, "k56" },
    { S_MODEM, "rings" }, { S_MODEM, "law" },
    { S_LINE, "number" }, { S_LINE, "tones" }, { S_LINE, "busy_chance" },
    { S_LINE, "drop_mean" }, { S_LINE, "session_limit" }, { S_LINE, "idle_timeout" },
    { S_LINE, "service" }, { S_LINE, "dreamcast_now" }, { S_LINE, "dreamcast_lan" }, { S_LINE, "dreamcast_upnp" }, { S_LINE, "dreamcast_iwango" }, { S_LINE, "saturn_matchmaking" },
    { S_STATUS, "listen" }, { S_STATUS, "port" }, { S_STATUS, "admin_password" },
    { S_LOG, "level" },
    { S_NETWORK, "ppp_peer2" }, { S_NETWORK, "archive" }, { S_PPP, "compression" }, { S_PPP, "encryption" }, { S_PPP, "encryption2" }, { S_SYSTEM, "update_check" }, { S_NETWORK, "dns_relay" }, { S_NETWORK, "ipv6" },
    { S_SERVICES, "netstalgia" }, { S_SERVICES, "menu" }, { S_SERVICES, "service" },
    { S_MODEM2, "modulations" }, { S_MODEM2, "max_downstream" }, { S_MODEM2, "max_upstream" },
    { S_MODEM2, "tx_level" }, { S_MODEM2, "v42" }, { S_MODEM2, "v42bis" }, { S_MODEM2, "v92" }, { S_MODEM2, "x2" }, { S_MODEM2, "k56" },
    { S_MODEM2, "rings" }, { S_MODEM2, "law" },
    { S_LINE2, "number" }, { S_LINE2, "tones" }, { S_LINE2, "busy_chance" },
    { S_LINE2, "drop_mean" }, { S_LINE2, "session_limit" }, { S_LINE2, "idle_timeout" },
    { S_LINE2, "service" }, { S_LINE2, "dreamcast_now" }, { S_LINE2, "dreamcast_lan" }, { S_LINE2, "dreamcast_upnp" }, { S_LINE2, "dreamcast_iwango" }, { S_LINE2, "saturn_matchmaking" },
};

/* value names; the index is the stored value */
static const char *const n_lan[]    = { "static", "dhcp" };
static const char *const n_onoff[]  = { "off", "on" };
static const char *const n_auth[]   = { "none", "pap", "chap" };
static const char *const n_law[]    = { "ulaw", "alaw" };
static const char *const n_tones[]  = { "us", "eu" };
static const char *const n_service[] = { "internet", "archive", "dreamcast", "saturn" };
static const char *const n_listen[] = { "off", "lan" };
static const char *const n_level[]  = { "error", "warn", "info", "debug" };
static const char *const n_mods[]   = { "v90", "v34", "v32bis", "v22bis", "v22", "v21", "v23" };  /* bit i */
/* the same lists in the order the messages and the docs give them */
static const char l_lan[]    = "dhcp, static";
static const char l_onoff[]  = "on, off";
static const char l_auth[]   = "none, pap, chap";
static const char l_law[]    = "ulaw, alaw";
static const char l_tones[]  = "us, eu";
static const char l_service[] = "internet, archive, dreamcast, saturn";
static const char l_listen[] = "lan, off";
static const char l_level[]  = "error, warn, info, debug";
static const char l_mods[]   = "v90, v34, v32bis, v22bis, v22, v21, v23";

/* the [modem] and [line] fields of *c into a port, and back */
static void port_take(cfg_port_t *p, const cfg_t *c)
{
    p->ppp_peer = c->ppp_peer;
    p->encryption = c->encryption;
    p->modulations = c->modulations; p->max_down = c->max_down; p->max_up = c->max_up;
    p->tx_level = c->tx_level; p->v42 = c->v42; p->v42bis = c->v42bis; p->v92 = c->v92; p->x2 = c->x2; p->k56 = c->k56;
    p->rings = c->rings; p->law = c->law;
    memcpy(p->line_number, c->line_number, sizeof p->line_number);
    p->line_tones = c->line_tones; p->busy_chance = c->busy_chance; p->drop_mean_min = c->drop_mean_min;
    p->session_limit_min = c->session_limit_min; p->idle_timeout_min = c->idle_timeout_min;
    p->service = c->service; p->dc_now = c->dc_now; p->dc_lan = c->dc_lan; p->dc_upnp = c->dc_upnp; p->dc_iwango = c->dc_iwango; p->sat_match = c->sat_match;
}

static void port_give(const cfg_port_t *p, cfg_t *c)
{
    c->ppp_peer = p->ppp_peer;
    c->encryption = p->encryption;
    c->modulations = p->modulations; c->max_down = p->max_down; c->max_up = p->max_up;
    c->tx_level = p->tx_level; c->v42 = p->v42; c->v42bis = p->v42bis; c->v92 = p->v92; c->x2 = p->x2; c->k56 = p->k56;
    c->rings = p->rings; c->law = p->law;
    memcpy(c->line_number, p->line_number, sizeof c->line_number);
    c->line_tones = p->line_tones; c->busy_chance = p->busy_chance; c->drop_mean_min = p->drop_mean_min;
    c->session_limit_min = p->session_limit_min; c->idle_timeout_min = p->idle_timeout_min;
    c->service = p->service; c->dc_now = p->dc_now; c->dc_lan = p->dc_lan; c->dc_upnp = p->dc_upnp; c->dc_iwango = p->dc_iwango; c->sat_match = p->sat_match;
}

void cfg_for_port(const cfg_t *c, int port, cfg_t *out)
{
    if (out != c) *out = *c;
    if (port == 1) {
        cfg_port_t one;
        memset(&one, 0, sizeof one);
        port_take(&one, c);
        port_give(&c->port2, out);
        out->port2 = one;               /* the other port's, so that both views validate */
    }
}

void cfg_default(cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->lan_dhcp      = 1;
    c->lan_addr      = 0xC0A80102u;     /* 192.168.1.2 */
    c->lan_netmask   = 0xFFFFFF00u;
    c->lan_gateway   = 0;
    c->ppp_local     = 0x0A404001u;     /* 10.64.64.1 */
    c->ppp_peer      = 0x0A404002u;     /* 10.64.64.2 */
    c->dns[0]        = 0x09090909u;     /* 9.9.9.9 */
    c->dns[1]        = 0x01010101u;     /* 1.1.1.1 */
    c->nat           = 1;
    c->dns_relay     = 1;
    c->ipv6          = 1;
    c->auth          = CFG_AUTH_NONE;
    c->mru           = 1500;
    c->echo_interval = 30;
    c->compression   = 1;
    c->encryption    = 0;
    c->modulations   = CFG_MOD_DEFAULT;
    c->max_down      = 56000;
    c->max_up        = 33600;
    c->tx_level      = -12;
    c->v42           = 1;
    c->v42bis        = 1;
    c->v92           = 1;
    c->rings         = 1;
    c->law           = CFG_LAW_ULAW;
    c->line_tones    = CFG_TONES_US;    /* number "" = any; no busy, no drops */
    c->status_listen = CFG_LISTEN_LAN;
    c->status_port   = 80;
    c->log_level     = CFG_LOG_INFO;
    c->svc_netstalgia = 1;
    port_take(&c->port2, c);            /* the same modem and line settings */
    c->port2.ppp_peer = 0x0A404003u;    /* 10.64.64.3 */
}

/* ---- messages ---------------------------------------------------------- */

/* "[section] key: <text>" into msg; always returns -1 */
static int kerr(char *msg, int k, const char *fmt, ...)
{
    va_list ap;
    int n = snprintf(msg, MSG_MAX, "[%s] %s: ", sec_names[keydefs[k].sec], keydefs[k].name);
    va_start(ap, fmt);
    vsnprintf(msg + n, (size_t)(MSG_MAX - n), fmt, ap);
    va_end(ap);
    return -1;
}

static const char *ipstr(uint32_t a, char out[16])
{
    sbuf_t s;
    sb_init(&s, out, 16);
    sb_ip(&s, a);
    sb_end(&s);
    return out;
}

/* a name for a message: at most 40 octets, anything unprintable as '?' */
static const char *safe(const char *z, char out[41])
{
    size_t i;
    for (i = 0; i < 40 && z[i]; i++)
        out[i] = (z[i] >= 0x20 && z[i] < 0x7f) ? z[i] : '?';
    out[i] = '\0';
    return out;
}

/* ---- validation (shared by cfg_validate and the parser) ----------------- */

static int host_ok(uint32_t a)
{
    unsigned top = a >> 24;
    return top >= 1 && top <= 223 && top != 127;
}

static int rng(char *msg, int k, long v, long lo, long hi)
{
    if (v >= lo && v <= hi) return 0;
    return kerr(msg, k, "%ld is out of range %ld..%ld", v, lo, hi);
}

static int en(char *msg, int k, long v, long n)
{
    if (v >= 0 && v < n) return 0;
    return kerr(msg, k, "invalid value %ld", v);
}

static int host(char *msg, int k, uint32_t a)
{
    char ip[16];
    if (host_ok(a)) return 0;
    return kerr(msg, k, "%s is not a usable host address", ipstr(a, ip));
}

/* 1 if z is 1..CFG_NUMBER_MAX characters out of 0-9 * # */
static int number_ok(const char *z)
{
    size_t n = strspn(z, "0123456789*#");
    return n >= 1 && n <= CFG_NUMBER_MAX && z[n] == '\0';
}


/* ---- the numbers of services ([services], srv/svc.h) --------------------- */

static int all_digits(const char *z, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) if (z[i] < '0' || z[i] > '9') return 0;
    return 1;
}

int cfg_svc_parse(const char *v, size_t n, cfg_service_t *e, const char **why)
{
    static const char hostch[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";
    size_t i = 0, b, colon, k;
    long port = 0;
    memset(e, 0, sizeof *e);
#define WORD() do { while (i < n && (v[i] == ' ' || v[i] == '\t')) i++; b = i; while (i < n && v[i] != ' ' && v[i] != '\t') i++; } while (0)
    WORD();
    if (i == b) { *why = "missing number"; return -1; }
    if (i - b < CFG_SVC_NUMBER_MIN || i - b > CFG_SVC_NUMBER_MAX || !all_digits(v + b, i - b)) {
        *why = "the number is not 3 to 15 digits"; return -1;
    }
    memcpy(e->number, v + b, i - b);
    WORD();
    if (i == b) { *why = "missing HOST:PORT"; return -1; }
    for (colon = i; colon > b && v[colon - 1] != ':'; colon--) { }
    if (colon == b) { *why = "missing :PORT after the host"; return -1; }
    colon--;                                            /* at the ':' */
    if (colon == b || colon - b > CFG_SVC_HOST_MAX || v[b] == '-') { *why = "the host is not 1 to 63 characters"; return -1; }
    for (k = b; k < colon; k++) if (!memchr(hostch, v[k], sizeof hostch - 1)) { *why = "the host has a character other than letters, digits, '.' and '-'"; return -1; }
    if (i - colon - 1 < 1 || i - colon - 1 > 5 || !all_digits(v + colon + 1, i - colon - 1) || v[colon + 1] == '0') {
        *why = "the port is not a number from 1 to 65535"; return -1;
    }
    for (k = colon + 1; k < i; k++) port = port * 10 + (v[k] - '0');
    if (port < 1 || port > 65535) { *why = "the port is not a number from 1 to 65535"; return -1; }
    memcpy(e->host, v + b, colon - b);
    e->port = (uint16_t)port;
    WORD();
    if (i - b == 3 && memcmp(v + b, "raw", 3) == 0) e->mode = CFG_SVC_RAW;
    else if (i - b == 6 && memcmp(v + b, "telnet", 6) == 0) e->mode = CFG_SVC_TELNET;
    else { *why = i == b ? "missing mode (raw or telnet)" : "the mode is not raw or telnet"; return -1; }
#undef WORD
    while (i < n && (v[i] == ' ' || v[i] == '\t')) i++;
    while (n > i && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
    if (n == i) { *why = "missing name"; return -1; }
    if (n - i > CFG_SVC_NAME_MAX) { *why = "the name is longer than 47 characters"; return -1; }
    for (k = i; k < n; k++) if (v[k] < 0x20 || v[k] > 0x7e) { *why = "the name is not printable ASCII"; return -1; }
    memcpy(e->name, v + i, n - i);
    for (k = 0; e->name[k]; k++) if (e->name[k] == '\t') e->name[k] = ' ';
    return 0;
}

int cfg_svc_number_match(const char *listed, const char *dialed)
{
    size_t a = strlen(listed), d = strlen(dialed);
    if (a == d) return strcmp(listed, dialed) == 0;
    if (a == 11 && d == 10 && listed[0] == '1') return strcmp(listed + 1, dialed) == 0;
    if (d == 11 && a == 10 && dialed[0] == '1') return strcmp(listed, dialed + 1) == 0;
    return 0;
}

int cfg_svc_number_begins(const char *listed, const char *dialed)
{
    size_t a = strlen(listed), d = strlen(dialed);
    if (cfg_svc_number_match(listed, dialed)) return 2;
    if (d < a && memcmp(listed, dialed, d) == 0) return 1;
    if (a == 11 && listed[0] == '1' && d < 10 && memcmp(listed + 1, dialed, d) == 0) return 1;
    if (a == 10 && d < 11 && d > 0 && dialed[0] == '1' && memcmp(listed, dialed + 1, d - 1) == 0) return 1;
    return 0;
}

int cfg_svc_number_taken(const char *z)
{
    size_t n = strlen(z);
    if (n == 11 && z[0] == '1') { z++; n--; }
    if (n != 10 || !all_digits(z, n)) return 0;
    /* the BBS numbers, and the time service's (srv/srv.h, SRV_ACTS) */
    return strcmp(z, "8002273425") == 0 || memcmp(z, "227", 3) == 0 || strcmp(z, "3034944774") == 0;
}

/* the [modem] and [line] fields of *c; o = 0, or K2_OFF when they hold
 * port 2's values (the messages then name [modem2] and [line2]) */
static int validate_port(const cfg_t *c, int o, char *msg, int *k1)
{
    *k1 = K_MODS + o;
    if (c->modulations == 0) return kerr(msg, K_MODS + o, "empty list");
    if (c->modulations & ~CFG_MOD_ALL) return kerr(msg, K_MODS + o, "unknown modulation bits");
    *k1 = K_MAXDOWN + o;   if (rng(msg, K_MAXDOWN + o, c->max_down, 300, 56000)) return -1;
    *k1 = K_MAXUP + o;     if (rng(msg, K_MAXUP + o, c->max_up, 300, 48000)) return -1;
    *k1 = K_TXLEVEL + o;   if (rng(msg, K_TXLEVEL + o, c->tx_level, -30, -6)) return -1;
    *k1 = K_V42 + o;       if (en(msg, K_V42 + o, c->v42, 2)) return -1;
    *k1 = K_V42BIS + o;    if (en(msg, K_V42BIS + o, c->v42bis, 2)) return -1;
    *k1 = K_V92 + o;       if (en(msg, K_V92 + o, c->v92, 2)) return -1;
    *k1 = K_X2 + o;        if (en(msg, K_X2 + o, c->x2, 2)) return -1;
    *k1 = K_K56 + o;       if (en(msg, K_K56 + o, c->k56, 2)) return -1;
    *k1 = K_RINGS + o;     if (rng(msg, K_RINGS + o, c->rings, 1, 10)) return -1;
    *k1 = K_LAW + o;       if (en(msg, K_LAW + o, c->law, 2)) return -1;
    *k1 = K_NUMBER + o;
    if (!memchr(c->line_number, '\0', sizeof c->line_number))
        return kerr(msg, K_NUMBER + o, "longer than %d characters", CFG_NUMBER_MAX);
    if (c->line_number[0] && !number_ok(c->line_number))
        return kerr(msg, K_NUMBER + o, "only 0-9 * # are allowed");
    *k1 = K_TONES + o;     if (en(msg, K_TONES + o, c->line_tones, 2)) return -1;
    *k1 = K_BUSY + o;      if (rng(msg, K_BUSY + o, c->busy_chance, 0, 100)) return -1;
    *k1 = K_DROP + o;      if (rng(msg, K_DROP + o, c->drop_mean_min, 0, 10080)) return -1;
    *k1 = K_SESSION + o;   if (rng(msg, K_SESSION + o, c->session_limit_min, 0, 10080)) return -1;
    *k1 = K_IDLE + o;      if (rng(msg, K_IDLE + o, c->idle_timeout_min, 0, 10080)) return -1;
    *k1 = K_SERVICE + o;   if (en(msg, K_SERVICE + o, c->service, CFG_SERVICE_COUNT)) return -1;
    *k1 = K_DCNOW + o;     if (en(msg, K_DCNOW + o, c->dc_now, 2)) return -1;
    *k1 = K_DCLAN + o;     if (en(msg, K_DCLAN + o, c->dc_lan, 2)) return -1;
    *k1 = K_DCUPNP + o;    if (en(msg, K_DCUPNP + o, c->dc_upnp, 2)) return -1;
    *k1 = K_DCIWANGO + o;  if (en(msg, K_DCIWANGO + o, c->dc_iwango, 2)) return -1;
    *k1 = K_SATMATCH + o;  if (en(msg, K_SATMATCH + o, c->sat_match, 2)) return -1;
    return 0;
}

/* 0 if valid. Otherwise -1, the message in msg, and in *k1 / *k2 the keys
 * to blame (the parser reports the line of k1 if the file set it, else of
 * k2, else no line). */
/* the rules between the [modem] keys of one port (o = 0, or K2_OFF with
 * port 2's values in *c, as for validate_port) */
static int modem_rules(const cfg_t *c, int o, char *msg, int *k1, int *k2)
{
    const char *sec = sec_names[keydefs[K_V42BIS + o].sec];
    if (c->v42bis && !c->v42) {
        *k1 = K_V42BIS + o; *k2 = K_V42 + o;
        snprintf(msg, MSG_MAX, "[%s] v42bis = on needs v42 = on (set v42bis = off)", sec);
        return -1;
    }
    if (c->x2 && !(c->modulations & CFG_MOD_V34)) {
        *k1 = K_X2 + o; *k2 = K_MODS + o;
        snprintf(msg, MSG_MAX, "[%s] x2 = on needs v34 in modulations", sec);
        return -1;
    }
    if (c->k56 && !(c->modulations & CFG_MOD_V34)) {
        *k1 = K_K56 + o; *k2 = K_MODS + o;
        snprintf(msg, MSG_MAX, "[%s] k56 = on needs v34 in modulations", sec);
        return -1;
    }
    return 0;
}

static int validate(const cfg_t *c, char *msg, int *k1, int *k2)
{
    char ip[16], nm[16], sn[41];
    uint32_t inv;
    int i, j;

    *k2 = -1;
    *k1 = K_LAN;      if (en(msg, K_LAN, c->lan_dhcp, 2)) return -1;
    *k1 = K_ADDRESS;  if (host(msg, K_ADDRESS, c->lan_addr)) return -1;
    *k1 = K_NETMASK;
    inv = ~c->lan_netmask;
    if ((inv & (inv + 1u)) != 0 || inv < 3u || inv == 0xFFFFFFFFu)
        return kerr(msg, K_NETMASK, "%s is not a contiguous netmask of /1 to /30",
                    ipstr(c->lan_netmask, ip));
    *k1 = K_GATEWAY;   if (c->lan_gateway && host(msg, K_GATEWAY, c->lan_gateway)) return -1;
    *k1 = K_PPP_LOCAL; if (host(msg, K_PPP_LOCAL, c->ppp_local)) return -1;
    *k1 = K_PPP_PEER;  if (host(msg, K_PPP_PEER, c->ppp_peer)) return -1;
    *k1 = K_DNS1;      if (c->dns[0] && host(msg, K_DNS1, c->dns[0])) return -1;
    *k1 = K_DNS2;      if (c->dns[1] && host(msg, K_DNS2, c->dns[1])) return -1;
    *k1 = K_NAT;       if (en(msg, K_NAT, c->nat, 2)) return -1;
    *k1 = K_DNS_RELAY; if (en(msg, K_DNS_RELAY, c->dns_relay, 2)) return -1;
    *k1 = K_IPV6;      if (en(msg, K_IPV6, c->ipv6, 2)) return -1;

    *k1 = K_AUTH;      if (en(msg, K_AUTH, c->auth, 3)) return -1;
    *k1 = K_MRU;       if (rng(msg, K_MRU, c->mru, 68, 1500)) return -1;
    *k1 = K_ECHO;      if (rng(msg, K_ECHO, c->echo_interval, 0, 3600)) return -1;
    *k1 = K_COMPRESSION; if (en(msg, K_COMPRESSION, c->compression, 2)) return -1;
    *k1 = K_ENCRYPTION; if (en(msg, K_ENCRYPTION, c->encryption, 2)) return -1;

    if (validate_port(c, 0, msg, k1)) return -1;
    *k1 = K_LISTEN;    if (en(msg, K_LISTEN, c->status_listen, 2)) return -1;
    *k1 = K_PORT;      if (rng(msg, K_PORT, c->status_port, 1, 65535)) return -1;
    *k1 = K_ADMINPW;
    if (!memchr(c->admin_password, '\0', sizeof c->admin_password))
        return kerr(msg, K_ADMINPW, "password is longer than %d octets", CFG_SECRET_MAX);
    *k1 = K_LEVEL;     if (en(msg, K_LEVEL, c->log_level, 4)) return -1;

    /* user table */
    *k1 = K_USER;
    if (c->n_users < 0 || c->n_users > CFG_MAX_USERS)
        return kerr(msg, K_USER, "more than %d users", CFG_MAX_USERS);
    for (i = 0; i < c->n_users; i++) {
        const cfg_user_t *u = &c->users[i];
        if (!memchr(u->name, '\0', sizeof u->name))
            return kerr(msg, K_USER, "name is longer than %d octets", CFG_NAME_MAX);
        if (!memchr(u->secret, '\0', sizeof u->secret))
            return kerr(msg, K_USER, "secret is longer than %d octets", CFG_SECRET_MAX);
        if (u->name[0] == '\0') return kerr(msg, K_USER, "empty name");
        for (j = 0; j < i; j++)
            if (strcmp(c->users[j].name, u->name) == 0)
                return kerr(msg, K_USER, "name '%s' already defined", safe(u->name, sn));
    }


    /* services */
    *k1 = K_SVC_NETSTALGIA; if (en(msg, K_SVC_NETSTALGIA, c->svc_netstalgia, 2)) return -1;
    *k1 = K_SVC_MENU; if (en(msg, K_SVC_MENU, c->svc_menu, 2)) return -1;
    *k1 = K_SVC;
    if (c->n_services < 0 || c->n_services > CFG_MAX_SERVICES)
        return kerr(msg, K_SVC, "more than %d services", CFG_MAX_SERVICES);
    for (i = 0; i < c->n_services; i++) {
        const cfg_service_t *e = &c->services[i];
        char t[CFG_SVC_NUMBER_MAX + CFG_SVC_HOST_MAX + CFG_SVC_NAME_MAX + 20];
        cfg_service_t back; const char *why = "";
        int tl;
        if (!memchr(e->number, '\0', sizeof e->number) || !memchr(e->host, '\0', sizeof e->host)
            || !memchr(e->name, '\0', sizeof e->name))
            return kerr(msg, K_SVC, "a field is too long");
        /* the same rules as the file: the entry written out reads back */
        tl = snprintf(t, sizeof t, "%s %s:%u %s %s", e->number, e->host, (unsigned)e->port,
                      e->mode == CFG_SVC_TELNET ? "telnet" : e->mode == CFG_SVC_RAW ? "raw" : "?", e->name);
        if (tl < 0 || (size_t)tl >= sizeof t || cfg_svc_parse(t, (size_t)tl, &back, &why))
            return kerr(msg, K_SVC, "%s: %s", safe(e->number, sn), why);
        if (strcmp(back.name, e->name) != 0)
            return kerr(msg, K_SVC, "%s: the name has blanks at an end or a tab", safe(e->number, sn));
        if (cfg_svc_number_taken(e->number))
            return kerr(msg, K_SVC, "%s is a number the box answers itself (the BBS numbers, the time service)", safe(e->number, sn));
        for (j = 0; j < i; j++)
            if (cfg_svc_number_match(c->services[j].number, e->number))
                return kerr(msg, K_SVC, "%s is listed twice", safe(e->number, sn));
        if (c->line_number[0] && cfg_svc_number_match(e->number, c->line_number))
            return kerr(msg, K_SVC, "%s is the number of port 1", safe(e->number, sn));
        if (c->port2.line_number[0] && cfg_svc_number_match(e->number, c->port2.line_number))
            return kerr(msg, K_SVC, "%s is the number of port 2", safe(e->number, sn));
    }

    /* rules across fields */
    if (c->ppp_peer == c->ppp_local) {
        *k1 = K_PPP_PEER; *k2 = K_PPP_LOCAL;
        return kerr(msg, K_PPP_PEER, "%s is the same as ppp_local", ipstr(c->ppp_peer, ip));
    }
    if (!c->lan_dhcp) {
        *k1 = K_ADDRESS; *k2 = K_NETMASK;
        if ((c->lan_addr & inv) == 0)
            return kerr(msg, K_ADDRESS, "%s is the network address of %s",
                        ipstr(c->lan_addr, ip), ipstr(c->lan_netmask, nm));
        if ((c->lan_addr & inv) == inv)
            return kerr(msg, K_ADDRESS, "%s is the broadcast address of %s",
                        ipstr(c->lan_addr, ip), ipstr(c->lan_netmask, nm));
        *k2 = K_LAN;
        if (c->lan_addr == c->ppp_peer)
            return kerr(msg, K_ADDRESS, "%s is the same as ppp_peer", ipstr(c->lan_addr, ip));
        if (c->lan_addr == c->ppp_local)
            return kerr(msg, K_ADDRESS, "%s is the same as ppp_local", ipstr(c->lan_addr, ip));
        if (c->lan_gateway) {
            *k1 = K_GATEWAY; *k2 = K_ADDRESS;
            if (c->lan_gateway == c->lan_addr)
                return kerr(msg, K_GATEWAY, "%s is the same as address", ipstr(c->lan_gateway, ip));
            if ((c->lan_gateway ^ c->lan_addr) & c->lan_netmask)
                return kerr(msg, K_GATEWAY, "%s is not in the network of address %s",
                            ipstr(c->lan_gateway, ip), ipstr(c->lan_addr, nm));
        }
    }
    if (c->auth != CFG_AUTH_NONE && c->n_users == 0) {
        *k1 = K_AUTH; *k2 = K_USER;
        return kerr(msg, K_AUTH, "%s needs at least one user", n_auth[c->auth]);
    }
    if ((c->encryption || c->port2.encryption) && c->n_users == 0) {
        /* the user list is shared, so either port needing MPPE needs a user */
        *k1 = c->encryption ? K_ENCRYPTION : K_ENCRYPTION2; *k2 = K_USER;
        return kerr(msg, *k1, "encryption needs MS-CHAPv2, so at least one user");
    }
    if (modem_rules(c, 0, msg, k1, k2)) return -1;

    /* port 2 */
    {
        cfg_t two;
        cfg_for_port(c, 1, &two);
        *k2 = -1;
        *k1 = K_ENCRYPTION2; if (en(msg, K_ENCRYPTION2, two.encryption, 2)) return -1;
        if (validate_port(&two, K2_OFF, msg, k1)) return -1;
        if (modem_rules(&two, K2_OFF, msg, k1, k2)) return -1;
        /* x2 and k56 may be on for both ports: the daemon serves only one heavy
         * call at a time (srv.c, the shared heavy slot), so no config rule
         * limits it. */
        *k1 = K_PPP_PEER2; *k2 = -1;
        if (host(msg, K_PPP_PEER2, two.ppp_peer)) return -1;
        *k2 = K_PPP_LOCAL;
        if (two.ppp_peer == c->ppp_local)
            return kerr(msg, K_PPP_PEER2, "%s is the same as ppp_local", ipstr(two.ppp_peer, ip));
        *k2 = K_PPP_PEER;
        if (two.ppp_peer == c->ppp_peer)
            return kerr(msg, K_PPP_PEER2, "%s is the same as ppp_peer", ipstr(two.ppp_peer, ip));
        if (!c->lan_dhcp && c->lan_addr == two.ppp_peer) {
            *k1 = K_ADDRESS; *k2 = K_PPP_PEER2;
            return kerr(msg, K_ADDRESS, "%s is the same as ppp_peer2", ipstr(c->lan_addr, ip));
        }
    }
    return 0;
}

int cfg_validate(const cfg_t *c, char *err, size_t errsz)
{
    char msg[MSG_MAX];
    int k1, k2;
    if (validate(c, msg, &k1, &k2) == 0) {
        if (errsz) err[0] = '\0';
        return CFG_OK;
    }
    if (errsz) snprintf(err, errsz, "%s", msg);
    return CFG_ERR;
}

/* ---- value syntax -------------------------------------------------------- */

static int is_blank(char ch) { return ch == ' ' || ch == '\t'; }

/* plain decimal, optional '-', no leading zeros, at most 9 digits */
static int get_int(const char *v, long *out)
{
    const char *p = v;
    long n = 0;
    int neg = 0, digits = 0;
    if (*p == '-') { neg = 1; p++; }
    if (p[0] == '0' && p[1] != '\0') return -1;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 9) return -1;
        n = n * 10 + (*p++ - '0');
    }
    if (digits == 0 || *p != '\0') return -1;
    *out = neg ? -n : n;
    return 0;
}

/* dotted quad, each part 0..255 without leading zeros */
static int get_ip(const char *v, uint32_t *out)
{
    const char *p = v;
    uint32_t a = 0;
    int i;
    for (i = 0; i < 4; i++) {
        unsigned n = 0;
        int digits = 0;
        if (p[0] == '0' && p[1] >= '0' && p[1] <= '9') return -1;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return -1;
            n = n * 10u + (unsigned)(*p++ - '0');
        }
        if (digits == 0 || n > 255u) return -1;
        if (*p++ != (i < 3 ? '.' : '\0')) return -1;
        a = (a << 8) | n;
    }
    *out = a;
    return 0;
}

/* index of the n-octet word v in names[0..count), or -1 */
static int pick(const char *v, size_t n, const char *const *names, int count)
{
    int i;
    for (i = 0; i < count; i++)
        if (strlen(names[i]) == n && memcmp(names[i], v, n) == 0) return i;
    return -1;
}

static int hexval(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* One bare or quoted word from *pp into out (NUL terminated, at most max
 * octets). Returns 0, or -1 with *why set to what is wrong ("long" when
 * the word exceeds max). *quoted tells which form it was. */
static int get_word(const char **pp, char *out, size_t max, int *quoted, const char **why,
                    char *esc)
{
    const char *p = *pp;
    size_t n = 0;
    int ch;

    *quoted = (*p == '"');
    if (*quoted) {
        p++;
        for (;;) {
            if (*p == '\0') { *why = "unterminated quoted string"; return -1; }
            if (*p == '"') { p++; break; }
            ch = (unsigned char)*p++;
            if (ch == '\\') {
                if (*p == '\\' || *p == '"') {
                    ch = *p++;
                } else if (*p == 'x') {
                    int hi = hexval(p[1]);
                    int lo = hi < 0 ? -1 : hexval(p[2]);
                    if (lo < 0 || (hi == 0 && lo == 0)) {
                        *why = "bad \\x escape (two hex digits, not 00)";
                        return -1;
                    }
                    ch = hi * 16 + lo;
                    p += 3;
                } else {
                    *esc = *p ? *p : ' ';
                    *why = "unknown escape";
                    return -1;
                }
            }
            if (n >= max) { *why = "long"; return -1; }
            out[n++] = (char)ch;
        }
        if (*p != '\0' && !is_blank(*p)) { *why = "missing blank after closing quote"; return -1; }
    } else {
        while (*p != '\0' && !is_blank(*p)) {
            if (*p == '"' || *p == '\\') {
                *why = "quote or backslash in a bare word (use a quoted string)";
                return -1;
            }
            if (n >= max) { *why = "long"; return -1; }
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
    *pp = p;
    return 0;
}

/* ---- parser -------------------------------------------------------------- */

typedef struct {
    int line_of[K_COUNT];               /* line that set each key, 0 = none */
    int user_line[CFG_MAX_USERS];
    int old_archive;                    /* [network] archive = on */
    cfg_t two;                          /* [modem2] and [line2] are read into the
                                           [modem] and [line] fields of this one */
} pctx_t;

/* the message for a get_word failure on the word called `what` */
static int word_err(char *msg, int k, const char *what, int max, const char *why, char esc)
{
    if (strcmp(why, "long") == 0)
        return kerr(msg, k, "%s is longer than %d octets", what, max);
    if (strcmp(why, "unknown escape") == 0)
        return kerr(msg, k, "unknown escape \\%c (use \\\" \\\\ or \\xHH)", esc);
    return kerr(msg, k, "%s", why);
}

static int set_user(cfg_t *c, pctx_t *px, int line, const char *v, char *msg)
{
    cfg_user_t u;
    const char *p = v, *why = "";
    char esc = ' ', sn[41];
    int quoted, i;

    memset(&u, 0, sizeof u);
    if (get_word(&p, u.name, CFG_NAME_MAX, &quoted, &why, &esc))
        return word_err(msg, K_USER, "name", CFG_NAME_MAX, why, esc);
    if (u.name[0] == '\0') return kerr(msg, K_USER, "empty name");
    while (is_blank(*p)) p++;
    if (*p == '\0') return kerr(msg, K_USER, "missing secret (user = NAME SECRET)");
    if (get_word(&p, u.secret, CFG_SECRET_MAX, &quoted, &why, &esc))
        return word_err(msg, K_USER, "secret", CFG_SECRET_MAX, why, esc);
    if (!quoted && strcmp(u.secret, "<redacted>") == 0)
        return kerr(msg, K_USER, "secret is <redacted>: this is a redacted dump, not a config file");
    while (is_blank(*p)) p++;
    if (*p != '\0') return kerr(msg, K_USER, "unexpected text after the secret");
    for (i = 0; i < c->n_users; i++)
        if (strcmp(c->users[i].name, u.name) == 0)
            return kerr(msg, K_USER, "name '%s' already defined on line %d",
                        safe(u.name, sn), px->user_line[i]);
    if (c->n_users >= CFG_MAX_USERS)
        return kerr(msg, K_USER, "more than %d users", CFG_MAX_USERS);
    px->user_line[c->n_users] = line;
    c->users[c->n_users++] = u;
    return 0;
}

static int set_service(cfg_t *c, const char *v, char *msg)
{
    cfg_service_t e; const char *why = "";
    if (cfg_svc_parse(v, strlen(v), &e, &why)) return kerr(msg, K_SVC, "%s (service = NUMBER HOST:PORT raw|telnet NAME)", why);
    if (c->n_services >= CFG_MAX_SERVICES) return kerr(msg, K_SVC, "more than %d services", CFG_MAX_SERVICES);
    c->services[c->n_services++] = e;
    return 0;
}

/* one word like a user secret; nothing at all is the empty password */
static int set_password(cfg_t *c, const char *v, char *msg)
{
    char pw[CFG_SECRET_MAX + 1];
    const char *p = v, *why = "";
    char esc = ' ';
    int quoted;

    memset(pw, 0, sizeof pw);
    if (get_word(&p, pw, CFG_SECRET_MAX, &quoted, &why, &esc))
        return word_err(msg, K_ADMINPW, "password", CFG_SECRET_MAX, why, esc);
    if (!quoted && strcmp(pw, "<redacted>") == 0)
        return kerr(msg, K_ADMINPW, "password is <redacted>: this is a redacted dump, not a config file");
    while (is_blank(*p)) p++;
    if (*p != '\0') return kerr(msg, K_ADMINPW, "unexpected text after the password");
    memcpy(c->admin_password, pw, sizeof pw);
    return 0;
}

static int set_number(cfg_t *c, int k, const char *v, char *msg)
{
    memset(c->line_number, 0, sizeof c->line_number);
    if (strcmp(v, "any") == 0) return 0;
    if (!number_ok(v))
        return kerr(msg, k, "'%.40s' is not 1..%d characters from 0-9 * # or the word any",
                    v, CFG_NUMBER_MAX);
    strcpy(c->line_number, v);
    return 0;
}

static int set_mods(cfg_t *c, int k, const char *v, char *msg)
{
    unsigned mask = 0;
    const char *p = v;
    for (;;) {
        const char *b, *e;
        int m;
        while (is_blank(*p)) p++;
        b = p;
        while (*p != '\0' && *p != ',') p++;
        e = p;
        while (e > b && is_blank(e[-1])) e--;
        if (e == b) return kerr(msg, k, "empty list item");
        m = pick(b, (size_t)(e - b), n_mods, CFG_MOD_COUNT);
        if (m < 0)
            return kerr(msg, k, "'%.*s' is not one of: %s",
                        (int)(e - b > 40 ? 40 : e - b), b, l_mods);
        if (mask & (1u << m)) return kerr(msg, k, "'%s' is listed twice", n_mods[m]);
        mask |= 1u << m;
        if (*p == '\0') break;
        p++;
    }
    c->modulations = mask;
    return 0;
}

static int set_enum(int *field, int k, const char *v, const char *const *names, int count,
                    const char *list, char *msg)
{
    int i = pick(v, strlen(v), names, count);
    if (i < 0) return kerr(msg, k, "'%.40s' is not one of: %s", v, list);
    *field = i;
    return 0;
}

static int set_int(int *field, int k, const char *v, char *msg)
{
    long n;
    if (get_int(v, &n)) return kerr(msg, k, "'%.40s' is not a number", v);
    *field = (int)n;
    return 0;
}

int cfg_parse_ip(const char *v, uint32_t *out)
{
    return get_ip(v, out) ? -1 : 0;
}

static int set_ip(uint32_t *field, int k, const char *v, int none_ok, char *msg)
{
    uint32_t a;
    if (none_ok && strcmp(v, "none") == 0) { *field = 0; return 0; }
    if (get_ip(v, &a)) return kerr(msg, k, "'%.40s' is not an IPv4 address", v);
    /* 0 in the struct means "none", so a literal 0.0.0.0 must fail here */
    if (none_ok && host(msg, k, a)) return -1;
    *field = a;
    return 0;
}

/* k says which field, mk is the key the messages name (the same, but for
 * port 2) */
static int set_value(cfg_t *c, pctx_t *px, int k, int mk, int line, const char *v, char *msg)
{
    if (k >= K2_MODS) return set_value(&px->two, px, k - K2_OFF, k, line, v, msg);
    switch (k) {
    case K_PPP_PEER2: return set_ip(&px->two.ppp_peer, mk, v, 0, msg);
    case K_LAN:       return set_enum(&c->lan_dhcp, mk, v, n_lan, 2, l_lan, msg);
    case K_ADDRESS:   return set_ip(&c->lan_addr, mk, v, 0, msg);
    case K_NETMASK:   return set_ip(&c->lan_netmask, mk, v, 0, msg);
    case K_GATEWAY:   return set_ip(&c->lan_gateway, mk, v, 1, msg);
    case K_PPP_LOCAL: return set_ip(&c->ppp_local, mk, v, 0, msg);
    case K_PPP_PEER:  return set_ip(&c->ppp_peer, mk, v, 0, msg);
    case K_DNS1:      return set_ip(&c->dns[0], mk, v, 1, msg);
    case K_DNS2:      return set_ip(&c->dns[1], mk, v, 1, msg);
    case K_NAT:       return set_enum(&c->nat, mk, v, n_onoff, 2, l_onoff, msg);
    case K_UPDATE_CHECK: return set_enum(&c->update_check, mk, v, n_onoff, 2, l_onoff, msg);
    case K_DNS_RELAY: return set_enum(&c->dns_relay, mk, v, n_onoff, 2, l_onoff, msg);
    case K_IPV6:      return set_enum(&c->ipv6, mk, v, n_onoff, 2, l_onoff, msg);
    /* the switch for the whole box of older settings: see cfg_parse */
    case K_ARCHIVE:   return set_enum(&px->old_archive, mk, v, n_onoff, 2, l_onoff, msg);
    case K_AUTH:      return set_enum(&c->auth, mk, v, n_auth, 3, l_auth, msg);
    case K_MRU:       return set_int(&c->mru, mk, v, msg);
    case K_ECHO:      return set_int(&c->echo_interval, mk, v, msg);
    case K_COMPRESSION: return set_enum(&c->compression, mk, v, n_onoff, 2, l_onoff, msg);
    case K_ENCRYPTION: return set_enum(&c->encryption, mk, v, n_onoff, 2, l_onoff, msg);
    case K_ENCRYPTION2: return set_enum(&px->two.encryption, mk, v, n_onoff, 2, l_onoff, msg);
    case K_USER:      return set_user(c, px, line, v, msg);
    case K_SVC:       return set_service(c, v, msg);
    case K_SVC_NETSTALGIA: return set_enum(&c->svc_netstalgia, mk, v, n_onoff, 2, l_onoff, msg);
    case K_SVC_MENU:  return set_enum(&c->svc_menu, mk, v, n_onoff, 2, l_onoff, msg);
    case K_MODS:      return set_mods(c, mk, v, msg);
    case K_MAXDOWN:   return set_int(&c->max_down, mk, v, msg);
    case K_MAXUP:     return set_int(&c->max_up, mk, v, msg);
    case K_TXLEVEL:   return set_int(&c->tx_level, mk, v, msg);
    case K_V42:       return set_enum(&c->v42, mk, v, n_onoff, 2, l_onoff, msg);
    case K_V42BIS:    return set_enum(&c->v42bis, mk, v, n_onoff, 2, l_onoff, msg);
    case K_V92:       return set_enum(&c->v92, mk, v, n_onoff, 2, l_onoff, msg);
    case K_X2:        return set_enum(&c->x2, mk, v, n_onoff, 2, l_onoff, msg);
    case K_K56:       return set_enum(&c->k56, mk, v, n_onoff, 2, l_onoff, msg);
    case K_RINGS:     return set_int(&c->rings, mk, v, msg);
    case K_LAW:       return set_enum(&c->law, mk, v, n_law, 2, l_law, msg);
    case K_NUMBER:    return set_number(c, mk, v, msg);
    case K_TONES:     return set_enum(&c->line_tones, mk, v, n_tones, 2, l_tones, msg);
    case K_BUSY:      return set_int(&c->busy_chance, mk, v, msg);
    case K_DROP:      return set_int(&c->drop_mean_min, mk, v, msg);
    case K_SESSION:   return set_int(&c->session_limit_min, mk, v, msg);
    case K_IDLE:      return set_int(&c->idle_timeout_min, mk, v, msg);
    case K_SERVICE:   return set_enum(&c->service, mk, v, n_service, CFG_SERVICE_COUNT, l_service, msg);
    case K_DCNOW:     return set_enum(&c->dc_now, mk, v, n_onoff, 2, l_onoff, msg);
    case K_DCLAN:     return set_enum(&c->dc_lan, mk, v, n_onoff, 2, l_onoff, msg);
    case K_DCUPNP:    return set_enum(&c->dc_upnp, mk, v, n_onoff, 2, l_onoff, msg);
    case K_DCIWANGO:  return set_enum(&c->dc_iwango, mk, v, n_onoff, 2, l_onoff, msg);
    case K_SATMATCH:  return set_enum(&c->sat_match, mk, v, n_onoff, 2, l_onoff, msg);
    case K_LISTEN:    return set_enum(&c->status_listen, mk, v, n_listen, 2, l_listen, msg);
    case K_PORT:      return set_int(&c->status_port, mk, v, msg);
    case K_ADMINPW:   return set_password(c, v, msg);
    case K_LEVEL:     return set_enum(&c->log_level, mk, v, n_level, 4, l_level, msg);
    default:          return -1;
    }
}

/* "name:line: msg" (line 0: "name: msg"); long names keep their tail */
static void report(char *err, size_t errsz, const char *name, int line, const char *msg)
{
    size_t nl = strlen(name);
    if (!errsz) return;
    if (nl > 60) name += nl - 60;
    if (line > 0) snprintf(err, errsz, "%s:%d: %s", name, line, msg);
    else snprintf(err, errsz, "%s: %s", name, msg);
}

/* trim blanks at both ends of [b, e); returns the new start, NUL at the end */
static char *trim(char *b, char *e)
{
    while (b < e && is_blank(*b)) b++;
    while (e > b && is_blank(e[-1])) e--;
    *e = '\0';
    return b;
}

/* parse one line (already checked for length and bytes, NUL terminated) */
static int do_line(cfg_t *c, pctx_t *px, int *sec, int lineno, char *line, char *msg)
{
    char *l = trim(line, line + strlen(line));
    char *eq, *key, *val;
    size_t n = strlen(l);
    int k;

    if (*l == '\0' || *l == '#' || *l == ';') return 0;
    if (*l == '[') {
        if (l[n - 1] != ']' || memchr(l, ']', n) != l + n - 1) {
            snprintf(msg, MSG_MAX, "section header: missing ] at the end of the line");
            return -1;
        }
        key = trim(l + 1, l + n - 1);
        *sec = pick(key, strlen(key), sec_names, S_COUNT);
        if (*sec < 0) {
            snprintf(msg, MSG_MAX, "unknown section [%.32s] (sections: network ppp modem modem2"
                     " line line2 status log system services)", key);
            return -1;
        }
        return 0;
    }
    eq = strchr(l, '=');
    if (!eq) { snprintf(msg, MSG_MAX, "expected key = value, got '%.40s'", l); return -1; }
    val = trim(eq + 1, l + n);
    key = trim(l, eq);
    if (*key == '\0') { snprintf(msg, MSG_MAX, "missing key before ="); return -1; }
    if (*sec < 0) {
        snprintf(msg, MSG_MAX, "key '%.40s' outside any [section]", key);
        return -1;
    }
    for (k = 0; k < K_COUNT; k++)
        if (keydefs[k].sec == *sec && strcmp(keydefs[k].name, key) == 0) break;
    if (k == K_COUNT) {
        snprintf(msg, MSG_MAX, "unknown key '%.40s' in [%s]", key, sec_names[*sec]);
        return -1;
    }
    if (k != K_USER && k != K_SVC && px->line_of[k]) {
        snprintf(msg, MSG_MAX, "duplicate key '%s' (first set on line %d)", key, px->line_of[k]);
        return -1;
    }
    if (*val == '\0' && k != K_ADMINPW) return kerr(msg, k, "missing value");
    if (set_value(c, px, k, k, lineno, val, msg)) return -1;
    if (!px->line_of[k]) px->line_of[k] = lineno;
    return 0;
}

int cfg_parse(cfg_t *c, const char *name, const char *text, size_t len,
              char *err, size_t errsz)
{
    pctx_t px;
    char line[CFG_LINE_MAX + 1], msg[MSG_MAX];
    size_t pos = 0;
    int sec = -1, lineno = 0, k1, k2;

    memset(&px, 0, sizeof px);
    cfg_default(c);
    cfg_for_port(c, 1, &px.two);
    if (errsz) err[0] = '\0';
    msg[0] = '\0';

    while (pos < len) {
        const char *l = text + pos;
        size_t ll = 0, i;
        while (pos + ll < len && l[ll] != '\n') ll++;
        pos += ll + (pos + ll < len ? 1u : 0u);
        lineno++;
        if (ll && l[ll - 1] == '\r') ll--;
        if (ll > CFG_LINE_MAX) {
            snprintf(msg, sizeof msg, "line is longer than %d characters", CFG_LINE_MAX);
            goto fail;
        }
        for (i = 0; i < ll; i++) {
            unsigned ch = (unsigned char)l[i];
            if (ch != '\t' && (ch < 0x20 || ch > 0x7e)) {
                snprintf(msg, sizeof msg, "invalid byte 0x%02x at column %d (printable ASCII only;"
                         " write other octets as \\xHH in a quoted string)", ch, (int)i + 1);
                goto fail;
            }
        }
        memcpy(line, l, ll);
        line[ll] = '\0';
        if (do_line(c, &px, &sec, lineno, line, msg)) goto fail;
    }

    /* Settings written before each port had a service of its own: archive
     * = on in [network] meant both ports. It still does, for a port whose
     * section does not say. */
    if (px.old_archive) {
        if (!px.line_of[K_SERVICE]) c->service = CFG_SERVICE_ARCHIVE;
        if (!px.line_of[K2_SERVICE]) px.two.service = CFG_SERVICE_ARCHIVE;
    }
    port_take(&c->port2, &px.two);
    if (validate(c, msg, &k1, &k2)) {
        lineno = px.line_of[k1];
        if (!lineno && k2 >= 0) lineno = px.line_of[k2];
        goto fail;
    }
    return CFG_OK;

fail:
    cfg_default(c);
    report(err, errsz, name, lineno, msg);
    return CFG_ERR;
}

int cfg_load(cfg_t *c, const char *path, char *err, size_t errsz)
{
    char text[CFG_FILE_MAX + 1], msg[MSG_MAX];
    size_t len = 0;
    int fd;

    cfg_default(c);
    if (errsz) err[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT || errno == ENOTDIR) return CFG_DEFAULTS;
        snprintf(msg, sizeof msg, "cannot open: %s", strerror(errno));
        report(err, errsz, path, 0, msg);
        return CFG_ERR;
    }
    while (len < sizeof text) {
        ssize_t r = read(fd, text + len, sizeof text - len);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) {
            snprintf(msg, sizeof msg, "cannot read: %s", strerror(errno));
            close(fd);
            report(err, errsz, path, 0, msg);
            return CFG_ERR;
        }
        if (r == 0) break;
        len += (size_t)r;
    }
    close(fd);
    if (len > CFG_FILE_MAX) {
        snprintf(msg, sizeof msg, "file is larger than %d bytes", CFG_FILE_MAX);
        report(err, errsz, path, 0, msg);
        return CFG_ERR;
    }
    return cfg_parse(c, path, text, len, err, errsz);
}

/* ---- canonical writer ---------------------------------------------------- */

static void w_key(sbuf_t *s, int k)
{
    sb_puts(s, keydefs[k].name);
    sb_puts(s, " = ");
}

static void w_str(sbuf_t *s, int k, const char *v)
{
    w_key(s, k); sb_puts(s, v); sb_putc(s, '\n');
}

static void w_int(sbuf_t *s, int k, int v)
{
    w_key(s, k); sb_i32(s, v); sb_putc(s, '\n');
}

static void w_ip(sbuf_t *s, int k, uint32_t a, int none_ok)
{
    w_key(s, k);
    if (none_ok && a == 0) sb_puts(s, "none"); else sb_ip(s, a);
    sb_putc(s, '\n');
}

/* value name, or "?" for a field outside its range (cfg_validate refuses it) */
static const char *nm(const char *const *names, int count, int v)
{
    return (v >= 0 && v < count) ? names[v] : "?";
}

static void w_quoted(sbuf_t *s, const char *z, size_t max)
{
    size_t i;
    sb_putc(s, '"');
    for (i = 0; i < max && z[i]; i++) {
        unsigned ch = (unsigned char)z[i];
        if (ch == '"' || ch == '\\') { sb_putc(s, '\\'); sb_putc(s, (char)ch); }
        else if (ch < 0x20 || ch > 0x7e) { sb_puts(s, "\\x"); sb_hex2(s, ch); }
        else sb_putc(s, (char)ch);
    }
    sb_putc(s, '"');
}

/* [modem] and [line], or with o = K2_OFF and port 2's values in *c,
 * [modem2] and [line2] */
static void w_port(sbuf_t *s, const cfg_t *c, int o)
{
    int i, first = 1;
    sb_puts(s, o ? "\n[modem2]\n" : "\n[modem]\n");
    w_key(s, K_MODS + o);
    for (i = 0; i < CFG_MOD_COUNT; i++) {
        if (!(c->modulations & (1u << i))) continue;
        if (!first) sb_puts(s, ", ");
        sb_puts(s, n_mods[i]);
        first = 0;
    }
    sb_putc(s, '\n');
    w_int(s, K_MAXDOWN + o, c->max_down);
    w_int(s, K_MAXUP + o, c->max_up);
    w_int(s, K_TXLEVEL + o, c->tx_level);
    w_str(s, K_V42 + o, nm(n_onoff, 2, c->v42));
    w_str(s, K_V42BIS + o, nm(n_onoff, 2, c->v42bis));
    w_str(s, K_V92 + o, nm(n_onoff, 2, c->v92));
    w_str(s, K_X2 + o, nm(n_onoff, 2, c->x2));
    w_str(s, K_K56 + o, nm(n_onoff, 2, c->k56));
    w_int(s, K_RINGS + o, c->rings);
    w_str(s, K_LAW + o, nm(n_law, 2, c->law));

    sb_puts(s, o ? "\n[line2]\n" : "\n[line]\n");
    w_key(s, K_NUMBER + o);
    if (c->line_number[0]) {
        for (i = 0; i < CFG_NUMBER_MAX && c->line_number[i]; i++) sb_putc(s, c->line_number[i]);
    } else {
        sb_puts(s, "any");
    }
    sb_putc(s, '\n');
    w_str(s, K_TONES + o, nm(n_tones, 2, c->line_tones));
    w_int(s, K_BUSY + o, c->busy_chance);
    w_int(s, K_DROP + o, c->drop_mean_min);
    w_int(s, K_SESSION + o, c->session_limit_min);
    w_int(s, K_IDLE + o, c->idle_timeout_min);
    w_str(s, K_SERVICE + o, nm(n_service, CFG_SERVICE_COUNT, c->service));
    w_str(s, K_DCNOW + o, nm(n_onoff, 2, c->dc_now));
    w_str(s, K_DCLAN + o, nm(n_onoff, 2, c->dc_lan));
    w_str(s, K_DCUPNP + o, nm(n_onoff, 2, c->dc_upnp));
    w_str(s, K_DCIWANGO + o, nm(n_onoff, 2, c->dc_iwango));
    w_str(s, K_SATMATCH + o, nm(n_onoff, 2, c->sat_match));

}

int cfg_write(const cfg_t *c, int with_secrets, char *buf, size_t cap)
{
    sbuf_t s;
    cfg_t two;
    int i, n;

    sb_init(&s, buf, cap);
    sb_puts(&s, with_secrets ? "# v90d configuration\n"
                             : "# v90d configuration, secrets redacted\n");
    sb_puts(&s, "\n[network]\n");
    w_str(&s, K_LAN, nm(n_lan, 2, c->lan_dhcp));
    w_ip(&s, K_ADDRESS, c->lan_addr, 0);
    w_ip(&s, K_NETMASK, c->lan_netmask, 0);
    w_ip(&s, K_GATEWAY, c->lan_gateway, 1);
    w_ip(&s, K_PPP_LOCAL, c->ppp_local, 0);
    w_ip(&s, K_PPP_PEER, c->ppp_peer, 0);
    w_ip(&s, K_PPP_PEER2, c->port2.ppp_peer, 0);
    w_ip(&s, K_DNS1, c->dns[0], 1);
    w_ip(&s, K_DNS2, c->dns[1], 1);
    w_str(&s, K_NAT, nm(n_onoff, 2, c->nat));
    w_str(&s, K_DNS_RELAY, nm(n_onoff, 2, c->dns_relay));
    w_str(&s, K_IPV6, nm(n_onoff, 2, c->ipv6));

    sb_puts(&s, "\n[ppp]\n");
    w_str(&s, K_AUTH, nm(n_auth, 3, c->auth));
    w_int(&s, K_MRU, c->mru);
    w_int(&s, K_ECHO, c->echo_interval);
    w_str(&s, K_COMPRESSION, nm(n_onoff, 2, c->compression));
    w_str(&s, K_ENCRYPTION, nm(n_onoff, 2, c->encryption));
    w_str(&s, K_ENCRYPTION2, nm(n_onoff, 2, c->port2.encryption));
    n = c->n_users < 0 ? 0 : c->n_users > CFG_MAX_USERS ? CFG_MAX_USERS : c->n_users;
    for (i = 0; i < n; i++) {
        w_key(&s, K_USER);
        w_quoted(&s, c->users[i].name, sizeof c->users[i].name);
        sb_putc(&s, ' ');
        if (with_secrets) w_quoted(&s, c->users[i].secret, sizeof c->users[i].secret);
        else sb_puts(&s, "<redacted>");
        sb_putc(&s, '\n');
    }

    w_port(&s, c, 0);
    cfg_for_port(c, 1, &two);
    w_port(&s, &two, K2_OFF);

    sb_puts(&s, "\n[status]\n");
    w_str(&s, K_LISTEN, nm(n_listen, 2, c->status_listen));
    w_int(&s, K_PORT, c->status_port);
    w_key(&s, K_ADMINPW);
    if (with_secrets || c->admin_password[0] == '\0')
        w_quoted(&s, c->admin_password, sizeof c->admin_password);
    else
        sb_puts(&s, "<redacted>");
    sb_putc(&s, '\n');

    sb_puts(&s, "\n[log]\n");
    w_str(&s, K_LEVEL, nm(n_level, 4, c->log_level));

    sb_puts(&s, "\n[system]\n");
    w_str(&s, K_UPDATE_CHECK, nm(n_onoff, 2, c->update_check));

    sb_puts(&s, "\n[services]\n");
    w_str(&s, K_SVC_NETSTALGIA, nm(n_onoff, 2, c->svc_netstalgia));
    w_str(&s, K_SVC_MENU, nm(n_onoff, 2, c->svc_menu));
    n = c->n_services < 0 ? 0 : c->n_services > CFG_MAX_SERVICES ? CFG_MAX_SERVICES : c->n_services;
    for (i = 0; i < n; i++) {
        const cfg_service_t *e = &c->services[i];
        w_key(&s, K_SVC);
        sb_puts(&s, e->number);
        sb_putc(&s, ' '); sb_puts(&s, e->host); sb_putc(&s, ':'); sb_u64(&s, e->port);
        sb_puts(&s, e->mode == CFG_SVC_TELNET ? " telnet " : " raw ");
        sb_puts(&s, e->name); sb_putc(&s, '\n');
    }
    return sb_end(&s);
}

/* ---- saving --------------------------------------------------------------- */

int cfg_save(const cfg_t *c, const char *path, char *err, size_t errsz)
{
    char text[CFG_TEXT_MAX], tmp[CFG_PATH_MAX + 5], msg[MSG_MAX];
    size_t pl = strlen(path), off = 0;
    int len, fd, k1, k2;

    if (errsz) err[0] = '\0';
    if (pl == 0) { snprintf(msg, sizeof msg, "empty path"); goto fail; }
    if (pl > CFG_PATH_MAX) {
        snprintf(msg, sizeof msg, "path is longer than %d characters", CFG_PATH_MAX);
        goto fail;
    }
    if (validate(c, msg, &k1, &k2)) goto fail;
    len = cfg_write(c, 1, text, sizeof text);
    if (len < 0) {                              /* a valid config always fits */
        snprintf(msg, sizeof msg, "config text is larger than %d bytes", CFG_TEXT_MAX);
        goto fail;
    }
    memcpy(tmp, path, pl);
    memcpy(tmp + pl, ".tmp", 5);

    /* A temporary left by an earlier crash goes first, so that the file is
     * always made here, with mode 0600, and never opened through a link. */
    unlink(tmp);
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        snprintf(msg, sizeof msg, "cannot write: %s", strerror(errno));
        goto fail;
    }
    while (off < (size_t)len) {
        ssize_t w = write(fd, text + off, (size_t)len - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) goto fail_write;
        off += (size_t)w;
    }
    if (fsync(fd) < 0) goto fail_write;
    if (close(fd) < 0) { fd = -1; goto fail_write; }
    if (rename(tmp, path) < 0) {
        snprintf(msg, sizeof msg, "cannot replace: %s", strerror(errno));
        unlink(tmp);
        goto fail;
    }
    return CFG_OK;

fail_write:
    snprintf(msg, sizeof msg, "cannot write: %s", strerror(errno));
    if (fd >= 0) close(fd);
    unlink(tmp);
fail:
    report(err, errsz, path, 0, msg);
    return CFG_ERR;
}
