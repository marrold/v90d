/* v90d/cfg - the settings form: page renderer and decoder of the posted
 * body. See settings.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "sbuf.h"
#include "page.h"

#define VAL_MAX CFG_SECRET_MAX      /* the longest value any field takes */

enum {
    F_NUMBER, F_TONES, F_RINGS, F_BUSY, F_DROP, F_SESSION, F_IDLE, F_AUTH,
    F_MODS, F_V90, F_V34, F_V32BIS, F_V22BIS, F_V22, F_V21, F_V23, F_DOWN, F_UP, F_ADMINPW, F_ADMINCLR,
    F_LAN, F_ADDRESS, F_NETMASK, F_GATEWAY, F_DNS1, F_DNS2, F_PORT, F_SERVICE, F_V92, F_COMPRESSION,
    F_ENCRYPTION,
    F_DCNOW, F_DCLAN, F_DCUPNP, F_DCIWANGO, F_SATMATCH, F_UPDATE_CHECK, F_X2, F_K56,
    F_SVCNET, F_ADD, F_SVCMENU, F_DNSRELAY, F_IPV6,
    F_COUNT,
    F_USER = 100, F_PASS = 200,     /* plus the row */
    F_SVC = 300                     /* plus 100 * the S_ column, plus the row */
};
/* the columns of the service rows */
enum { S_NUM, S_HOST, S_PORT, S_MODE, S_NAME, S_COLS };
static const char *const svc_col[S_COLS] = { "snum", "shost", "sport", "smode", "sname" };
/* name on the wire, and the config section for messages */
typedef char fields_fit_the_seen_bits[F_COUNT <= 64 ? 1 : -1];
static const struct { const char *name; const char *sec; } fields[F_COUNT] = {
    { "number", "line" }, { "tones", "line" }, { "rings", "modem" }, { "busy_chance", "line" },
    { "drop_mean", "line" }, { "session_limit", "line" }, { "idle_timeout", "line" },
    { "auth", "ppp" },
    { "mods", "modem" }, { "mod_v90", "modem" }, { "mod_v34", "modem" }, { "mod_v32bis", "modem" },
    { "mod_v22bis", "modem" }, { "mod_v22", "modem" },
    { "mod_v21", "modem" }, { "mod_v23", "modem" }, { "max_downstream", "modem" }, { "max_upstream", "modem" },
    { "admin_password", "status" }, { "admin_clear", "status" },
    { "lan", "network" }, { "address", "network" }, { "netmask", "network" },
    { "gateway", "network" }, { "dns1", "network" }, { "dns2", "network" },
    { "port", "line" }, { "service", "line" }, { "v92", "modem" }, { "compression", "ppp" },
    { "encryption", "ppp" },
    { "dreamcast_now", "line" }, { "dreamcast_lan", "line" }, { "dreamcast_upnp", "line" }, { "dreamcast_iwango", "line" }, { "saturn_matchmaking", "line" },
    { "update_check", "system" }, { "x2", "modem" }, { "k56", "modem" },
    { "netstalgia", "services" }, { "add", "status" }, { "menu", "services" }, { "dns_relay", "network" }, { "ipv6", "network" },
};

static const char *const n_tones[] = { "us", "eu" };
static const char *const t_tones[] = { "North America", "Europe" };
static const char *const n_auth[]  = { "none", "pap", "chap" };
static const char *const t_auth[]  = { "none", "PAP", "CHAP" };
static const char *const n_lan[]   = { "static", "dhcp" };      /* the value of lan_dhcp */
static const char *const t_lan[]   = { "Static address", "DHCP" };
static const char *const n_service[] = { "internet", "archive", "dreamcast", "saturn" };
static const char *const t_service[] = { "Internet: as it is", "Archive: the old web (Protoweb)", "Dreamcast: for the game console",
                                         "Saturn: console to console (NetLink)" };
static const char *const n_v92[]   = { "off", "on" };
static const char *const t_dcnow[]  = { "Off", "On: tell the Dreamcast Now site that a console is online and which game server it asked for" };
static const char *const t_dclan[]  = { "Off: the console is behind this box's address", "On: the console gets an address of its own on the network" };
static const char *const t_dcupnp[] = { "Off", "On: ask the router to send the game ports here (UPnP)" };
static const char *const t_dciwango[] = { "Off", "On: reach the IWANGO games' servers through the VPN (needs a profile, below)" };
static const char *const t_satmatch[] = { "Off: direct only (dial 0 to wait, #a*b*c*d# to call an address)",
                                          "On: pair players through saturn.dreampipe.net (dial #0NN# and #NN)" };
static const char *const t_v92[]   = { "Off: V.90 upstream (V.34, up to 33600 bit/s)",
                                       "On: offer PCM upstream to V.92 callers" };
static const char *const t_x2[]    = { "Off",
                                       "On: offer x2 to a U.S. Robotics caller (needs V.34; one heavy call at a time)" };
static const char *const t_k56[]   = { "Off",
                                       "On (experimental): offer K56flex to a caller whose modem does it (needs V.34; one heavy call at a time)" };
static const char *const t_update[] = { "Off: the box never contacts GitHub",
                                        "On: check GitHub once a day and offer a newer release on the Firmware page" };
static const char *const n_comp[]  = { "off", "on" };
static const char *const t_comp[]  = { "Off: no PPP compression",
                                       "On: Deflate or MPPC, whichever the caller asks for" };
static const char *const t_enc[]   = { "Off: no encryption required",
                                       "On: require 128-bit MPPE (forces MS-CHAPv2; a user is needed, and plain compression is dropped)" };
static const char *const t_mods[]  = { "V.90", "V.34", "V.32bis", "V.22bis", "V.22", "V.21", "V.23" };     /* bit i */
static const char *const n_svcmode[] = { "raw", "telnet" };        /* CFG_SVC_* */
static const char *const t_svcnet[] = { "Off: only the numbers above",
                                        "On: also Netstalgia's numbers, fetched once a day" };
static const char *const t_ipv6[] = { "Off: IPv4 only",
                                      "On: an address from your network's router, when it offers IPv6" };
static const char *const t_dnsrelay[] = { "Off: the callers ask the servers below themselves",
                                          "On: the box answers the callers' names, asking the servers below" };
static const char *const t_svcmenu[] = { "Off: every call on the Internet service is PPP",
                                         "On: a terminal program gets the menu first" };

/* ---- the page -------------------------------------------------------------- */

/* length of the string in z[0..max), which may lack its NUL */
static size_t len_of(const char *z, size_t max)
{
    const char *e = memchr(z, '\0', max);
    return e ? (size_t)(e - z) : max;
}

/* 1 if a browser that got these octets in a UTF-8 page sends the same
 * octets back: well-formed UTF-8 (shortest form, no surrogates, at most
 * U+10FFFF) without control characters */
