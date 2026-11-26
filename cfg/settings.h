/* v90d/cfg - the settings form: an HTML page that shows the part of the
 * configuration a user changes from a browser, and the decoder for what the
 * browser posts back. Nothing here touches a socket or a file; httpd.c
 * serves the page at /settings and hands the posted body to
 * settings_apply().
 *
 * THE FORM
 *
 * Plain HTML: a few tables of text inputs, selects and checkboxes, a Save
 * button in the last card (and one out of sight first, for Enter), the Add
 * buttons of the Users and Numbers tables (/ui.js adds a row at once; without
 * it Add posts), nothing from elsewhere. It posts application/x-www-form-urlencoded with these
 * fields:
 *
 *   number            phone number; empty or `any` = every number connects
 *   tones             us | eu
 *   rings             1..10
 *   busy_chance       0..100
 *   drop_mean         0..10080
 *   session_limit     0..10080
 *   idle_timeout      0..10080
 *   auth              none | pap | chap
 *   user0 .. user15   user names, empty = row not used
 *   pass0 .. pass15   their passwords, see below
 *   mods              1: the seven mod_ fields below are a complete set of
 *                     checkboxes, one that is missing means off
 *   mod_v90, mod_v34, mod_v32bis, mod_v22bis, mod_v22, mod_v21, mod_v23
 *                     1 = on, 0 = off
 *   max_downstream    300..56000
 *   max_upstream      300..48000 (above 33600: V.92 only)
 *   v92               on | off
 *   compression       on | off (PPP compression, [ppp])
 *   admin_password    new password of this page, empty = keep
 *   admin_clear       1 = remove the password of this page
 *   add               the Add button of the Users and Numbers pages (any
 *                     value): saved as ever, then the page with an empty row
 *   netstalgia        on | off: answer the numbers of Netstalgia's list of
 *                     services too ([services])
 *   snum0 .. snum15   the owner's service numbers, empty = row not used
 *   shost0 .. shost15, sport0 .. sport15, smode0 .. smode15 (raw | telnet),
 *   sname0 .. sname15 the rest of each: see [services] in config.h
 *
 * The service rows: when any s field is posted, the table is made anew
 * from the rows, a row that is not posted keeping what it had and a row
 * with an empty number dropped (the others closed up); a row with a host
 * or a name but no number is an error.
 *
 * A field that is absent keeps the current value, so a client may post
 * just `busy_chance=50`. (The browser always sends them all, except
 * unchecked checkboxes, which is what `mods` is for.) Everything that is
 * not in the form (addresses, MRU, the status port ...) is taken over from
 * the current configuration unchanged.
 *
 * Passwords are never written into the page. For the users this means:
 *
 *   - a row whose name is one of the current users and whose password
 *     field is empty keeps that user's password;
 *   - a non-empty password field sets the password;
 *   - a name that is not a current user needs a password (an empty one can
 *     only be given in the config file);
 *   - an empty name removes the row; the rows that remain are closed up.
 *
 * User names are octet strings. A name that is valid UTF-8 without control
 * characters is shown in a text input as it is (the page is UTF-8, so the
 * browser sends the same octets back). Any other name cannot make the trip
 * through a browser unharmed: its row shows the name as text with \xHH
 * escapes and has no name input, so that row can get a new password here
 * but can only be renamed or removed in the config file.
 *
 * All values are HTML-escaped. The page is at most SETTINGS_HTML_MAX
 * bytes. */
#ifndef V90D_CFG_SETTINGS_H
#define V90D_CFG_SETTINGS_H

#include <stddef.h>
#include "config.h"

#define SETTINGS_HTML_MAX  28672  /* settings_render_html never needs more */
#define SETTINGS_BODY_MAX  8192   /* a browser's post of the whole form with
                                     every field at its longest fits */
#define SETTINGS_MSG_MAX   160    /* messages of settings_apply fit */

#define SETTINGS_OK        0
#define SETTINGS_INVALID   1      /* understood, but a value is not acceptable */
#define SETTINGS_BAD      (-1)    /* not a form of ours */

/* Write the page for config *c into buf. With saved nonzero it says that
 * the settings were saved; with error not NULL it shows that text as the
 * reason why they were not (escaped: it may quote user input). Never
 * writes more than cap bytes, always NUL terminates when cap > 0. Returns
 * the length without the NUL, or -1 if cap is too small. */
int settings_render_html(const cfg_t *c, int saved, const char *error, char *buf, size_t cap);

