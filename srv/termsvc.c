/* See termsvc.h. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "daemon.h"
#include "termsvc.h"
#include "bbslink.h"
#include "tmenu.h"
#include "tdir.h"

#define GREET_MS    1500u      /* after the modems connect: Dial-Up Networking speaks first, a terminal program is listening */
#define MENU_IDLE_MS (10u * 60u * 1000u)    /* no key for this long: goodbye (the clocks would keep the line busy) */
#define LOAD_CHUNK  2048       /* octets of a list read per block */

/* ---- the directory, made again a piece per block --------------------- */

static tdir_t dir;
static struct {
    int want, phase, fd;       /* phase 0 idle, 1 the boards, 2 the details */
    unsigned gen, loaded_gen;  /* bbslink's lists_gen we are making / have made */
    int has_info;
    char line[1024]; size_t n; int skip;
} ld = { 0, 0, -1, 0, ~0u, 0, "", 0, 0 };

/* ---- the menus ---------------------------------------------------------- */

static struct term {
    int       on, started, dialing, wake;
    uint32_t  online_at, key_at;
    tmenu_t   m;
    uint8_t   pend[512]; size_t pn;    /* taken from the menu, not yet by the line */
    char      dialed[24], connect[48], node[12], speed[32], user[12], hello[64], addr[16], server[16];
    char      items_hint[32];
} T[PORTS];

/* the directory is always made: 1 800 BBS DIAL shows it on every port
 * (bbslink_operator); the board details are fetched only for the menu */
void termsvc_want(int on) { ld.want = 1; bbslink_menu_want(on); }

static int any_menu(void)
{
    int k;
    for (k = 0; k < PORTS; k++) if (T[k].on) return 1;
    return 0;
}

static int load_open(const char *path)
{
    if (!path || !path[0]) return -1;
    ld.fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    ld.n = 0; ld.skip = 0;
    return ld.fd;
}

static void load_line(void)
{
    if (!ld.skip) {
        if (ld.phase == 1) tdir_bbs_line(&dir, ld.line, ld.n);
        else tdir_info_line(&dir, ld.line, ld.n);
    }
    ld.n = 0; ld.skip = 0;
}

static void load_poll(void)
{
    char buf[LOAD_CHUNK];
    ssize_t r, i;
    if (ld.phase == 0) {
        unsigned g = bbslink_lists_gen();
        /* the directory is read by every menu: made again only while nobody is in one */
        if (!ld.want || g == ld.loaded_gen || any_menu()) return;
        ld.gen = g;
        tdir_init(&dir);
        ld.has_info = 0;
        ld.phase = 1;
        if (load_open(bbslink_list_path()) < 0) ld.phase = 2;
        if (ld.phase == 2 && load_open(bbslink_info_path()) < 0) ld.phase = 3;
        return;
    }
    if (ld.phase == 3) {                       /* done: by name, once */
        tdir_sort(&dir);
        ld.loaded_gen = ld.gen;
        ld.phase = 0;
        logf_(CFG_LOG_INFO, "menu: the BBS directory has %d boards, %d with the guide's details", dir.n, dir.with_info);
        return;
    }
    r = read(ld.fd, buf, sizeof buf);
    if (r < 0 && (errno == EAGAIN || errno == EINTR)) return;
    if (r <= 0) {
        if (ld.n || ld.skip) load_line();
        close(ld.fd); ld.fd = -1;
        if (ld.phase == 2) ld.has_info = dir.with_info > 0;
        ld.phase++;
        if (ld.phase == 2 && load_open(bbslink_info_path()) < 0) ld.phase = 3;
        return;
    }
    for (i = 0; i < r; i++) {
        if (buf[i] == '\n') load_line();
        else if (ld.n < sizeof ld.line - 1) ld.line[ld.n++] = buf[i];
        else ld.skip = 1;                      /* longer than any line of the lists */
    }
}

/* ---- what the menu shows of the box ----------------------------------- */

static const char *mod_name(int m, int dotted)
{
    switch (m) {
    case V90M_CONN_V90: return dotted ? "V.90" : "V90";
    case V90M_CONN_V92: return dotted ? "V.92" : "V92";
    case V90M_CONN_X2: return "x2";
    case V90M_CONN_K56FLEX: return "K56flex";
    case V90M_CONN_V34: return dotted ? "V.34" : "V34";
    case V90M_CONN_V32BIS: return dotted ? "V.32bis" : "V32B";
    case V90M_CONN_V22BIS: return dotted ? "V.22bis" : "V22B";
    case V90M_CONN_V22: return dotted ? "V.22" : "V22";
    case V90M_CONN_BELL212A: return dotted ? "Bell 212A" : "B212";
    case V90M_CONN_V21: return dotted ? "V.21" : "V21";
    case V90M_CONN_BELL103: return dotted ? "Bell 103" : "B103";
    case V90M_CONN_V23: return dotted ? "V.23" : "V23";
    default: return "";
    }
}