static int form_safe(const char *z, size_t n)
{
    const unsigned char *p = (const unsigned char *)z;
    size_t i = 0, k, need;

    while (i < n) {
        uint32_t cp = p[i], min;
        if (cp < 0x20 || cp == 0x7f) return 0;
        if (cp < 0x80) { i++; continue; }
        if (cp >= 0xc2 && cp <= 0xdf)      { need = 1; cp &= 0x1f; min = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { need = 2; cp &= 0x0f; min = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { need = 3; cp &= 0x07; min = 0x10000; }
        else return 0;
        if (n - i <= need) return 0;
        for (k = 1; k <= need; k++) {
            if ((p[i + k] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (p[i + k] & 0x3fu);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
        i += need + 1;
    }
    return 1;
}

static void row_open(sbuf_t *b, const char *label)
{
    sb_puts(b, "<tr><th>"); sb_puts(b, label); sb_puts(b, "</th><td>");
}

static void row_close(sbuf_t *b, const char *note)
{
    if (note[0]) { sb_puts(b, "<small>"); sb_puts(b, note); sb_puts(b, "</small>"); }
    sb_puts(b, "</td></tr>\n");
}

/* <input type=.. name=..[row] size=.. maxlength=..   (left open) */
/* a row number that is written "IDX": the template of a new row, which the
 * page's script copies with the next number in its place (/ui.js) */
#define ROW_TEMPLATE (-2)

static void in_open(sbuf_t *b, const char *type, const char *name, int row, int size, int maxlen)
{
    sb_puts(b, "<input type=\""); sb_puts(b, type);
    sb_puts(b, "\" name=\""); sb_puts(b, name);
    if (row == ROW_TEMPLATE) sb_puts(b, "IDX");
    else if (row >= 0) sb_u64(b, (uint64_t)row);
    sb_puts(b, "\" size=\""); sb_u64(b, (uint64_t)size);
    sb_puts(b, "\" maxlength=\""); sb_u64(b, (uint64_t)maxlen);
    sb_putc(b, '"');
}

static void in_int(sbuf_t *b, const char *label, int f, int v, const char *note)
{
    row_open(b, label);
    in_open(b, "text", fields[f].name, -1, 6, 5);
    sb_puts(b, " value=\""); sb_i32(b, v); sb_puts(b, "\">");
    row_close(b, note);
}

/* an IPv4 address; 0 is shown as an empty field */
static void in_addr(sbuf_t *b, const char *label, int f, uint32_t a, const char *note)
{
    row_open(b, label);
    in_open(b, "text", fields[f].name, -1, 16, 15);
    sb_puts(b, " value=\""); if (a) sb_ip(b, a); sb_puts(b, "\">");
    row_close(b, note);
}

static void in_password(sbuf_t *b, const char *name, int row)
{
    in_open(b, "password", name, row, 24, CFG_SECRET_MAX);
    sb_puts(b, " autocomplete=\"new-password\">");
}

static void in_check(sbuf_t *b, int f, int on, const char *text)
{
    sb_puts(b, "<label><input type=\"checkbox\" name=\""); sb_puts(b, fields[f].name);
    sb_puts(b, on ? "\" value=\"1\" checked> " : "\" value=\"1\"> ");
    sb_puts(b, text); sb_puts(b, "</label>");
}

static void in_select(sbuf_t *b, const char *label, int f, int v, const char *const *names,
                      const char *const *texts, int count)
{
    int i;
    row_open(b, label);
    sb_puts(b, "<select name=\""); sb_puts(b, fields[f].name); sb_puts(b, "\">");
    for (i = 0; i < count; i++) {
        sb_puts(b, "<option value=\""); sb_puts(b, names[i]);
        sb_puts(b, i == v ? "\" selected>" : "\">");
        sb_puts(b, texts[i]); sb_puts(b, "</option>");
    }
    sb_puts(b, "</select>");
    row_close(b, "");
}

static const struct { const char *path, *title; } pages[SETTINGS_PAGES] = {
    { "/settings", "settings" }, { "/line", "line" }, { "/modem", "modem" }, { "/users", "users" },
    { "/network", "network" }, { "/system", "system" },
    { "/line2", "line, port 2" }, { "/modem2", "modem, port 2" },
    { "/dreamcast", "Consoles" }, { "/dreamcast2", "Consoles, port 2" },
    { "/services", "services" },
};

const char *settings_page_path(int page)
{
    return pages[page >= 0 && page < SETTINGS_PAGES ? page : 0].path;
}

int settings_page_port2(int page)
{
    return page == SETTINGS_LINE2 || page == SETTINGS_MODEM2 || page == SETTINGS_CONSOLES2;
}

static int two_ports = 1;       /* the box has two FXS ports unless told otherwise */
void settings_set_two_ports(int two) { two_ports = two != 0; }

int settings_page_of(const char *path, size_t len)
{
    int i;
    for (i = 0; i < SETTINGS_PAGES; i++)
        if (strlen(pages[i].path) == len && memcmp(pages[i].path, path, len) == 0) {
            if (!two_ports && (i == SETTINGS_LINE2 || i == SETTINGS_MODEM2 || i == SETTINGS_CONSOLES2)) return -1;
            return i;
        }
    return -1;
}

static settings_svc_rows_fn svc_rows;
void settings_set_svc_rows(settings_svc_rows_fn fn) { svc_rows = fn; }

static char vpn_remote[48]; static int vpn_stored;
void settings_set_vpn(const char *remote)
{
    vpn_stored = remote != NULL;
    snprintf(vpn_remote, sizeof vpn_remote, "%s", remote ? remote : "");
}

/* empty rows to show on the Users and Numbers pages beyond those that are
 * always there (settings_render_page_add) */
static int add_rows;

int settings_render_html(const cfg_t *c, int saved, const char *error, char *buf, size_t cap)
{
    return settings_render_page(c, SETTINGS_ALL, saved, error, buf, cap);
}

/* one row of the users' table: user i (name[0..nl)), or a new one ("New") */
static void user_row(sbuf_t *b, int i, const char *name, size_t nl, int used)
{
    sb_puts(b, used ? "<tr><td>" : "<tr class=\"new\"><td>");
    if (used) sb_u64(b, (uint64_t)i + 1u); else sb_puts(b, "New");
    sb_puts(b, "</td><td>");
    if (form_safe(name, nl)) {
        in_open(b, "text", "user", i, 24, CFG_NAME_MAX);
        sb_puts(b, " value=\""); sb_html(b, name, nl, 1); sb_puts(b, "\">");
    } else {
        sb_html(b, name, nl, 0);
        sb_puts(b, " <i>(change it in the config file)</i>");
    }
    sb_puts(b, "</td><td>");
    in_password(b, "pass", i);
    sb_puts(b, "</td></tr>\n");
}

/* one row of the numbers' table: e, or a new one (e NULL) */
static void svc_row(sbuf_t *b, int i, const cfg_service_t *e)
{
    int m;
    sb_puts(b, e ? "<tr><td>" : "<tr class=\"new\"><td>");
    if (e) sb_u64(b, (uint64_t)i + 1u); else sb_puts(b, "New");
    sb_puts(b, "</td><td>");
    in_open(b, "text", svc_col[S_NUM], i, 13, CFG_SVC_NUMBER_MAX);
    sb_puts(b, " value=\""); if (e) sb_html(b, e->number, sizeof e->number, 0); sb_puts(b, "\"></td><td>");
    in_open(b, "text", svc_col[S_HOST], i, 18, CFG_SVC_HOST_MAX);
    sb_puts(b, " value=\""); if (e) sb_html(b, e->host, sizeof e->host, 0); sb_puts(b, "\"></td><td>");
    in_open(b, "text", svc_col[S_PORT], i, 5, 5);
    sb_puts(b, " value=\""); if (e) sb_u64(b, e->port); sb_puts(b, "\"></td><td>");
    sb_puts(b, "<select name=\"smode");
    if (i == ROW_TEMPLATE) sb_puts(b, "IDX"); else sb_u64(b, (uint64_t)i);
    sb_puts(b, "\">");
    for (m = 0; m < 2; m++) {
        sb_puts(b, "<option value=\""); sb_puts(b, n_svcmode[m]);
        sb_puts(b, e && e->mode == m ? "\" selected>" : "\">"); sb_puts(b, n_svcmode[m]); sb_puts(b, "</option>");
    }
    sb_puts(b, "</select></td><td>");
    in_open(b, "text", svc_col[S_NAME], i, 18, CFG_SVC_NAME_MAX);
    sb_puts(b, " value=\""); if (e) sb_html(b, e->name, sizeof e->name, 0); sb_puts(b, "\"></td></tr>\n");
}

/* the Add button: with the script (/ui.js) it puts a copy of the template
 * tpl into the table body rows, up to max rows, without leaving the page;
 * without it, it posts the form and the page comes back with a new row */
static void add_button(sbuf_t *b, const char *tpl, const char *rows, int max)
{
    sb_puts(b, "<p><input type=\"submit\" name=\"add\" value=\"Add\" data-tpl=\""); sb_puts(b, tpl);
    sb_puts(b, "\" data-rows=\""); sb_puts(b, rows);
    sb_puts(b, "\" data-max=\""); sb_u64(b, (uint64_t)max); sb_puts(b, "\"></p>\n");
}

int settings_render_page_add(const cfg_t *c, int page, int saved, int add, const char *error, char *buf, size_t cap)
{
    int r;
    add_rows = add ? 1 : 0;
    r = settings_render_page(c, page, saved, error, buf, cap);
    add_rows = 0;
    return r;
}

int settings_render_page(const cfg_t *c, int page, int saved, const char *error, char *buf, size_t cap)
{
#define ON(p) (page == SETTINGS_ALL || page == (p))
    sbuf_t b;
    cfg_t two;
    int i, n, own = page, port2 = 0;

    sb_init(&b, buf, cap);
    if (page < 0 || page >= SETTINGS_PAGES) page = own = SETTINGS_ALL;
    page_consoles_tab = c->service == CFG_SERVICE_DREAMCAST || c->port2.service == CFG_SERVICE_DREAMCAST
                      || c->service == CFG_SERVICE_SATURN || c->port2.service == CFG_SERVICE_SATURN;
    if (page == SETTINGS_LINE2 || page == SETTINGS_MODEM2 || page == SETTINGS_CONSOLES2) {
        /* port 2: the same sections, filled from its settings */
        port2 = 1;
        cfg_for_port(c, 1, &two); c = &two;
        page = page == SETTINGS_LINE2 ? SETTINGS_LINE : page == SETTINGS_MODEM2 ? SETTINGS_MODEM
             : SETTINGS_CONSOLES;
    }
    page_top(&b, pages[own].title, "",
             page == SETTINGS_CONSOLES ? PAGE_TAB_CONSOLES : page == SETTINGS_SERVICES ? PAGE_TAB_SERVICES
             : page ? page : -1, NULL);
    if (error) {
        sb_puts(&b, "<p><b>Not saved:</b> ");
        sb_html(&b, error, SETTINGS_MSG_MAX, 0);
        sb_puts(&b, "</p>\n");
    } else if (saved) {
        sb_puts(&b, "<p><b>Settings saved.</b></p>\n");
    }
    if (two_ports && (page == SETTINGS_LINE || page == SETTINGS_MODEM || page == SETTINGS_CONSOLES)) {
        const char *base = pages[page].path;
        sb_puts(&b, "<p class=\"sw\">");
        if (port2) { sb_puts(&b, "<a href=\""); sb_puts(&b, base); sb_puts(&b, "\">Port 1</a> <b>Port 2</b>"); }
        else { sb_puts(&b, "<b>Port 1</b> <a href=\""); sb_puts(&b, base); sb_puts(&b, "2\">Port 2</a>"); }
        sb_puts(&b, "</p>\n");
    }
    sb_puts(&b, "<form method=\"post\" action=\""); sb_puts(&b, pages[own].path); sb_puts(&b, "\">\n");
    /* Enter in a field presses the form's first button: a Save first, out of
     * sight, so that it is never the Add button of a table */
    if (ON(SETTINGS_USERS) || page == SETTINGS_SERVICES)
        sb_puts(&b, "<input type=\"submit\" value=\"Save\" class=\"def\" tabindex=\"-1\" aria-hidden=\"true\">\n");
    if (port2) {
        sb_puts(&b, "<input type=\"hidden\" name=\""); sb_puts(&b, fields[F_PORT].name);
        sb_puts(&b, "\" value=\"2\">\n");
    }

    if (ON(SETTINGS_LINE)) {
    sb_puts(&b, "<section><h2>Telephone line</h2>\n<table>\n");
    row_open(&b, "Phone number");
    in_open(&b, "text", fields[F_NUMBER].name, -1, 22, CFG_NUMBER_MAX);
    sb_puts(&b, " value=\""); sb_html(&b, c->line_number, sizeof c->line_number, 0); sb_puts(&b, "\">");
    row_close(&b, "digits, * and #; empty: every number connects");
    in_select(&b, "Service", F_SERVICE, c->service, n_service, t_service, CFG_SERVICE_COUNT);
    in_select(&b, "Tones", F_TONES, c->line_tones, n_tones, t_tones, 2);
    in_int(&b, "Rings before answer", F_RINGS, c->rings, "1 to 10");
    in_int(&b, "Busy signal", F_BUSY, c->busy_chance, "percent of calls, 0 to 100");
    in_int(&b, "Line drop", F_DROP, c->drop_mean_min,
           "mean minutes until the line drops at random, 0 = never");
    in_int(&b, "Session limit", F_SESSION, c->session_limit_min, "minutes, 0 = none");
    in_int(&b, "Idle timeout", F_IDLE, c->idle_timeout_min, "minutes without traffic, 0 = none");
    sb_puts(&b, "</table>\n<p>Service says what a caller on this port gets. Archive: every web"
                " (HTTP) and FTP address the caller opens is fetched from Protoweb, the archive of"
                " the web as it was, with no proxy to set on the caller's computer; mail, chat and"
                " secure sites do not work. Dreamcast: set the console to dial this port's number"
                " with a user name and password of the Users page; PPP compression, PPP encryption and V.92 are off"
                " for this port and the console is given the name servers of the game revival"
                " projects, whatever the other pages say. Its further switches are on the Consoles page (under Services). Saturn: two consoles that telephone each other, each on a box of its own; dial 0 to wait for the other player, #a*b*c*d# to call the one at that address. The last two have not been tried with a real"
                " console. A change takes effect when the lines are free.</p></section>\n");

    }
    if (ON(SETTINGS_CONSOLES)) {
    sb_puts(&b, "<section><h2>Dreamcast</h2>\n<table>\n");
    in_select(&b, "Dreamcast Now", F_DCNOW, c->dc_now, n_v92, t_dcnow, 2);
    in_select(&b, "Console on the network", F_DCLAN, c->dc_lan, n_v92, t_dclan, 2);
    in_select(&b, "Game ports from the router", F_DCUPNP, c->dc_upnp, n_v92, t_dcupnp, 2);
    in_select(&b, "IWANGO games", F_DCIWANGO, c->dc_iwango, n_v92, t_dciwango, 2);
    sb_puts(&b, "</table>\n<p>");
    if (page == SETTINGS_CONSOLES)
        sb_puts(&b, c->service == CFG_SERVICE_DREAMCAST ? "The service of this port is Dreamcast: these switches are in force. "
                                                         : "The service of this port is not Dreamcast (Line tab): these switches do nothing until it is. ");
    sb_puts(&b, "They only act on a port whose service is Dreamcast. Dreamcast Now sends a report to"
                " dcnow-2016.appspot.com every 15 seconds during a call (a hash of this box's hardware address"
                " and a hash of the game server's name), which is how that site shows who is online. Console on"
                " the network: the box asks the network's DHCP server for a second address and hands it to the"
                " console, which is then a host of its own and not behind this box's address. Game ports from"
                " the router: the network's router is asked by UPnP to pass the games' ports on, to the console"
                " when it has an address of its own, else to this box. IWANGO games (Sega Tetris, Aero Dancing, Golf Shiyouyo 2, Daytona USA): their servers of today stand"
                " inside a private network that is reached through an OpenVPN tunnel; with the switch on and a profile"
                " for that network stored (below), the box keeps the tunnel up. All four are off unless turned on. A"
                " change takes effect when the lines are free.</p></section>\n");
    }
    if (ON(SETTINGS_CONSOLES)) {
    sb_puts(&b, "<section><h2>Saturn</h2>\n<table>\n");
    in_select(&b, "Matchmaking", F_SATMATCH, c->sat_match, n_v92, t_satmatch, 2);
    sb_puts(&b, "</table>\n<p>");
    if (page == SETTINGS_CONSOLES)
        sb_puts(&b, c->service == CFG_SERVICE_SATURN ? "The service of this port is Saturn: this switch is in force. "
                                                      : "The service of this port is not Saturn (Line tab): this switch does nothing until it is. ");
    sb_puts(&b, "Direct play always works: the waiting player dials 0, the other dials #a*b*c*d# with"
                " that player's address (the waiting box must be reachable on TCP 65432 and UDP 20001)."
                " Matchmaking, off unless turned on, instead pairs two players who dial the same game"
                " through the match server at saturn.dreampipe.net, and lets them find each other through"
                " home routers (STUN). With it on, dial #0NN# to wait for game NN or #NN to be paired as"
                " the caller. Turning it on makes this box a client of that server; no address of yours is"
                " sent but your game choice and public address are. A change takes effect at the next call.</p></section>\n");
    }
    if (ON(SETTINGS_MODEM)) {
    sb_puts(&b, "<section><h2>Modem</h2>\n<table>\n");
    row_open(&b, "Modulations");
    sb_puts(&b, "<input type=\"hidden\" name=\""); sb_puts(&b, fields[F_MODS].name);
    sb_puts(&b, "\" value=\"1\">");
    for (i = 0; i < CFG_MOD_COUNT; i++) {
        if (i) sb_putc(&b, ' ');
        in_check(&b, F_V90 + i, (c->modulations >> i) & 1u, t_mods[i]);
    }
    row_close(&b, "");
    in_int(&b, "Max downstream", F_DOWN, c->max_down, "bit/s, 300 to 56000");
    in_int(&b, "Max upstream", F_UP, c->max_up, "bit/s, 300 to 48000");
    in_select(&b, "V.92", F_V92, c->v92, n_v92, t_v92, 2);
    in_select(&b, "x2", F_X2, c->x2, n_v92, t_x2, 2);
    in_select(&b, "K56flex", F_K56, c->k56, n_v92, t_k56, 2);
    in_select(&b, "PPP compression", F_COMPRESSION, c->compression, n_comp, t_comp, 2);
    in_select(&b, "PPP encryption", F_ENCRYPTION, c->encryption, n_comp, t_enc, 2);
    sb_puts(&b, "</table>\n<p>With V.92 on, PCM upstream is used when the caller's modem asks for it (for"
                " example AT+PIG=0 on Conexant modems); set Max upstream above 33600 to let"
                " it go faster. A caller that does not ask gets V.90. PPP compression (for both ports) works on"
                " top of the modem's own: web pages and text arrive about three times as fast"
                " as the line rate. PPP encryption is set per port: on, this port requires MS-CHAPv2 and"
                " 128-bit MPPE, drops plain compression, and refuses a caller that will not encrypt.</p></section>\n");

    }
    if (ON(SETTINGS_USERS)) {
    sb_puts(&b, "<section><h2>PPP login</h2>\n<table>\n");
    in_select(&b, "Authentication", F_AUTH, c->auth, n_auth, t_auth, 3);
    sb_puts(&b, "</table>\n<div class=\"x\"><table>\n"
                "<thead><tr><th>#</th><th>User name</th><th>Password</th></tr></thead>\n<tbody id=\"urows\">\n");
    n = c->n_users < 0 ? 0 : c->n_users > CFG_MAX_USERS ? CFG_MAX_USERS : c->n_users;
    /* the users there are, and an empty row when there is none yet or Add
     * asked for one */
    for (i = 0; i < CFG_MAX_USERS && i < n + (n == 0) + add_rows; i++) {
        const char *name = i < n ? c->users[i].name : "";
        user_row(&b, i, name, len_of(name, sizeof c->users[i].name), i < n);
    }
    sb_puts(&b, "</tbody></table></div>\n");
    if (n < CFG_MAX_USERS) add_button(&b, "utpl", "urows", CFG_MAX_USERS);
    sb_puts(&b, "<p>");
    if (n >= CFG_MAX_USERS) sb_puts(&b, "There are 16 users, as many as the box takes. ");
    else sb_puts(&b, "Add gives the table an empty row for a new user; Save keeps what you fill in. ");
    sb_puts(&b, "To remove one, empty its name and save. Passwords are not shown: leave the"
                " field empty to keep the one a user has, type a new one to change it.</p></section>\n");

    }
    if (ON(SETTINGS_NETWORK)) {
    sb_puts(&b, "<section><h2>Ethernet</h2>\n<table>\n");
    row_open(&b, "Address from");
    sb_puts(&b, "<select name=\"lan\">");
    for (i = 1; i >= 0; i--) {
        sb_puts(&b, "<option value=\""); sb_puts(&b, n_lan[i]);
        sb_puts(&b, c->lan_dhcp == i ? "\" selected>" : "\">");
        sb_puts(&b, t_lan[i]); sb_puts(&b, "</option>");
    }
    sb_puts(&b, "</select>");
    row_close(&b, "");
    in_addr(&b, "Address", F_ADDRESS, c->lan_addr, "static only");
    in_addr(&b, "Netmask", F_NETMASK, c->lan_netmask, "static only");
    in_addr(&b, "Gateway", F_GATEWAY, c->lan_gateway, "static only; empty: none");
    in_select(&b, "IPv6", F_IPV6, c->ipv6, n_v92, t_ipv6, 2);
    in_select(&b, "Callers' names", F_DNSRELAY, c->dns_relay, n_v92, t_dnsrelay, 2);
    in_addr(&b, "DNS server 1", F_DNS1, c->dns[0], "empty: none");
    in_addr(&b, "DNS server 2", F_DNS2, c->dns[1], "both empty, with the box answering: the network's own");
    sb_puts(&b, "</table>\n<p>The Ethernet settings are used from the next restart on. Whatever is"
                " set here, the box also answers on " PAGE_RESCUE_IP " (put the computer on"
                " 169.254.x.x, netmask 255.255.0.0).</p>\n<p>With the box answering the callers'"
                " names, a caller's name server is the box's own dial-up address. It asks the servers"
                " above (or, with both empty, the ones your network gave the box) and answers no"
                " IPv6 addresses: the dial-up link carries IPv4 only, and a caller with an IPv6 stack"
                " (Windows XP with IPv6 installed) would otherwise try addresses it cannot reach.</p></section>\n");

    }
    if (ON(SETTINGS_SYSTEM)) {
    sb_puts(&b, "<section><h2>Software updates</h2>\n<table>\n");
    in_select(&b, "Check for updates", F_UPDATE_CHECK, c->update_check, n_v92, t_update, 2);
    sb_puts(&b, "</table>\n<p>When on, the box asks GitHub once a day whether a newer v90d"
                " release exists and, if there is, offers it on the Firmware page. Nothing is"
                " installed without your click. Off: the box never contacts GitHub.</p></section>\n");
    sb_puts(&b, "<section><h2>Login</h2>\n<table>\n");
    row_open(&b, "Admin password");
    in_password(&b, fields[F_ADMINPW].name, -1);
    row_close(&b, strcmp(c->admin_password, "admin") == 0
        ? "This is still the factory password (admin): set another one. The user name is admin."
        : c->admin_password[0]
        ? "A password is set (user name admin). Empty: keep it."
        : "No password is set: anyone on the LAN can change these settings."
          " With one, the user name is admin.");
    row_open(&b, "Remove");
    in_check(&b, F_ADMINCLR, 0, "remove the admin password");
    row_close(&b, "");
    sb_puts(&b, "</table></section>\n");
    }
    if (page == SETTINGS_SERVICES) {
    sb_puts(&b, "<section><h2>Numbers to services</h2>\n<div class=\"x\"><table>\n"
                "<thead><tr><th>#</th><th>Number</th><th>Host</th><th>Port</th><th>Mode</th><th>Name</th></tr></thead>\n"
                "<tbody id=\"srows\">\n");
    n = c->n_services < 0 ? 0 : c->n_services > CFG_MAX_SERVICES ? CFG_MAX_SERVICES : c->n_services;
    for (i = 0; i < CFG_MAX_SERVICES && i < n + (n == 0) + add_rows; i++)
        svc_row(&b, i, i < n ? &c->services[i] : NULL);
    sb_puts(&b, "</tbody></table></div>\n");
    if (n < CFG_MAX_SERVICES) add_button(&b, "stpl", "srows", CFG_MAX_SERVICES);
    sb_puts(&b, "<table>\n");
    in_select(&b, "Netstalgia's numbers", F_SVCNET, c->svc_netstalgia, n_v92, t_svcnet, 2);
    in_select(&b, "Terminal menu", F_SVCMENU, c->svc_menu, n_v92, t_svcmenu, 2);
    sb_puts(&b, "</table>\n<p>A caller who dials one of these numbers, on either port and whatever its"
                " service, is connected straight to the host: no PPP, the call carries what the modems"
                " send. Raw passes the octets both ways as they are, for a game or program with a protocol"
                " of its own. Telnet says <code>Calling</code> and the name, then speaks telnet, for a"
                " board or text service. The call ends when the host hangs up or does not answer."
                " A number is 3 to 15 digits as the caller dials it (an 11-digit number with a 1 in front also"
                " answers without the 1); it may not be a port's own number, a BBS number or the time service's"
                " (303 494 4774). The host may be"
                " on your network. A row without a number is not used.</p>\n"
                "<p>The terminal menu: a caller on a port's Internet number who uses a terminal program"
                " (HyperTerminal, Telix, minicom) gets a menu in ANSI colour or plain text: start PPP on the"
                " same call, see who is on the lines, and the BBS directory, whose boards it connects to."
                " Dial-Up Networking callers are not affected: their first PPP frame takes them straight to"
                " PPP. A PPP program that waits for the box to speak first needs it off.</p></section>\n");
    }
    /* Save, inside the last card of the form */
    {
        static const char end[] = "</section>\n";
        size_t k = sizeof end - 1;
        /* the card's close, or (the page cut off) as much of it as was written */
        if (b.ovf) { while (k && !(b.len >= k && memcmp(b.p + b.len - k, end, k) == 0)) k--; }
        else if (!(b.len >= k && memcmp(b.p + b.len - k, end, k) == 0)) k = 0;
        b.len -= k;
        sb_puts(&b, "<p class=\"act\"><input type=\"submit\" value=\"Save\"></p></section>\n</form>\n");
    }
    /* the new rows the Add button copies (outside the form: never posted) */
    if (ON(SETTINGS_USERS)) {
        sb_puts(&b, "<template id=\"utpl\">"); user_row(&b, ROW_TEMPLATE, "", 0, 0); sb_puts(&b, "</template>\n");
    }
    if (page == SETTINGS_SERVICES) {
        sb_puts(&b, "<template id=\"stpl\">"); svc_row(&b, ROW_TEMPLATE, NULL); sb_puts(&b, "</template>\n");
    }
    if (page == SETTINGS_CONSOLES) {
        /* one profile for the box, whichever port's page this is; posted as
         * it is (text/plain), since it is longer than a form field may be */
        sb_puts(&b, "<section><h2>VPN profile for the IWANGO games</h2>\n"
                    "<form method=\"post\" action=\"/dreamcast/vpn\" enctype=\"text/plain\">\n<p>");
        if (vpn_stored) { sb_puts(&b, "A profile is stored (server "); sb_html(&b, vpn_remote, sizeof vpn_remote, 0); sb_puts(&b, ")."); }
        else sb_puts(&b, "No profile is stored.");
        sb_puts(&b, " It is an OpenVPN client profile (.ovpn) with its certificates and keys inside it, from whoever"
                    " runs the network the games' servers are in; this firmware does not come with one. Paste it"
                    " here. It is checked, kept with the settings, and not shown again. One profile serves both"
                    " ports.</p>\n<p><textarea name=\"profile\" rows=\"8\" cols=\"70\" spellcheck=\"false\"></textarea></p>\n"
                    "<p class=\"act\"><input type=\"submit\" value=\"Store the profile\"></p>\n</form>\n");
        if (vpn_stored)
            sb_puts(&b, "<form method=\"post\" action=\"/dreamcast/vpn\" enctype=\"text/plain\">\n"
                        "<p class=\"act\"><input type=\"hidden\" name=\"remove\" value=\"1\">"
                        "<input type=\"submit\" value=\"Remove the stored profile\"></p>\n</form>\n");
        sb_puts(&b, "</section>\n");
    }
    if (page == SETTINGS_SERVICES) {
#define SVC_NOTE "<p>The list is Netstalgia's, signed by it; the box fetches it once a day while the switch" \
                 " above is on. Your own numbers come first when a number is on both.</p></section>\n"
        /* room kept for what follows the rows */
        static const char tail[] = "</table></div>\n<p>... and 00000 more.</p>" SVC_NOTE PAGE_END;
        sb_puts(&b, "<section><h2>Netstalgia's numbers</h2>\n");
        if (!c->svc_netstalgia) sb_puts(&b, "<p>Off: the box does not answer these.</p>\n");
        if (svc_rows && b.cap > b.len + sizeof tail + 128) {
            int shown = 0, left = 0;
            sb_puts(&b, "<div class=\"x\"><table>\n<tr><th>Number</th><th>Name</th><th>Mode</th><th>Host</th></tr>\n");
            b.len += svc_rows(b.p + b.len, b.cap - b.len - sizeof tail, &shown, &left);
            if (!shown) sb_puts(&b, "<tr><td colspan=\"4\">None yet.</td></tr>\n");
            sb_puts(&b, "</table></div>\n");
            if (left) { sb_puts(&b, "<p>... and "); sb_u64(&b, (uint64_t)left); sb_puts(&b, " more.</p>"); }
        } else if (!svc_rows) sb_puts(&b, "<p>No list.</p>\n");
        sb_puts(&b, SVC_NOTE);
#undef SVC_NOTE
    }
    if (ON(SETTINGS_SYSTEM))
        sb_puts(&b, "<form method=\"post\" action=\"/restart\">\n"
                    "<section><h2>Restart</h2>\n<p>Restarts the box. A call in progress is cut off;"
                    " the box is back in about 20 seconds.</p>\n"
                    "<p><input type=\"submit\" value=\"Restart\"></p></section>\n"
                    "</form>\n");
    sb_puts(&b, PAGE_END);
#undef ON
    return sb_end(&b);
}

/* ---- the decoder ----------------------------------------------------------- */

typedef struct {
    cfg_t      *out;
    uint64_t    seen;                       /* bit per fixed field (F_COUNT of them: at most 64) */
    unsigned    user_seen, pass_seen;       /* bit per row */
    cfg_user_t  row[CFG_MAX_USERS];         /* names and passwords as posted */
    unsigned    svc_seen[S_COLS];           /* bit per service row */
    char        svc[S_COLS][CFG_MAX_SERVICES][VAL_MAX + 1];   /* the service rows as posted */
    int         sw[F_COUNT];                /* the 0/1 fields: -1 = not posted */
    char        admin_pw[CFG_SECRET_MAX + 1];
    int         invalid;
    int         port2;                      /* the line and modem fields are port 2's */
    int         add;                        /* the Add button: a new empty row after saving */
    int         bad;                        /* not a form of ours after all */
    char        msg[SETTINGS_MSG_MAX];
} form_t;

/* the config section a field belongs to, for messages */
static const char *sec_of(const form_t *f, int fi)
{
    const char *s = fields[fi].sec;
    if (!f->port2) return s;
    if (strcmp(s, "line") == 0) return "line2";
    if (strcmp(s, "modem") == 0) return "modem2";
    return s;
}

/* the config key a field sets, for messages: port 2's own key where it has
 * one outside [line2] and [modem2] */
static const char *key_of(const form_t *f, int fi)
{
    if (f->port2 && fi == F_ENCRYPTION) return "encryption2";
    return fields[fi].name;
}

/* note the first thing that is wrong with a value */
static void fail(form_t *f, const char *fmt, ...)
{
    va_list ap;
    if (f->invalid) return;
    f->invalid = 1;
    va_start(ap, fmt);
    vsnprintf(f->msg, sizeof f->msg, fmt, ap);
    va_end(ap);
}

/* a value for a message: at most 40 octets, anything unprintable as '?' */
static const char *safe(const char *z, char out[41])
{
    size_t i;
    for (i = 0; i < 40 && z[i]; i++)
        out[i] = (z[i] >= 0x20 && z[i] < 0x7f) ? z[i] : '?';
    out[i] = '\0';
    return out;
}

static int hexval(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* Decode the urlencoded value p[0..n) into out (NUL terminated, at most
 * VAL_MAX octets; *len gets the full decoded length, which may be more).
 * -1 if it is malformed: a broken percent escape, a NUL however written,
 * or an octet that a form encoder never leaves bare. */
static int decode(const char *p, size_t n, char out[VAL_MAX + 1], size_t *len)
{
    size_t i = 0, o = 0;
    while (i < n) {
        unsigned ch = (unsigned char)p[i++];
        if (ch == '%') {
            int hi = i < n ? hexval(p[i]) : -1;
            int lo = i + 1 < n ? hexval(p[i + 1]) : -1;
            if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) return -1;
            ch = (unsigned)(hi * 16 + lo);
            i += 2;
        } else if (ch == '+') {
            ch = ' ';
        } else if (ch <= 0x20 || ch == 0x7f || ch == '=' || ch == '&') {
            return -1;
        }
        if (o < VAL_MAX) out[o] = (char)ch;
        o++;
    }
    out[o < VAL_MAX ? o : VAL_MAX] = '\0';
    *len = o;
    return 0;
}

/* the row of a name that is prefix and a row number 0..max-1, no leading zero; -1 if none */
static int row_of(const char *p, size_t n, const char *prefix, int max)
{
    size_t pl = strlen(prefix);
    int row;
    if (n <= pl || n > pl + 2 || memcmp(p, prefix, pl) != 0) return -1;
    if (p[pl] < '0' || p[pl] > '9') return -1;
    row = p[pl] - '0';
    if (n == pl + 2) {
        if (row == 0 || p[pl + 1] < '0' || p[pl + 1] > '9') return -1;
        row = row * 10 + (p[pl + 1] - '0');
    }
    return row < max ? row : -1;
}

/* F_ index of the field name p[0..n), F_USER + row, F_PASS + row,
 * F_SVC + 100 * column + row, or -1 */
static int field_of(const char *p, size_t n)
{
    int f, k;
    for (f = 0; f < F_COUNT; f++)
        if (strlen(fields[f].name) == n && memcmp(fields[f].name, p, n) == 0) return f;
    for (k = 0; k < S_COLS; k++)
        if ((f = row_of(p, n, svc_col[k], CFG_MAX_SERVICES)) >= 0) return F_SVC + 100 * k + f;
    if (n >= 5 && n <= 6 && (memcmp(p, "user", 4) == 0 || memcmp(p, "pass", 4) == 0)) {
        int row = p[4] - '0';
        if (row < 0 || row > 9) return -1;
        if (n == 6) {
            if (row != 1 || p[5] < '0' || p[5] > '9') return -1;    /* no leading zero */
            row = 10 + (p[5] - '0');
        }
        if (row >= CFG_MAX_USERS) return -1;
        return (p[0] == 'u' ? F_USER : F_PASS) + row;
    }
    return -1;
}

/* decimal number of 1 to 9 digits, blanks around it allowed */
static int get_num(const char *v, int *out)
{
    long n = 0;
    int digits = 0;
    while (*v == ' ') v++;
    while (*v >= '0' && *v <= '9') {
        if (++digits > 9) return -1;
        n = n * 10 + (*v++ - '0');
    }
    while (*v == ' ') v++;
    if (digits == 0 || *v != '\0') return -1;
    *out = (int)n;
    return 0;
}

static void set_num(form_t *f, int fi, int *field, const char *v)
{
    char sv[41];
    if (get_num(v, field))
        fail(f, "[%s] %s: '%s' is not a number", sec_of(f, fi), key_of(f, fi), safe(v, sv));
}

/* an address field: blanks around it allowed; empty = none where that is allowed */
static void set_addr(form_t *f, int fi, uint32_t *field, const char *v, int none_ok)
{
    char t[16], sv[41];
    size_t n;
    uint32_t a;

    while (*v == ' ') v++;
    n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    if (n == 0 && none_ok) { *field = 0; return; }
    if (n < sizeof t) { memcpy(t, v, n); t[n] = '\0'; }
    if (n >= sizeof t || cfg_parse_ip(t, &a))
        fail(f, "[%s] %s: '%s' is not an IPv4 address", sec_of(f, fi), key_of(f, fi), safe(v, sv));
    else
        *field = a;
}

static void set_enum(form_t *f, int fi, int *field, const char *v, const char *const *names,
                     int count, const char *list)
{
    char sv[41];
    int i;
    for (i = 0; i < count; i++)
        if (strcmp(names[i], v) == 0) { *field = i; return; }
    fail(f, "[%s] %s: '%s' is not one of: %s", sec_of(f, fi), key_of(f, fi), safe(v, sv), list);
}

/* one decoded field into the form */
static void set_field(form_t *f, int fi, const char *v, size_t len)
{
    cfg_t *c = f->out;
    char sv[41];

    if (fi >= F_SVC) {
        int k = (fi - F_SVC) / 100, row = (fi - F_SVC) % 100;
        memcpy(f->svc[k][row], v, (len > VAL_MAX ? VAL_MAX : len) + 1);
        if (len > VAL_MAX) fail(f, "[services] service %d: %s is too long", row + 1, svc_col[k]);
        return;
    }
    if (fi >= F_PASS) {
        if (len > CFG_SECRET_MAX)
            fail(f, "[ppp] user %d: password is longer than %d octets", fi - F_PASS + 1, CFG_SECRET_MAX);
        else
            memcpy(f->row[fi - F_PASS].secret, v, len + 1);
        return;
    }
    if (fi >= F_USER) {
        if (len > CFG_NAME_MAX)
            fail(f, "[ppp] user %d: name is longer than %d octets", fi - F_USER + 1, CFG_NAME_MAX);
        else
            memcpy(f->row[fi - F_USER].name, v, len + 1);
        return;
    }
    switch (fi) {
    case F_NUMBER:
        if (len > CFG_NUMBER_MAX) {
            fail(f, "[%s] number: longer than %d characters", sec_of(f, fi), CFG_NUMBER_MAX);
            break;
        }
        memset(c->line_number, 0, sizeof c->line_number);
        if (strcmp(v, "any") != 0) memcpy(c->line_number, v, len);
        break;
    case F_TONES:   set_enum(f, fi, &c->line_tones, v, n_tones, 2, "us, eu"); break;
    case F_AUTH:    set_enum(f, fi, &c->auth, v, n_auth, 3, "none, pap, chap"); break;
    case F_LAN:     set_enum(f, fi, &c->lan_dhcp, v, n_lan, 2, "dhcp, static"); break;
    case F_SERVICE: set_enum(f, fi, &c->service, v, n_service, CFG_SERVICE_COUNT, "internet, archive, dreamcast, saturn"); break;
    case F_V92:     set_enum(f, fi, &c->v92, v, n_v92, 2, "on, off"); break;
    case F_X2:      set_enum(f, fi, &c->x2, v, n_v92, 2, "on, off"); break;
    case F_K56:     set_enum(f, fi, &c->k56, v, n_v92, 2, "on, off"); break;
    case F_DCNOW:   set_enum(f, fi, &c->dc_now, v, n_v92, 2, "on, off"); break;
    case F_DCLAN:   set_enum(f, fi, &c->dc_lan, v, n_v92, 2, "on, off"); break;
    case F_DCUPNP:  set_enum(f, fi, &c->dc_upnp, v, n_v92, 2, "on, off"); break;
    case F_DCIWANGO: set_enum(f, fi, &c->dc_iwango, v, n_v92, 2, "on, off"); break;
    case F_SATMATCH: set_enum(f, fi, &c->sat_match, v, n_v92, 2, "on, off"); break;
    case F_COMPRESSION: set_enum(f, fi, &c->compression, v, n_comp, 2, "on, off"); break;
    case F_ENCRYPTION: set_enum(f, fi, &c->encryption, v, n_comp, 2, "on, off"); break;
    case F_UPDATE_CHECK: set_enum(f, fi, &c->update_check, v, n_v92, 2, "on, off"); break;
    case F_SVCNET:  set_enum(f, fi, &c->svc_netstalgia, v, n_v92, 2, "on, off"); break;
    case F_SVCMENU: set_enum(f, fi, &c->svc_menu, v, n_v92, 2, "on, off"); break;
    case F_DNSRELAY: set_enum(f, fi, &c->dns_relay, v, n_v92, 2, "on, off"); break;
    case F_IPV6:    set_enum(f, fi, &c->ipv6, v, n_v92, 2, "on, off"); break;
    case F_ADD:     f->add = 1; break;          /* the button's label, whatever it is */
    case F_ADDRESS: set_addr(f, fi, &c->lan_addr, v, 0); break;
    case F_NETMASK: set_addr(f, fi, &c->lan_netmask, v, 0); break;
    case F_GATEWAY: set_addr(f, fi, &c->lan_gateway, v, 1); break;
    case F_DNS1:    set_addr(f, fi, &c->dns[0], v, 1); break;
    case F_DNS2:    set_addr(f, fi, &c->dns[1], v, 1); break;
    case F_RINGS:   set_num(f, fi, &c->rings, v); break;
    case F_BUSY:    set_num(f, fi, &c->busy_chance, v); break;
    case F_DROP:    set_num(f, fi, &c->drop_mean_min, v); break;
    case F_SESSION: set_num(f, fi, &c->session_limit_min, v); break;
    case F_IDLE:    set_num(f, fi, &c->idle_timeout_min, v); break;
    case F_DOWN:    set_num(f, fi, &c->max_down, v); break;
    case F_UP:      set_num(f, fi, &c->max_up, v); break;
    case F_PORT:
        /* the pass before took port 2 from the literal pair "port=2" only:
         * the field must say the same however it was written */
        if (len != 1 || (v[0] != '1' && v[0] != '2') || (v[0] == '2' && !two_ports) || (v[0] == '2') != f->port2) f->bad = 1;
        break;
    case F_ADMINPW:
        if (len > CFG_SECRET_MAX)
            fail(f, "[status] admin_password: password is longer than %d octets", CFG_SECRET_MAX);
        else
            memcpy(f->admin_pw, v, len + 1);
        break;
    default:                                    /* the 0/1 fields */
        if (len == 1 && (v[0] == '0' || v[0] == '1')) f->sw[fi] = v[0] - '0';
        else fail(f, "[%s] %s: '%s' is not 0 or 1", sec_of(f, fi), key_of(f, fi), safe(v, sv));
        break;
    }
}

/* the user table of the candidate, from the rows posted and the current
 * users (see settings.h for the rules) */
static void build_users(form_t *f, const cfg_t *cur)
{
    cfg_t *c = f->out;
    int ncur = cur->n_users < 0 ? 0 : cur->n_users > CFG_MAX_USERS ? CFG_MAX_USERS : cur->n_users;
    int i, j, n = 0;
    char sn[41];

    cfg_user_t row[CFG_MAX_USERS]; int used[CFG_MAX_USERS], last = -1;

    memset(row, 0, sizeof row); memset(used, 0, sizeof used);
    for (i = 0; i < CFG_MAX_USERS; i++) {
        const char *name = (f->user_seen >> i) & 1u ? f->row[i].name : i < ncur ? cur->users[i].name : "";
        const char *pw = f->row[i].secret;
        cfg_user_t *u = &row[i];

        if (name[0] == '\0') {
            if (pw[0]) fail(f, "[ppp] user %d: password without a name", i + 1);
            continue;
        }
        used[i] = 1; last = i;
        for (j = 0; j < ncur; j++)
            if (strcmp(cur->users[j].name, name) == 0) break;
        strcpy(u->name, name);
        if (pw[0]) strcpy(u->secret, pw);
        else if (j < ncur) strcpy(u->secret, cur->users[j].secret);
        else fail(f, "[ppp] user '%s': a new user needs a password", safe(name, sn));
    }
    /* Closed up when all is well. On an error the page comes back with the
     * rows where they were posted, so the next post's row numbers still
     * mean the rows of the current settings. */
    memset(c->users, 0, sizeof c->users);
    for (i = 0; i < CFG_MAX_USERS; i++) {
        if (f->invalid) { if (i <= last) c->users[i] = row[i]; }
        else if (used[i]) c->users[n++] = row[i];
    }
    c->n_users = f->invalid ? last + 1 : n;
}

/* a value without the blanks at its ends, into out[VAL_MAX + 1] */
static const char *trimmed(const char *v, char out[VAL_MAX + 1])
{
    size_t n;
    while (*v == ' ') v++;
    n = strlen(v);
    while (n && v[n - 1] == ' ') n--;
    memcpy(out, v, n); out[n] = '\0';
    return out;
}

/* the service table of the candidate, when any service field was posted */
static void build_services(form_t *f, const cfg_t *cur)
{
    cfg_t *c = f->out;
    int ncur = cur->n_services < 0 ? 0 : cur->n_services > CFG_MAX_SERVICES ? CFG_MAX_SERVICES : cur->n_services;
    int i, k, n = 0, any = 0;

    cfg_service_t row[CFG_MAX_SERVICES]; int used[CFG_MAX_SERVICES], last = -1;

    for (k = 0; k < S_COLS; k++) any |= f->svc_seen[k] != 0;
    if (!any) return;
    memset(row, 0, sizeof row); memset(used, 0, sizeof used);
    for (i = 0; i < CFG_MAX_SERVICES; i++) {
        char col[S_COLS][VAL_MAX + 1], port[8], line[S_COLS * (VAL_MAX + 1) + 8];
        const cfg_service_t *e = i < ncur ? &cur->services[i] : NULL;
        const char *why = "";
        for (k = 0; k < S_COLS; k++) {
            if ((f->svc_seen[k] >> i) & 1u) { trimmed(f->svc[k][i], col[k]); continue; }
            col[k][0] = '\0';
            if (!e) continue;
            if (k == S_NUM) strcpy(col[k], e->number);
            else if (k == S_HOST) strcpy(col[k], e->host);
            else if (k == S_PORT) { snprintf(port, sizeof port, "%u", (unsigned)e->port); strcpy(col[k], port); }
            else if (k == S_MODE) strcpy(col[k], n_svcmode[e->mode == CFG_SVC_TELNET]);
            else strcpy(col[k], e->name);
        }
        if (!col[S_MODE][0]) strcpy(col[S_MODE], "raw");
        if (!col[S_NUM][0] && !col[S_HOST][0] && !col[S_PORT][0] && !col[S_NAME][0]) continue;   /* an empty row: not used */
        used[i] = 1; last = i;
        snprintf(line, sizeof line, "%s %s:%s %s %s", col[S_NUM], col[S_HOST], col[S_PORT], col[S_MODE], col[S_NAME]);
        if (!col[S_NUM][0])
            fail(f, "[services] service %d: a number is needed", i + 1);
        else if (strchr(col[S_NUM], ' ') || strchr(col[S_HOST], ' ') || strchr(col[S_PORT], ' ') || strchr(col[S_MODE], ' '))
            fail(f, "[services] service %d: a blank inside the number, host, port or mode", i + 1);
        else if (cfg_svc_parse(line, strlen(line), &row[i], &why))
            fail(f, "[services] service %d: %s", i + 1, why);
        else continue;
        /* not taken: the row as it was typed, for the page that says why */
        memset(&row[i], 0, sizeof row[i]);
        /* (cut to what each field holds: the page shows it again) */
        memcpy(row[i].number, col[S_NUM], strnlen(col[S_NUM], sizeof row[i].number - 1));
        memcpy(row[i].host, col[S_HOST], strnlen(col[S_HOST], sizeof row[i].host - 1));
        memcpy(row[i].name, col[S_NAME], strnlen(col[S_NAME], sizeof row[i].name - 1));
        { unsigned long pv = strtoul(col[S_PORT], NULL, 10); row[i].port = pv <= 65535 ? (uint16_t)pv : 0; }
        row[i].mode = strcmp(col[S_MODE], "telnet") == 0 ? CFG_SVC_TELNET : CFG_SVC_RAW;
    }
    /* as for the users: closed up when all is well, in place on an error */
    memset(c->services, 0, sizeof c->services);
    for (i = 0; i < CFG_MAX_SERVICES; i++) {
        if (f->invalid) { if (i <= last) c->services[i] = row[i]; }
        else if (used[i]) c->services[n++] = row[i];
    }
    c->n_services = f->invalid ? last + 1 : n;
}

static int last_add;
int settings_added(void) { return last_add; }

int settings_apply(const cfg_t *cur, const char *body, size_t len, cfg_t *out,
                   char *err, size_t errsz)
{
    form_t f;
    size_t pos = 0;
    unsigned mask;
    int i;

    memset(&f, 0, sizeof f);
    last_add = 0;
    f.out = out;
    for (i = 0; i < F_COUNT; i++) f.sw[i] = -1;
    *out = *cur;
    if (errsz) err[0] = '\0';

    /* port=2 anywhere in the body: the line and modem fields are port 2's.
     * They are then applied to the view of that port and put back at the end. */
    while (pos < len) {
        size_t pl = 0;
        while (pos + pl < len && body[pos + pl] != '&') pl++;
        if (pl == 6 && memcmp(body + pos, "port=2", 6) == 0) f.port2 = 1;
        pos += pl + 1;
    }
    pos = 0;
    if (f.port2) cfg_for_port(cur, 1, out);

    while (pos < len) {
        const char *pair = body + pos, *eq;
        char val[VAL_MAX + 1];
        size_t pl = 0, vlen;
        int fi;

        while (pos + pl < len && pair[pl] != '&') pl++;
        /* a pair is followed by & and another pair, or by the end */
        if (pos + pl < len && pos + pl + 1 == len) goto bad;
        pos += pl + 1;
        eq = memchr(pair, '=', pl);
        if (!eq) goto bad;
        fi = field_of(pair, (size_t)(eq - pair));
        if (fi < 0) goto bad;
        if (fi >= F_SVC) {
            int k = (fi - F_SVC) / 100, row = (fi - F_SVC) % 100;
            if ((f.svc_seen[k] >> row) & 1u) goto bad;
            f.svc_seen[k] |= 1u << row;
        } else if (fi >= F_PASS) {
            if ((f.pass_seen >> (fi - F_PASS)) & 1u) goto bad;
            f.pass_seen |= 1u << (fi - F_PASS);
        } else if (fi >= F_USER) {
            if ((f.user_seen >> (fi - F_USER)) & 1u) goto bad;
            f.user_seen |= 1u << (fi - F_USER);
        } else {
            if ((f.seen >> fi) & 1u) goto bad;
            f.seen |= (uint64_t)1 << fi;
        }
        if (decode(eq + 1, pl - (size_t)(eq + 1 - pair), val, &vlen)) goto bad;
        set_field(&f, fi, val, vlen);
        if (f.bad) goto bad;
    }

    /* modulations: the whole set, or one switch at a time */
    mask = f.sw[F_MODS] == 1 ? 0u : out->modulations;
    for (i = 0; i < CFG_MOD_COUNT; i++) {
        if (f.sw[F_V90 + i] == 1) mask |= 1u << i;
        if (f.sw[F_V90 + i] == 0) mask &= ~(1u << i);
    }
    out->modulations = mask;

    if (f.sw[F_ADMINCLR] == 1 && f.admin_pw[0])
        fail(&f, "[status] admin_password: a new password and admin_clear together");
    else if (f.sw[F_ADMINCLR] == 1)
        memset(out->admin_password, 0, sizeof out->admin_password);
    else if (f.admin_pw[0])
        memcpy(out->admin_password, f.admin_pw, sizeof f.admin_pw);

    build_users(&f, cur);
    build_services(&f, cur);

    last_add = f.add;
    if (f.port2) { cfg_t view = *out; cfg_for_port(&view, 1, out); }
    if (!f.invalid && cfg_validate(out, f.msg, sizeof f.msg) != CFG_OK) f.invalid = 1;
    if (f.invalid) {
        if (errsz) snprintf(err, errsz, "%s", f.msg);
        return SETTINGS_INVALID;
    }
    return SETTINGS_OK;

bad:
    *out = *cur;
    return SETTINGS_BAD;
}

/* ---- the login page -------------------------------------------------------- */

int login_render_html(int failed, char *buf, size_t cap)
{
    sbuf_t b;

    sb_init(&b, buf, cap);
    page_top(&b, "login", "", -1, "Login");
    sb_puts(&b, "<div class=\"lg\"><form method=\"post\" action=\"/login\">\n<section><h2>Log in</h2>\n");
    if (failed) sb_puts(&b, "<p class=\"err\">Wrong user name or password.</p>\n");
    sb_puts(&b,
        "<table>\n"
        "<tr><th>User name</th><td><input type=\"text\" name=\"user\" size=\"24\" maxlength=\"32\""
        " autocomplete=\"username\" autofocus></td></tr>\n"
        "<tr><th>Password</th><td><input type=\"password\" name=\"password\" size=\"24\" maxlength=\"64\""
        " autocomplete=\"current-password\"></td></tr>\n"
        "</table>\n"
        "<p class=\"act\"><input type=\"submit\" value=\"Log in\"></p></section>\n"
        "</form></div>\n" PAGE_END);
    return sb_end(&b);
}

int login_check(const char *body, size_t len, const char *pw)
{
    /* both sides of each comparison in buffers of one size, every octet
     * looked at, so the time does not tell how much was right */
    char user[VAL_MAX + 1], pass[VAL_MAX + 1], wu[VAL_MAX + 1], wp[VAL_MAX + 1];
    size_t pos = 0, i;
    unsigned seen = 0, diff = 0;

    memset(user, 0, sizeof user); memset(pass, 0, sizeof pass);
    memset(wu, 0, sizeof wu); memset(wp, 0, sizeof wp);
    while (pos < len) {
        const char *pair = body + pos, *eq;
        char val[VAL_MAX + 1];
        size_t pl = 0, vlen, nl;
        unsigned bit;

        while (pos + pl < len && pair[pl] != '&') pl++;
        if (pos + pl < len && pos + pl + 1 == len) return -1;
        pos += pl + 1;
        eq = memchr(pair, '=', pl);
        if (!eq) return -1;
        nl = (size_t)(eq - pair);
        if (nl == 4 && memcmp(pair, "user", 4) == 0) bit = 1;
        else if (nl == 8 && memcmp(pair, "password", 8) == 0) bit = 2;
        else return -1;
        if (seen & bit) return -1;
        seen |= bit;
        memset(val, 0, sizeof val);
        if (decode(eq + 1, pl - nl - 1, val, &vlen)) return -1;
        if (vlen > VAL_MAX) diff = 1;                   /* longer than any password */
        memcpy(bit == 1 ? user : pass, val, sizeof val);
    }
    if (seen != 3) diff = 1;
    strcpy(wu, "admin");
    for (i = 0; i < VAL_MAX && pw[i]; i++) wp[i] = pw[i];
    for (i = 0; i < sizeof user; i++)
        diff |= (unsigned)(unsigned char)(user[i] ^ wu[i]) | (unsigned)(unsigned char)(pass[i] ^ wp[i]);
    return diff == 0;
}