/* The settings are spread over pages, each with its own form, path and
 * place in the menu; SETTINGS_ALL is all of them on one page at /settings (kept for
 * scripts, no tab leads there). settings_render_page() is the renderer of
 * one of them; settings_apply() takes the post of any. */
enum { SETTINGS_ALL, SETTINGS_LINE, SETTINGS_MODEM, SETTINGS_USERS, SETTINGS_NETWORK,
       SETTINGS_SYSTEM,
       /* the Line and Modem pages of port 2 (/line2, /modem2): the same
        * fields with a hidden port=2, which makes settings_apply() put them
        * into cfg_t.port2. They sit under the Line and Modem tabs. */
       SETTINGS_LINE2, SETTINGS_MODEM2,
       /* the Consoles page (/dreamcast, /dreamcast2): the dreamcast switches
        * and the saturn matchmaking switch, per port */
       SETTINGS_CONSOLES, SETTINGS_CONSOLES2,
       /* the Numbers page (/services, under Services): the owner's numbers that connect
        * straight to a service, the switch for Netstalgia's, and
        * Netstalgia's list as it is (settings_set_svc_rows). Not on the
        * page of all settings. */
       SETTINGS_SERVICES, SETTINGS_PAGES };
int settings_render_page(const cfg_t *c, int page, int saved, const char *error, char *buf, size_t cap);
/* The same with add: one more empty row on the Users and Numbers pages (the
 * Add button: the page saved, then shown again with a row to fill in). Those
 * pages show their entries, and one empty row while there is none. */
int settings_render_page_add(const cfg_t *c, int page, int saved, int add, const char *error, char *buf, size_t cap);
/* What the Dreamcast page says about the VPN profile (vpn.h) that the
 * owner keeps: "host port" of its server, or NULL when none is stored. The
 * profile itself is never shown. */
void settings_set_vpn(const char *remote);
/* How many FXS ports the box has (the daemon tells the web layer once at
 * start: an HT801 has one, an HT802 two). With one, the settings pages show
 * no Port 2 switch, the port-2 paths (/line2, /modem2, /dreamcast2) do not
 * resolve, and a port=2 post is refused. The default is two. */
void settings_set_two_ports(int two);
/* Netstalgia's numbers for the Numbers page: fn writes them as <tr> rows
 * of four cells (number, name, mode, host:port) into buf[0..cap) and
 * returns how many octets, *left the number of entries that did not fit
 * or were not shown. NULL (the default): no list. */
typedef size_t (*settings_svc_rows_fn)(char *buf, size_t cap, int *shown, int *left);
void settings_set_svc_rows(settings_svc_rows_fn fn);
const char *settings_page_path(int page);
/* the page whose path is path[0..len), -1 if it is none */
int settings_page_of(const char *path, size_t len);
/* 1 for the pages of port 2 (/line2, /modem2, /dreamcast2): their post
 * carries port=2, and no other page's may */
int settings_page_port2(int page);

/* Decode a posted form body and build the candidate config *out from the
 * current one *cur (which must be valid). Returns
 *
 *   SETTINGS_OK       *out is the new config and passes cfg_validate
 *   SETTINGS_INVALID  a value is wrong; err says which and why, and *out
 *                     holds the candidate as far as it was understood, for
 *                     showing the form again. It must not be used.
 *   SETTINGS_BAD      the body is malformed: a bad percent escape, a NUL
 *                     octet, a field twice, an unknown field, a pair
 *                     without `=`. *out is a copy of *cur.
 *
 * err may be NULL if errsz is 0. */
int settings_apply(const cfg_t *cur, const char *body, size_t len, cfg_t *out,
                   char *err, size_t errsz);
/* 1 if the form that settings_apply() took last was posted with its Add
 * button (field add, any value): show the page with an empty row next */
int settings_added(void);

/* The login page: user name and password, posted to /login. failed adds
 * the line that the last try was wrong. At most LOGIN_HTML_MAX octets;
 * returns the length or -1. */
#define LOGIN_HTML_MAX  8192
#define LOGIN_BODY_MAX  512    /* a posted login form */
int login_render_html(int failed, char *buf, size_t cap);

/* The posted login form body[0..len): 1 if it carries the user name admin
 * and the password pw (compared in constant time), 0 if not, -1 if the
 * body is malformed (bad escape, unknown or repeated field). */
int login_check(const char *body, size_t len, const char *pw);

#endif