const char *termsvc_where(int k)
{
    uint32_t a; int on, kind;
    if (k < 0 || k >= n_ports || !S[k].in_call) return "";
    kind = srv_link(&S[k], &a, &on);
    if (kind == SRV_LINK_MENU) return bbslink_menu_busy(k) ? "a BBS" : "the menu";
    if (kind == SRV_LINK_BBS) return T[k].on && !T[k].dialing && !bbslink_menu_busy(k) ? "1-800-BBS-DIAL" : "a BBS";
    if (kind == SRV_LINK_SVC) return "a service";
    if (kind == SRV_LINK_ACTS) return "the time service";
    if (kind != SATURN_NONE) return "a Saturn game";
    switch (S[k].cfg.service) {
    case CFG_SERVICE_ARCHIVE: return "the archive (PPP)";
    case CFG_SERVICE_DREAMCAST: return "a Dreamcast";
    default: return S[k].protoweb ? "Protoweb (PPP)" : "Internet (PPP)";
    }
}

static int busy_lines(void)
{
    int p, n = 0;
    for (p = 0; p < n_ports; p++) if (S[p].in_call) n++;
    return n;
}

/* s into d[0..cap), cut to fit */
static void fit(char *d, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(d, s, n); d[n] = '\0';
}

static int tonight(void *ctx, int i, char label[20], char value[16], int *colour)
{
    int k = (int)((struct term *)ctx - T);
    const dce_status_t *d = &S[k].ds;
    char v[48];
    switch (i) {
    case 0: fit(label, 20, "Lines busy");   snprintf(v, sizeof v, "%d of %d", busy_lines(), n_ports); *colour = TS_LG; break;
    case 1: fit(label, 20, "This call");    snprintf(v, sizeof v, "%s %d", mod_name(d->modulation, 1), d->bitrate); *colour = TS_YL; break;
    case 2: fit(label, 20, "BBS boards");
        if (dir.n >= 1000) snprintf(v, sizeof v, "%d,%03d", dir.n / 1000, dir.n % 1000); else snprintf(v, sizeof v, "%d", dir.n);
        *colour = TS_WH; break;
    case 3: fit(label, 20, "Calls so far"); snprintf(v, sizeof v, "%lu", (unsigned long)S[k].status.calls_total); *colour = TS_WH; break;
    default: return 0;
    }
    fit(value, 16, v);
    return 1;
}

static int who(void *ctx, int i, tmenu_who *r)
{
    int k = (int)((struct term *)ctx - T), p, seen = -1;
    for (p = 0; p < n_ports; p++) {
        if (!S[p].in_call) continue;
        if (++seen != i) continue;
        snprintf(r->node, sizeof r->node, "%d", p + 1);
        snprintf(r->who, sizeof r->who, "Line %d", p + 1);
        snprintf(r->where, sizeof r->where, "%s", termsvc_where(p));
        snprintf(r->speed, sizeof r->speed, "%s %d", mod_name(S[p].ds.modulation, 1), S[p].ds.bitrate);
        r->minutes = (int)(S[p].call_s / 60u);
        r->you = p == k;
        return 1;
    }
    return 0;
}

static void start(int k, uint32_t now, int op)
{
    struct term *t = &T[k];
    const dce_status_t *d = &S[k].ds;
    tmenu_cfg c;
    char a[16];
    memset(&c, 0, sizeof c);
    snprintf(t->dialed, sizeof t->dialed, "%s", srv_dialed(&S[k]));
    snprintf(t->connect, sizeof t->connect, "CONNECT %d/%s%s%s", d->bitrate, mod_name(d->modulation, 0),
             d->error_control ? "/LAPM" : "", d->comp_alg == V90M_EC_COMP_V44 ? "/V44" : d->comp_alg ? "/V42BIS" : "");
    snprintf(t->node, sizeof t->node, "Line %d", k + 1);
    if (d->rx_bitrate && d->rx_bitrate != d->bitrate)
        snprintf(t->speed, sizeof t->speed, "%s %d/%d", mod_name(d->modulation, 1), d->bitrate, d->rx_bitrate);
    else snprintf(t->speed, sizeof t->speed, "%s %d", mod_name(d->modulation, 1), d->bitrate);
    snprintf(t->user, sizeof t->user, "Line %d", k + 1);
    snprintf(t->hello, sizeof t->hello, "Hello. You are on line %d of %d of this box.", k + 1, n_ports);
    snprintf(t->addr, sizeof t->addr, "%s", ip_str(S[k].cfg.ppp_peer, a));
    snprintf(t->server, sizeof t->server, "%s", ip_str(S[k].cfg.ppp_local, a));
    if (dir.n >= 1000) snprintf(t->items_hint, sizeof t->items_hint, "%d,%03d boards to dial", dir.n / 1000, dir.n % 1000);
    else snprintf(t->items_hint, sizeof t->items_hint, "%d boards to dial", dir.n);
    c.ctx = t;
    c.title = "NETSTALGIA ONLINE";
    c.dialed = t->dialed; c.connect = t->connect; c.node = t->node; c.speed = t->speed; c.user = t->user;
    c.hello[0] = t->hello;
    c.items[0] = (tmenu_item){ '1', "Start PPP", "the Internet", TM_PPP };
    c.items[1] = (tmenu_item){ '2', "Who's online", "who is dialed in", TM_WHO };
    c.items[2] = (tmenu_item){ '3', "BBS directory", t->items_hint, TM_BBS };
    c.dir = &dir;
    c.dir_source = ld.has_info ? "from the Telnet BBS Guide" : "names and numbers";
    c.dir_note = op ? "Or dial a board's own 227 number: straight through to it."
                    : "Dial 1-800-BBS-DIAL and a board's name, or its 227 number.";
    c.operator = op;
    c.ppp_addr = t->addr; c.ppp_server = t->server; c.ppp_mtu = S[k].cfg.mru;
    c.tonight = tonight; c.who = who; c.lines = n_ports;
    tmenu_init(&t->m, &c, now);
    t->started = 1; t->key_at = now; t->pn = 0;
    logf_(CFG_LOG_INFO, op ? "port %d: menu: 1-800-BBS-DIAL" : "port %d: menu: a terminal program called", k + 1);
}

void termsvc_from_modem(void *ctx, const uint8_t *oct, size_t n)
{
    int k = (int)(((srv_t *)ctx) - S);
    struct term *t = &T[k];
    if (k < 0 || k >= PORTS) return;
    if (t->dialing) { bbslink_from_modem(ctx, oct, n); return; }
    if (!t->on) return;
    if (!t->started) { t->wake = 1; return; }  /* the menu starts at once; the key that woke it is not one */
    tmenu_input(&t->m, oct, n, t->m.now);
    t->key_at = t->m.now;
}

void termsvc_poll(uint32_t now)
{
    int k;
    load_poll();
    for (k = 0; k < n_ports; k++) {
        struct term *t = &T[k];
        uint32_t a; int online, kind = srv_link(&S[k], &a, &online), act, board, op = bbslink_operator(k);
        if (kind != SRV_LINK_MENU && !op) {    /* no menu call: or it went over to PPP */
            if (t->on) { t->on = 0; t->started = 0; t->dialing = 0; }
            continue;
        }
        if (!t->on) { t->on = 1; t->started = 0; t->dialing = 0; t->wake = 0; t->online_at = now; t->pn = 0; }
        if (!online) { t->online_at = now; continue; }
        if (!t->started) {
            /* Dial-Up Networking's first frame takes the call to PPP well within
             * this; a key from a terminal program starts the menu at once */
            if (!t->wake && now - t->online_at < GREET_MS) continue;
            start(k, now, op);
        }
        if (t->dialing) {
            if (bbslink_menu_busy(k)) continue;
            t->dialing = 0;
            srv_ppp_watch(&S[k], 1);       /* the menu's again: a PPP frame may take the call */
            tmenu_back(&t->m, now);
            t->key_at = now;
        }
        tmenu_poll(&t->m, now);
        if (now - t->key_at >= MENU_IDLE_MS) { logf_(CFG_LOG_INFO, "port %d: menu: nothing typed for 10 minutes", k + 1); srv_hangup(&S[k]); continue; }
        /* to the line, as fast as it takes it */
        for (;;) {
            size_t took;
            if (!t->pn) t->pn = tmenu_output(&t->m, t->pend, sizeof t->pend);
            if (!t->pn) break;
            took = srv_send_raw(&S[k], t->pend, t->pn);
            if (!took) break;
            memmove(t->pend, t->pend + took, t->pn - took); t->pn -= took;
        }
        if (t->pn) continue;
        act = tmenu_action(&t->m, &board);
        if (act == TM_ACT_PPP) {
            if (srv_start_ppp(&S[k]) == 0) logf_(CFG_LOG_INFO, "port %d: menu: Start PPP", k + 1);
        } else if (act == TM_ACT_HANGUP) {
            srv_hangup(&S[k]);
        } else if (act == TM_ACT_DIAL) {
            const char *login = tdir_get(&dir, board, TD_LOGIN);
            /* a board's data (a file sent to it) may hold a PPP frame's start */
            if (bbslink_menu_dial(k, login) == 0) { t->dialing = 1; srv_ppp_watch(&S[k], 0); logf_(CFG_LOG_INFO, "port %d: menu: to the board %s", k + 1, login); }
            else tmenu_back(&t->m, now);
        }
    }
}
