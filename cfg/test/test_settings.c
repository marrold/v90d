/* Tests for the settings form: the page (content, escaping, no secret in
 * it, bounded output) and the decoder of the posted body (every field,
 * the password rules, malformed bodies), and both together: what a browser
 * would post back from the rendered page gives the same config again. */
#include <string.h>
#include "test.h"
#include "page.h"
#include "settings.h"

static char html[SETTINGS_HTML_MAX];
static char body[4 * SETTINGS_BODY_MAX];
static char err[SETTINGS_MSG_MAX];

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static int apply(const cfg_t *cur, const char *b, cfg_t *out)
{
    strcpy(err, "?");
    return settings_apply(cur, b, strlen(b), out, err, sizeof err);
}

/* a config with users, like the one the daemon would run with */
static void base(cfg_t *c)
{
    cfg_default(c);
    c->auth = CFG_AUTH_CHAP;
    c->n_users = 3;
    strcpy(c->users[0].name, "alice"); strcpy(c->users[0].secret, "SECRET-A");
    strcpy(c->users[1].name, "bob");   strcpy(c->users[1].secret, "SECRET-B");
    strcpy(c->users[2].name, "carol"); strcpy(c->users[2].secret, "");
    strcpy(c->line_number, "5551234");
    c->busy_chance = 10;
    c->mru = 576;                       /* not in the form: must survive */
    c->status_port = 8080;
}

/* ---- a browser, as far as forms go ----------------------------------------- */

/* text of attribute `attr` of the tag [tag, end) with the entities we emit
 * decoded; 0 if the tag has no such attribute */
static int attr_of(const char *tag, const char *end, const char *attr, char *out, size_t cap)
{
    char pat[32];
    const char *p;
    size_t n = 0, pl;
    snprintf(pat, sizeof pat, " %s=\"", attr);
    pl = strlen(pat);
    for (p = tag; p + pl <= end; p++)
        if (memcmp(p, pat, pl) == 0) break;
    if (p + pl > end) return 0;
    for (p += pl; p < end && *p != '"'; ) {
        static const struct { const char *e; char ch; } ent[] = {
            { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' } };
        size_t k;
        for (k = 0; k < 5; k++)
            if (strncmp(p, ent[k].e, strlen(ent[k].e)) == 0) break;
        if (n + 1 >= cap) break;
        if (k < 5) { out[n++] = ent[k].ch; p += strlen(ent[k].e); }
        else out[n++] = *p++;
    }
    out[n] = '\0';
    return 1;
}

static void put_enc(char *out, size_t cap, size_t *n, const char *z)
{
    static const char hx[] = "0123456789ABCDEF";
    for (; *z && *n + 4 < cap; z++) {
        unsigned ch = (unsigned char)*z;
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '*') out[(*n)++] = (char)ch;
        else if (ch == ' ') out[(*n)++] = '+';
        else { out[(*n)++] = '%'; out[(*n)++] = hx[ch >> 4]; out[(*n)++] = hx[ch & 15]; }
    }
}

/* What a browser posts when the form in `page` is submitted untouched:
 * every input and select in document order, checkboxes only when checked.
 * Returns the number of fields. */
static int submit(const char *page, char *out, size_t cap)
{
    const char *p = page, *form_end = strstr(page, "</form>");
    char name[64], val[512], type[32];
    size_t n = 0;
    int fields = 0;

    while (form_end && (p = strchr(p, '<')) != NULL && p < form_end) {
        const char *end = strchr(p, '>');
        int is_input = strncmp(p, "<input ", 7) == 0, is_select = strncmp(p, "<select ", 8) == 0;
        if (!end) break;
        if ((is_input || is_select) && attr_of(p, end, "name", name, sizeof name)) {
            int send = 1;
            val[0] = '\0';
            if (is_input) {
                if (!attr_of(p, end, "type", type, sizeof type)) strcpy(type, "text");
                attr_of(p, end, "value", val, sizeof val);
                if (strcmp(type, "submit") == 0) send = 0;
                if (strcmp(type, "checkbox") == 0) {
                    send = end - p > 9 && memcmp(end - 8, " checked", 8) == 0;
                }
            } else {
                const char *q = p, *sel_end = strstr(p, "</select>");
                send = 0;
                while (sel_end && (q = strstr(q + 1, "<option ")) != NULL && q < sel_end) {
                    const char *oe = strchr(q, '>');
                    if (oe && memcmp(oe - 9, " selected", 9) == 0) {
                        attr_of(q, oe, "value", val, sizeof val);
                        send = 1;
                    }
                }
            }
            if (send) {
                if (fields++) out[n++] = '&';
                put_enc(out, cap, &n, name);
                out[n++] = '=';
                put_enc(out, cap, &n, val);
            }
        }
        p = end;
    }
    out[n] = '\0';
    return fields;
}

/* ---- the page -------------------------------------------------------------- */

static void test_page(void)
{
    cfg_t c;
    int n;

    cfg_default(&c);
    n = settings_render_html(&c, 0, NULL, html, sizeof html);
    CHECK(n > 0);
    CHECK_EQ_I((int)strlen(html), n);
    CHECK(strncmp(html, "<!DOCTYPE html>", 15) == 0);
    CHECK(has(html, "</html>\n"));
    CHECK(has(html, "<title>Nestaliga settings</title>"));
    CHECK(has(html, "<h1><i></i>Nestaliga<small>dial-up server</small></h1>"));
    CHECK(has(html, "<a href=\"/\">Status</a>"));
    CHECK(has(html, "prefers-color-scheme:dark"));
    CHECK(has(html, "<a href=\"/line\">Phone</a>") && !has(html, "href=\"/config\""));
    CHECK(has(html, "<small>1 to 10</small>"));
    CHECK(has(html, "<form method=\"post\" action=\"/settings\">"));
    CHECK(has(html, "<input type=\"submit\" value=\"Save\">"));
    /* self-contained, and it must not reload itself under the user's hands:
     * one script, the box's own file, nothing inline */
    CHECK(has(html, "<script src=\"/ui.js?v=" V90D_VERSION "\" defer></script>") && !has(html, "<script>"));
    CHECK(strstr(strstr(html, "src=") + 4, "src=") == NULL && !has(html, "url(") &&
          !has(html, "http:") && !has(html, "https:") && !has(html, "@import") &&
          !has(html, "http-equiv") && !has(html, "\xe2\x80\x94"));
    CHECK(!has(html, "saved") && !has(html, "Not saved"));

    /* defaults */
    CHECK(has(html, "<input type=\"text\" name=\"number\" size=\"22\" maxlength=\"20\" value=\"\">"));
    CHECK(has(html, "<option value=\"us\" selected>"));
    CHECK(has(html, "<option value=\"eu\">"));
    CHECK(has(html, "name=\"rings\" size=\"6\" maxlength=\"5\" value=\"1\">"));
    CHECK(has(html, "name=\"busy_chance\" size=\"6\" maxlength=\"5\" value=\"0\">"));
    CHECK(has(html, "name=\"drop_mean\" size=\"6\" maxlength=\"5\" value=\"0\">"));
    CHECK(has(html, "name=\"session_limit\" size=\"6\" maxlength=\"5\" value=\"0\">"));
    CHECK(has(html, "name=\"idle_timeout\" size=\"6\" maxlength=\"5\" value=\"0\">"));
    CHECK(has(html, "<option value=\"none\" selected>"));
    CHECK(has(html, "<option value=\"pap\">") && has(html, "<option value=\"chap\">"));
    CHECK(has(html, "<input type=\"hidden\" name=\"mods\" value=\"1\">"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"mod_v90\" value=\"1\" checked>"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"mod_v34\" value=\"1\" checked> V.34</label>"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"mod_v32bis\" value=\"1\" checked> V.32bis</label>"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"mod_v22bis\" value=\"1\" checked>"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"mod_v22\" value=\"1\" checked>"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"mod_v21\" value=\"1\" checked>"));
    CHECK(has(html, "name=\"max_downstream\" size=\"6\" maxlength=\"5\" value=\"56000\">"));
    CHECK(has(html, "name=\"max_upstream\" size=\"6\" maxlength=\"5\" value=\"33600\">"));
    CHECK(has(html, "<small>bit/s, 300 to 48000</small>")); CHECK(!has(html, "300 to 33600"));
    /* V.92: a switch, on unless switched off */
    CHECK(has(html, "<tr><th>V.92</th><td>" "<select name=\"v92\"><option value=\"off\">Off: V.90 upstream (V.34, up to 33600 bit/s)</option>"
                    "<option value=\"on\" selected>On: offer PCM upstream to V.92 callers</option></select>"));
    CHECK(has(html, "AT+PIG=0")); CHECK(!has(html, "simulation"));
    CHECK(has(html, "<input type=\"text\" name=\"user0\" size=\"24\" maxlength=\"63\" value=\"\">"));
    /* no user yet: the one new row, with its Add button */
    CHECK(!has(html, "name=\"user1\"") && !has(html, "name=\"user16\""));
    CHECK(has(html, "<tr class=\"new\"><td>New</td>") && has(html, "<p><input type=\"submit\" name=\"add\" value=\"Add\" data-tpl="));
    CHECK(has(html, "<input type=\"password\" name=\"pass0\" size=\"24\" maxlength=\"64\""
                    " autocomplete=\"new-password\">"));
    CHECK(!has(html, "name=\"pass1\""));
    CHECK(has(html, "<input type=\"password\" name=\"admin_password\" size=\"24\" maxlength=\"64\""
                    " autocomplete=\"new-password\">"));
    CHECK(has(html, "<input type=\"checkbox\" name=\"admin_clear\" value=\"1\">"));
    CHECK(has(html, "No password is set"));

    /* other values */
    base(&c);
    c.line_tones = CFG_TONES_EU;
    c.rings = 4; c.drop_mean_min = 45; c.session_limit_min = 120; c.idle_timeout_min = 10;
    c.auth = CFG_AUTH_PAP;
    c.modulations = CFG_MOD_V22BIS | CFG_MOD_V21;
    c.max_down = 2400; c.max_up = 1200;
    strcpy(c.admin_password, "ADMIN-SECRET");
    n = settings_render_html(&c, 0, NULL, html, sizeof html);
    CHECK(n > 0);
    CHECK(has(html, "name=\"number\" size=\"22\" maxlength=\"20\" value=\"5551234\">"));
    CHECK(has(html, "<option value=\"us\">") && has(html, "<option value=\"eu\" selected>"));
    CHECK(has(html, "name=\"rings\" size=\"6\" maxlength=\"5\" value=\"4\">"));
    CHECK(has(html, "name=\"busy_chance\" size=\"6\" maxlength=\"5\" value=\"10\">"));
    CHECK(has(html, "name=\"drop_mean\" size=\"6\" maxlength=\"5\" value=\"45\">"));
    CHECK(has(html, "name=\"session_limit\" size=\"6\" maxlength=\"5\" value=\"120\">"));
    CHECK(has(html, "name=\"idle_timeout\" size=\"6\" maxlength=\"5\" value=\"10\">"));
    CHECK(has(html, "<option value=\"none\">") && has(html, "<option value=\"pap\" selected>"));
    CHECK(has(html, "name=\"mod_v90\" value=\"1\">"));
    CHECK(has(html, "name=\"mod_v22bis\" value=\"1\" checked>"));
    CHECK(has(html, "name=\"mod_v22\" value=\"1\">"));
    CHECK(has(html, "name=\"mod_v21\" value=\"1\" checked>"));
    CHECK(has(html, "name=\"max_downstream\" size=\"6\" maxlength=\"5\" value=\"2400\">"));
    CHECK(has(html, "name=\"max_upstream\" size=\"6\" maxlength=\"5\" value=\"1200\">"));
    c.v92 = 1;
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<select name=\"v92\"><option value=\"off\">Off: V.90 upstream (V.34, up to 33600 bit/s)</option>"
                    "<option value=\"on\" selected>On: offer PCM upstream to V.92 callers</option></select>"));
    c.v92 = 0;
    n = settings_render_html(&c, 0, NULL, html, sizeof html);
    CHECK(has(html, "name=\"user0\" size=\"24\" maxlength=\"63\" value=\"alice\">"));
    CHECK(has(html, "name=\"user1\" size=\"24\" maxlength=\"63\" value=\"bob\">"));
    CHECK(has(html, "name=\"user2\" size=\"24\" maxlength=\"63\" value=\"carol\">"));
    CHECK(!has(html, "name=\"user3\""));                     /* no empty row until Add asks for one */
    CHECK(has(html, "A password is set"));
    /* no secret is ever in the page, and no password field has a value */
    CHECK(!has(html, "SECRET"));
    {
        const char *p = html;
        int pw = 0, with_value = 0;
        while ((p = strstr(p, "<input type=\"password\"")) != NULL) {
            const char *e = strchr(p, '>'), *v = strstr(p, "value");
            pw++;
            if (!e || (v && v < e)) with_value++;
            p++;
        }
        CHECK_EQ_I(pw, 3 + 1 + 1);          /* the three users, the admin password, the template of a new row */
        CHECK_EQ_I(with_value, 0);
    }

    /* notices */
    CHECK(settings_render_html(&c, 1, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<p><b>Settings saved.</b></p>"));
    CHECK(!has(html, "Not saved"));
    CHECK(settings_render_html(&c, 0, "[ppp] user 'x<y>&\"q\"': a new user needs a password\x01\xff",
                               html, sizeof html) > 0);
    CHECK(has(html, "<p><b>Not saved:</b> [ppp] user &#39;x&lt;y&gt;&amp;&quot;q&quot;&#39;: a new user"
                    " needs a password\\x01\\xff</p>"));
    CHECK(!has(html, "Settings saved"));
    CHECK(!has(html, "x<y>"));
}

static void test_escaping(void)
{
    cfg_t c;
    const char *p;
    int i;

    /* markup in a user name and in a number that came from a bad post */
    base(&c);
    strcpy(c.users[0].name, "\"><script>alert('x')</script>&");
    strcpy(c.line_number, "\"><b>&'");
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "<script>") && !has(html, "<b>&") && !has(html, "alert('"));
    CHECK(has(html, "name=\"user0\" size=\"24\" maxlength=\"63\" value=\"&quot;&gt;&lt;script&gt;"
                    "alert(&#39;x&#39;)&lt;/script&gt;&amp;\">"));
    CHECK(has(html, "name=\"number\" size=\"22\" maxlength=\"20\" value=\"&quot;&gt;&lt;b&gt;&amp;&#39;\">"));

    /* a UTF-8 name goes out as it is; the browser sends the same octets */
    base(&c);
    strcpy(c.users[1].name, "jos\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80");
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "name=\"user1\" size=\"24\" maxlength=\"63\" value=\"jos\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80\">"));

    /* names a browser would not send back the same get no name input: the
     * row is shown as text and stays as it is */
    {
        static const char *const odd[] = {
            "caf\xe9",                  /* Latin-1, not UTF-8 */
            "a\x01z",                  /* control octet */
            "tab\there",
            "del\x7f",
            "\xc3",                     /* cut short */
            "\xc0\xaf",                 /* overlong */
            "\xe0\x80\xaf",             /* overlong */
            "\xed\xa0\x80",             /* surrogate */
            "\xf4\x90\x80\x80",         /* beyond U+10FFFF */
            "\xf8\x88\x80\x80\x80",     /* five-octet form */
            "\x80",                     /* lone continuation */
            "ok\xe2\x82",               /* cut short at the end */
        };
        for (i = 0; i < (int)(sizeof odd / sizeof odd[0]); i++) {
            base(&c);
            strcpy(c.users[1].name, odd[i]);
            CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
            CHECK(!has(html, "name=\"user1\""));
            CHECK(has(html, "name=\"user0\"") && has(html, "name=\"user2\"") && has(html, "name=\"pass1\""));
            for (p = html; *p; p++)
                if (!(*p == '\n' || (*p >= 0x20 && *p <= 0x7e))) break;
            CHECK(*p == '\0');                  /* such a page is pure ASCII */
        }
        base(&c);
        strcpy(c.users[1].name, "a<\x01\xff>");
        CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
        CHECK(has(html, "<td>a&lt;\\x01\\xff&gt; <i>(change it in the config file)</i></td>"));
    }

    /* fields without their NUL are read to their end and no further */
    base(&c);
    memset(c.users[0].name, 'N', sizeof c.users[0].name);
    memset(c.line_number, '7', sizeof c.line_number);
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    p = strstr(html, "name=\"user0\" size=\"24\" maxlength=\"63\" value=\"");
    CHECK(p != NULL);
    if (p) { p = strstr(p, "value=\"") + 7; CHECK_EQ_I(strspn(p, "N"), 64); CHECK(p[64] == '"'); }
    p = strstr(html, "name=\"number\" size=\"22\" maxlength=\"20\" value=\"");
    CHECK(p != NULL);
    if (p) { p = strstr(p, "value=\"") + 7; CHECK_EQ_I(strspn(p, "7"), 21); CHECK(p[21] == '"'); }

    /* fields outside their range do not index anything */
    base(&c);
    c.line_tones = 7; c.auth = -3; c.lan_dhcp = 5; c.service = 9; c.dc_now = c.dc_lan = c.dc_upnp = c.dc_iwango = c.sat_match = 9; c.v92 = 9; c.x2 = 9; c.k56 = 9; c.compression = 9; c.encryption = 9; c.update_check = 9; c.dns_relay = 9; c.ipv6 = 9; c.n_users = 1000; c.modulations = 0xffffffffu;
    c.rings = -2147483647 - 1; c.max_down = 2147483647;
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "name=\"rings\" size=\"6\" maxlength=\"5\" value=\"-2147483648\">"));
    CHECK(!has(html, " selected>"));
    c.n_users = -5;
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "name=\"user0\" size=\"24\" maxlength=\"63\" value=\"\">"));
}

static void worst(cfg_t *c)
{
    int i;
    cfg_default(c);
    c->n_users = CFG_MAX_USERS;
    for (i = 0; i < CFG_MAX_USERS; i++) {
        memset(c->users[i].name, '"', CFG_NAME_MAX);        /* 6 characters each */
        c->users[i].name[0] = (char)('a' + i);
        memset(c->users[i].secret, 's', CFG_SECRET_MAX);
    }
    memset(c->line_number, '"', CFG_NUMBER_MAX);
    c->rings = c->busy_chance = c->drop_mean_min = c->session_limit_min = -2147483647 - 1;
    c->idle_timeout_min = c->max_down = c->max_up = -2147483647 - 1;
    memset(c->admin_password, 'p', CFG_SECRET_MAX);
}

static void test_bounds(void)
{
    static char ref[SETTINGS_HTML_MAX], g[SETTINGS_HTML_MAX + 16];
    char msg[SETTINGS_MSG_MAX];
    cfg_t c;
    int n, cap;

    worst(&c);
    memset(msg, '"', sizeof msg - 1); msg[sizeof msg - 1] = '\0';
    n = settings_render_html(&c, 1, msg, ref, sizeof ref);
    CHECK(n > 0 && n < SETTINGS_HTML_MAX);
    printf("  worst-case settings page %d of %d bytes\n", n, SETTINGS_HTML_MAX);
    for (cap = 0; cap <= n + 1; cap++) {
        int r, ok = 1;
        memset(g + (cap > 64 ? cap - 64 : 0), '#', 64 + 16);
        r = settings_render_html(&c, 1, msg, g, (size_t)cap);
        if (cap <= n && r != -1) ok = 0;
        if (cap == n + 1 && r != n) ok = 0;
        if (g[cap] != '#' || g[cap + 1] != '#') ok = 0;
        if (cap > 0 && memchr(g, '\0', (size_t)cap) == NULL) ok = 0;
        if (cap > 0 && strncmp(g, ref, strlen(g)) != 0) ok = 0;
        if (!ok) { CHECK(ok); printf("  cap %d r %d\n", cap, r); break; }
    }
    CHECK(cap == n + 2);
}

/* ---- the decoder ----------------------------------------------------------- */

/* the Ethernet section: how the box gets its LAN address */
static void test_ethernet(void)
{
    cfg_t c, out;
    int n;

    base(&c);
    n = settings_render_html(&c, 0, NULL, html, sizeof html);
    CHECK(n > 0);
    /* the password the image ships with is pointed out */
    strcpy(c.admin_password, "admin");
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "still the factory password"));
    strcpy(c.admin_password, "admin2");
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "still the factory password") && has(html, "A password is set"));
    base(&c);
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<h2>Ethernet</h2>"));
    CHECK(has(html, "<select name=\"lan\"><option value=\"dhcp\" selected>DHCP</option>"
                    "<option value=\"static\">Static address</option></select>"));
    CHECK(has(html, "<input type=\"text\" name=\"address\" size=\"16\" maxlength=\"15\" value=\"192.168.1.2\">"));
    CHECK(has(html, "name=\"netmask\" size=\"16\" maxlength=\"15\" value=\"255.255.255.0\">"));
    CHECK(has(html, "name=\"gateway\" size=\"16\" maxlength=\"15\" value=\"\">"));
    CHECK(has(html, "name=\"dns1\" size=\"16\" maxlength=\"15\" value=\"9.9.9.9\">"));
    CHECK(has(html, "name=\"dns2\" size=\"16\" maxlength=\"15\" value=\"1.1.1.1\">"));
    CHECK(has(html, "next restart"));
    /* the service of the port: the Internet unless asked, one choice per port */
    CHECK(has(html, "<select name=\"service\">"));
    CHECK(has(html, "<option value=\"internet\" selected>"));
    CHECK(has(html, "Protoweb")); CHECK(has(html, "Dreamcast"));
    CHECK(!has(html, "name=\"archive\""));
    CHECK_EQ_I(apply(&c, "service=archive", &out), SETTINGS_OK);
    CHECK_EQ_I(out.service, CFG_SERVICE_ARCHIVE); CHECK_EQ_I(out.port2.service, CFG_SERVICE_INTERNET);
    n = settings_render_html(&out, 0, NULL, html, sizeof html);
    CHECK(has(html, "<option value=\"archive\" selected>"));
    CHECK_EQ_I(apply(&c, "port=2&service=dreamcast", &out), SETTINGS_OK);
    CHECK_EQ_I(out.service, CFG_SERVICE_INTERNET); CHECK_EQ_I(out.port2.service, CFG_SERVICE_DREAMCAST);
    /* PPP encryption is set per port (it needs a user, which MS-CHAPv2 does) */
    c.n_users = 1; strcpy(c.users[0].name, "u"); strcpy(c.users[0].secret, "p");
    CHECK_EQ_I(apply(&c, "encryption=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.encryption, 1); CHECK_EQ_I(out.port2.encryption, 0);
    CHECK_EQ_I(apply(&c, "port=2&encryption=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.encryption, 0); CHECK_EQ_I(out.port2.encryption, 1);
    /* a bad value is reported under the key it would have set */
    CHECK_EQ_I(apply(&c, "encryption=maybe", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] encryption: 'maybe' is not one of: on, off") == 0);
    CHECK_EQ_I(apply(&c, "port=2&encryption=maybe", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] encryption2: 'maybe' is not one of: on, off") == 0);
    c.service = CFG_SERVICE_ARCHIVE;
    CHECK_EQ_I(apply(&c, "service=internet", &out), SETTINGS_OK);
    CHECK_EQ_I(out.service, CFG_SERVICE_INTERNET);
    CHECK_EQ_I(apply(&c, "rings=2", &out), SETTINGS_OK);       /* absent: kept */
    CHECK_EQ_I(out.service, CFG_SERVICE_ARCHIVE);
    c.service = CFG_SERVICE_INTERNET;
    CHECK_EQ_I(apply(&c, "service=netlink", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[line] service: 'netlink' is not one of: internet, archive, dreamcast, saturn") == 0);
    CHECK_EQ_I(apply(&c, "service=saturn", &out), SETTINGS_OK);
    CHECK_EQ_I(out.service, CFG_SERVICE_SATURN);
    n = settings_render_html(&out, 0, NULL, html, sizeof html);
    CHECK(has(html, "<option value=\"saturn\" selected>"));
    /* three switches for a console's port, off unless asked, one set per port */
    n = settings_render_html(&c, 0, NULL, html, sizeof html);
    CHECK(has(html, "<select name=\"dreamcast_now\">")); CHECK(has(html, "<select name=\"dreamcast_lan\">"));
    CHECK(has(html, "<select name=\"dreamcast_upnp\">"));
    CHECK(has(html, "Dreamcast Now"));
    CHECK_EQ_I(apply(&c, "dreamcast_now=on&dreamcast_upnp=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.dc_now, 1); CHECK_EQ_I(out.dc_lan, 0); CHECK_EQ_I(out.dc_upnp, 1); CHECK_EQ_I(out.port2.dc_now, 0);
    CHECK_EQ_I(apply(&c, "port=2&dreamcast_lan=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.dc_lan, 0); CHECK_EQ_I(out.port2.dc_lan, 1);
    CHECK_EQ_I(apply(&c, "dreamcast_lan=perhaps", &out), SETTINGS_INVALID);
    CHECK(has(html, "<select name=\"dreamcast_iwango\">"));
    CHECK_EQ_I(apply(&c, "port=2&dreamcast_iwango=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.dc_iwango, 0); CHECK_EQ_I(out.port2.dc_iwango, 1);
    CHECK_EQ_I(apply(&c, "dreamcast_iwango=yes", &out), SETTINGS_INVALID);
    /* V.92: the same kind of switch, on the modem page */
    CHECK_EQ_I(apply(&c, "v92=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.v92, 1); CHECK_EQ_I(out.service, CFG_SERVICE_INTERNET);
    c.v92 = 1;
    CHECK_EQ_I(apply(&c, "v92=off", &out), SETTINGS_OK);
    CHECK_EQ_I(out.v92, 0);
    CHECK_EQ_I(apply(&c, "rings=2", &out), SETTINGS_OK);         /* absent: kept */
    CHECK_EQ_I(out.v92, 1);
    c.v92 = 0;
    /* PPP compression: the same kind of switch, on unless switched off */
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<select name=\"compression\">"));
    CHECK(has(html, "<option value=\"on\" selected>On: Deflate or MPPC"));
    CHECK_EQ_I(apply(&c, "compression=off", &out), SETTINGS_OK);
    CHECK_EQ_I(out.compression, 0); CHECK_EQ_I(out.v92, 0);
    CHECK(settings_render_html(&out, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<option value=\"off\" selected>Off: no PPP compression"));
    c.compression = 0;
    CHECK_EQ_I(apply(&c, "compression=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.compression, 1);
    CHECK_EQ_I(apply(&c, "rings=2", &out), SETTINGS_OK);         /* absent: kept */
    CHECK_EQ_I(out.compression, 0);
    c.compression = 1;
    CHECK_EQ_I(apply(&c, "compression=deflate", &out), SETTINGS_INVALID);
    CHECK_EQ_I(apply(&c, "v92=maybe", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[modem] v92: 'maybe' is not one of: on, off") == 0);
    CHECK_EQ_I(apply(&c, "v92=on&v92=off", &out), SETTINGS_BAD);
    /* v92 = on without v90 in the list is valid (it just has no effect) */
    CHECK_EQ_I(apply(&c, "v92=on&mods=1&mod_v34=1", &out), SETTINGS_OK);
    CHECK_EQ_I(out.v92, 1); CHECK_EQ_I(out.modulations, CFG_MOD_V34);
    /* upstream above 33600 is for V.92 */
    CHECK_EQ_I(apply(&c, "max_upstream=48000", &out), SETTINGS_OK);
    CHECK_EQ_I(out.max_up, 48000);
    n = settings_render_html(&c, 0, NULL, html, sizeof html);
    /* the way to restart: a form of its own, after the settings form */
    CHECK(strstr(html, "<form method=\"post\" action=\"/restart\">") > strstr(html, "</form>"));
    CHECK(has(html, "<input type=\"submit\" value=\"Restart\">"));

    CHECK_EQ_I(apply(&c, "lan=static&address=192.168.7.9&netmask=255.255.255.0&gateway=192.168.7.1"
                         "&dns1=8.8.8.8&dns2=", &out), SETTINGS_OK);
    CHECK_EQ_I(out.lan_dhcp, 0);
    CHECK(out.lan_addr == 0xC0A80709u && out.lan_netmask == 0xFFFFFF00u && out.lan_gateway == 0xC0A80701u);
    CHECK(out.dns[0] == 0x08080808u && out.dns[1] == 0);
    n = settings_render_html(&out, 0, NULL, html, sizeof html);
    CHECK(has(html, "<option value=\"static\" selected>"));
    CHECK(has(html, "name=\"gateway\" size=\"16\" maxlength=\"15\" value=\"192.168.7.1\">"));
    CHECK(has(html, "name=\"dns2\" size=\"16\" maxlength=\"15\" value=\"\">"));
    /* blanks around an address, one field alone */
    CHECK_EQ_I(apply(&c, "gateway=+192.168.1.1+", &out), SETTINGS_OK);
    CHECK(out.lan_gateway == 0xC0A80101u && out.lan_dhcp == 1);
    c = out;
    CHECK_EQ_I(apply(&c, "gateway=", &out), SETTINGS_OK);
    CHECK(out.lan_gateway == 0);
    base(&c);

    /* not acceptable */
    CHECK_EQ_I(apply(&c, "address=192.168.1", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[network] address: '192.168.1' is not an IPv4 address") == 0);
    CHECK_EQ_I(apply(&c, "address=", &out), SETTINGS_INVALID);
    CHECK_EQ_I(apply(&c, "lan=auto", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[network] lan: 'auto' is not one of: dhcp, static") == 0);
    CHECK_EQ_I(apply(&c, "lan=static&address=192.168.7.9&gateway=10.0.0.1", &out), SETTINGS_INVALID);
    CHECK(strstr(err, "gateway: 10.0.0.1 is not in the network of address 192.168.7.9") != NULL);
    CHECK_EQ_I(apply(&c, "netmask=255.0.255.0", &out), SETTINGS_INVALID);
    CHECK_EQ_I(apply(&c, "dns1=999.1.1.1", &out), SETTINGS_INVALID);
}

/* the settings are spread over five pages, each with its own form */
/* the Consoles tab: the dreamcast switches and the saturn matchmaking
 * switch on one page per port (/dreamcast, /dreamcast2), shown only while a
 * port uses the dreamcast or saturn service */
static void test_consoles_page(void)
{
    cfg_t c, out;

    CHECK_EQ_I(settings_page_of("/dreamcast", 10), SETTINGS_CONSOLES);
    CHECK_EQ_I(settings_page_of("/dreamcast2", 11), SETTINGS_CONSOLES2);
    CHECK(strcmp(settings_page_path(SETTINGS_CONSOLES2), "/dreamcast2") == 0);
    /* no console port: no tab */
    base(&c);
    CHECK(settings_render_page(&c, SETTINGS_LINE, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "/dreamcast"));
    /* a saturn port alone brings the tab up too */
    c.port2.service = CFG_SERVICE_SATURN;
    CHECK(settings_render_page(&c, SETTINGS_LINE, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<a href=\"/dreamcast\">Consoles</a>"));        /* the section leads to it first */
    base(&c);
    c.service = CFG_SERVICE_DREAMCAST; c.dc_lan = 1;
    c.port2.service = CFG_SERVICE_SATURN; c.port2.sat_match = 1;
    CHECK(settings_render_page(&c, SETTINGS_LINE, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "name=\"dreamcast_now\"")); CHECK(!has(html, "name=\"saturn_matchmaking\""));
    CHECK(has(html, "<a href=\"/dreamcast\">Consoles</a>"));           /* the section leads to the page */
    /* the page itself: both sections, the Dreamcast switches and the Saturn one */
    CHECK(settings_render_page(&c, SETTINGS_CONSOLES, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<b>Consoles</b>")); CHECK(!has(html, "<a href=\"/dreamcast\">Consoles</a>"));
    CHECK(has(html, "<p class=\"sw\"><b>Port 1</b> <a href=\"/dreamcast2\">Port 2</a></p>"));
    CHECK(has(html, "<form method=\"post\" action=\"/dreamcast\">"));
    CHECK(has(html, "<h2>Dreamcast</h2>")); CHECK(has(html, "<h2>Saturn</h2>"));
    CHECK(has(html, "<select name=\"dreamcast_now\">")); CHECK(has(html, "<select name=\"dreamcast_lan\">"));
    CHECK(has(html, "<select name=\"dreamcast_upnp\">")); CHECK(has(html, "<select name=\"dreamcast_iwango\">"));
    CHECK(has(html, "<select name=\"saturn_matchmaking\">")); CHECK(has(html, "saturn.dreampipe.net"));
    CHECK(has(html, "<option value=\"on\" selected>On: the console gets an address"));   /* dc_lan on */
    CHECK(has(html, "The service of this port is Dreamcast"));
    CHECK(has(html, "The service of this port is not Saturn"));
    CHECK(!has(html, "name=\"service\"")); CHECK(!has(html, "name=\"number\""));
    CHECK(settings_render_page(&c, SETTINGS_CONSOLES2, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<p class=\"sw\"><a href=\"/dreamcast\">Port 1</a> <b>Port 2</b></p>"));
    CHECK(has(html, "<input type=\"hidden\" name=\"port\" value=\"2\">"));
    CHECK(has(html, "The service of this port is Saturn"));
    CHECK(has(html, "The service of this port is not Dreamcast"));
    /* posts change only their own switches */
    CHECK_EQ_I(apply(&c, "dreamcast_now=on&dreamcast_upnp=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.dc_now, 1); CHECK_EQ_I(out.dc_upnp, 1); CHECK_EQ_I(out.service, CFG_SERVICE_DREAMCAST);
    CHECK_EQ_I(apply(&c, "port=2&saturn_matchmaking=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.port2.sat_match, 1);
    CHECK_EQ_I(apply(&c, "saturn_matchmaking=maybe", &out), SETTINGS_INVALID);
}

/* port 2 has a Line and a Modem page of its own */
/* a one-port box (an HT801): the Line, Modem and Dreamcast pages show no
 * Port 2 switch, and the port-2 paths do not resolve */
static void test_one_port(void)
{
    cfg_t c, out;

    base(&c); c.service = CFG_SERVICE_DREAMCAST;
    settings_set_two_ports(0);
    CHECK(settings_render_page(&c, SETTINGS_LINE, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "Port 2")); CHECK(!has(html, "/line2"));
    CHECK(settings_render_page(&c, SETTINGS_MODEM, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "/modem2"));
    CHECK(settings_render_page(&c, SETTINGS_CONSOLES, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "/dreamcast2"));
    CHECK_EQ_I(settings_page_of("/line2", 6), -1);
    CHECK_EQ_I(settings_page_of("/modem2", 7), -1);
    CHECK_EQ_I(settings_page_of("/dreamcast2", 11), -1);
    CHECK_EQ_I(settings_page_of("/line", 5), SETTINGS_LINE);     /* port 1 still there */
    /* a port=2 post is refused on a one-port box */
    CHECK_EQ_I(apply(&c, "port=2&rings=3", &out), SETTINGS_BAD);
    settings_set_two_ports(1);                                  /* back for the other tests */
}

static void test_port2(void)
{
    cfg_t c, out;
    int n;

    CHECK_EQ_I(settings_page_of("/line2", 6), SETTINGS_LINE2);
    CHECK_EQ_I(settings_page_of("/modem2", 7), SETTINGS_MODEM2);
    CHECK(strcmp(settings_page_path(SETTINGS_LINE2), "/line2") == 0);
    CHECK(strcmp(settings_page_path(SETTINGS_MODEM2), "/modem2") == 0);

    base(&c);
    strcpy(c.line_number, "5551111"); c.rings = 2;
    strcpy(c.port2.line_number, "5559999"); c.port2.rings = 3; c.port2.line_tones = CFG_TONES_EU;
    c.port2.modulations = CFG_MOD_V34 | CFG_MOD_V21; c.port2.max_down = 28800; c.port2.max_up = 9600;

    /* port 1's pages point at port 2's and the other way round */
    CHECK(settings_render_page(&c, SETTINGS_LINE, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<p class=\"sw\"><b>Port 1</b> <a href=\"/line2\">Port 2</a></p>"));
    CHECK(has(html, "value=\"5551111\"")); CHECK(!has(html, "5559999"));
    CHECK(!has(html, "name=\"port\""));
    CHECK(settings_render_page(&c, SETTINGS_LINE2, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<p class=\"sw\"><a href=\"/line\">Port 1</a> <b>Port 2</b></p>"));
    CHECK(has(html, "<form method=\"post\" action=\"/line2\">"));
    CHECK(has(html, "<input type=\"hidden\" name=\"port\" value=\"2\">"));
    CHECK(has(html, "value=\"5559999\"")); CHECK(!has(html, "5551111"));
    CHECK(has(html, "name=\"rings\" size=\"6\" maxlength=\"5\" value=\"3\""));
    CHECK(has(html, "<option value=\"eu\" selected>"));
    CHECK(has(html, "<b>Line</b>"));
    CHECK(!has(html, "name=\"mod_v90\"")); CHECK(!has(html, "name=\"user0\""));
    CHECK(settings_render_page(&c, SETTINGS_MODEM, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<p class=\"sw\"><b>Port 1</b> <a href=\"/modem2\">Port 2</a></p>"));
    CHECK(has(html, "name=\"mod_v90\" value=\"1\" checked>"));
    CHECK(settings_render_page(&c, SETTINGS_MODEM2, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<p class=\"sw\"><a href=\"/modem\">Port 1</a> <b>Port 2</b></p>"));
    CHECK(has(html, "<input type=\"hidden\" name=\"port\" value=\"2\">"));
    CHECK(has(html, "name=\"mod_v90\" value=\"1\">"));
    CHECK(has(html, "name=\"mod_v34\" value=\"1\" checked>"));
    CHECK(has(html, "name=\"max_downstream\" size=\"6\" maxlength=\"5\" value=\"28800\""));
    CHECK(has(html, "<option value=\"on\" selected>On: offer"));
    CHECK(has(html, "<b>Modem</b>"));
    /* V.92 per port: the same field name, the hidden port=2 says whose it is */
    CHECK_EQ_I(apply(&c, "port=2&v92=off", &out), SETTINGS_OK);
    CHECK_EQ_I(out.port2.v92, 0); CHECK_EQ_I(out.v92, 1);
    CHECK(settings_render_page(&out, SETTINGS_MODEM2, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<select name=\"v92\"><option value=\"off\" selected>") && has(html, "<option value=\"on\">On: offer"));
    CHECK(settings_render_page(&out, SETTINGS_MODEM, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<option value=\"on\" selected>On: offer"));
    CHECK(settings_render_page(&out, SETTINGS_LINE, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "name=\"v92\""));
    CHECK_EQ_I(apply(&c, "v92=off", &out), SETTINGS_OK);
    CHECK_EQ_I(out.v92, 0); CHECK_EQ_I(out.port2.v92, 1);
    CHECK_EQ_I(apply(&c, "port=2&v92=1", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[modem2] v92: '1' is not one of: on, off") == 0);
    CHECK_EQ_I(apply(&c, "port=2&max_upstream=48000", &out), SETTINGS_OK);
    CHECK_EQ_I(out.port2.max_up, 48000); CHECK_EQ_I(out.max_up, 33600);
    CHECK_EQ_I(apply(&c, "port=2&max_upstream=48001", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[modem2] max_upstream: 48001 is out of range 300..48000") == 0);

    /* a post with port=2 changes port 2 and nothing of port 1 */
    CHECK_EQ_I(apply(&c, "port=2&number=777&rings=4&tones=us", &out), SETTINGS_OK);
    CHECK(strcmp(out.port2.line_number, "777") == 0); CHECK_EQ_I(out.port2.rings, 4);
    CHECK_EQ_I(out.port2.line_tones, CFG_TONES_US);
    CHECK(strcmp(out.line_number, "5551111") == 0); CHECK_EQ_I(out.rings, 2);
    CHECK_EQ_I(out.port2.modulations, CFG_MOD_V34 | CFG_MOD_V21);
    CHECK_EQ_I(apply(&c, "mods=1&mod_v22bis=1&max_upstream=2400&port=2", &out), SETTINGS_OK);
    CHECK_EQ_I(out.port2.modulations, CFG_MOD_V22BIS); CHECK_EQ_I(out.port2.max_up, 2400);
    CHECK_EQ_I(out.modulations, CFG_MOD_DEFAULT); CHECK_EQ_I(out.max_up, 33600);
    CHECK(out.port2.ppp_peer == c.port2.ppp_peer && out.ppp_peer == c.ppp_peer);
    /* port=1 is port 1 */
    CHECK_EQ_I(apply(&c, "port=1&number=5", &out), SETTINGS_OK);
    CHECK(strcmp(out.line_number, "5") == 0); CHECK(strcmp(out.port2.line_number, "5559999") == 0);
    /* shared settings in the same post stay shared */
    CHECK_EQ_I(apply(&c, "port=2&auth=pap&number=1", &out), SETTINGS_OK);
    CHECK_EQ_I(out.auth, CFG_AUTH_PAP); CHECK(strcmp(out.port2.line_number, "1") == 0);
    /* what is wrong is named with the port's section */
    CHECK_EQ_I(apply(&c, "port=2&rings=99", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[modem2] rings: 99 is out of range 1..10") == 0);
    CHECK_EQ_I(apply(&c, "port=2&tones=xx", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[line2] tones: 'xx' is not one of: us, eu") == 0);
    CHECK_EQ_I(apply(&c, "port=2&max_downstream=x", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[modem2] max_downstream: 'x' is not a number") == 0);
    CHECK_EQ_I(apply(&c, "port=2&auth=zz", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] auth: 'zz' is not one of: none, pap, chap") == 0);
    /* not a port */
    CHECK_EQ_I(apply(&c, "port=3&number=5", &out), SETTINGS_BAD);
    CHECK_EQ_I(apply(&c, "port=&number=5", &out), SETTINGS_BAD);
    CHECK_EQ_I(apply(&c, "port=2&port=2", &out), SETTINGS_BAD);

    /* untouched forms of port 2 post back to the config they came from */
    CHECK(settings_render_page(&c, SETTINGS_LINE2, 0, NULL, html, sizeof html) > 0);
    n = submit(html, body, sizeof body);
    CHECK_EQ_I(n, 9);                       /* the three switches of the Dreamcast tab are not among them */
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&out, &c, sizeof c) == 0);
    CHECK(settings_render_page(&c, SETTINGS_MODEM2, 0, NULL, html, sizeof html) > 0);
    n = submit(html, body, sizeof body);
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&out, &c, sizeof c) == 0);
}

static void test_pages(void)
{
    static const struct { int page; const char *path, *tab, *has, *hasnt[4]; } pg[] = {
        { SETTINGS_LINE,    "/line",    "Line",    "name=\"number\"",
          { "name=\"mod_v90\"", "name=\"user0\"", "name=\"address\"", "name=\"admin_password\"" } },
        { SETTINGS_MODEM,   "/modem",   "Modem",   "name=\"mod_v90\"",
          { "name=\"number\"", "name=\"user0\"", "name=\"address\"", "name=\"admin_password\"" } },
        { SETTINGS_USERS,   "/users",   "Users",   "name=\"add\" value=\"Add\"",
          { "name=\"number\"", "name=\"mod_v90\"", "name=\"address\"", "name=\"admin_password\"" } },
        { SETTINGS_NETWORK, "/network", "Network", "name=\"gateway\"",
          { "name=\"number\"", "name=\"mod_v90\"", "name=\"user0\"", "name=\"admin_password\"" } },
        { SETTINGS_SYSTEM,  "/system",  "System",  "name=\"admin_password\"",
          { "name=\"number\"", "name=\"mod_v90\"", "name=\"user0\"", "name=\"address\"" } },
    };
    cfg_t c, out;
    char want[80];
    size_t i, k;

    base(&c);
    for (i = 0; i < sizeof pg / sizeof pg[0]; i++) {
        int n = settings_render_page(&c, pg[i].page, 0, NULL, html, sizeof html);
        CHECK(n > 0 && n == (int)strlen(html));
        snprintf(want, sizeof want, "<form method=\"post\" action=\"%s\">", pg[i].path);
        CHECK(has(html, want));
        snprintf(want, sizeof want, "<b>%s</b>", pg[i].tab);
        CHECK(has(html, want));
        CHECK(has(html, pg[i].has));
        for (k = 0; k < 4; k++) CHECK(!has(html, pg[i].hasnt[k]));
        CHECK(has(html, "<a href=\"/\">Status</a>") && has(html, "href=\"/logout\">Log out</a>"));
        CHECK(!has(html, "href=\"/config\"") && !has(html, "href=\"/settings\""));
        CHECK(has(html, "<input type=\"submit\" value=\"Save\">"));
        /* the restart form is on the system page only */
        CHECK(has(html, "action=\"/restart\"") == (pg[i].page == SETTINGS_SYSTEM));
        /* an untouched page posts back to exactly the config it came from */
        CHECK(submit(html, body, sizeof body) > 0);
        CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
        CHECK(memcmp(&c, &out, sizeof c) == 0);
        /* saved and error lines */
        CHECK(settings_render_page(&c, pg[i].page, 1, NULL, html, sizeof html) > 0);
        CHECK(has(html, "<p><b>Settings saved.</b></p>"));
        CHECK(settings_render_page(&c, pg[i].page, 0, "x<y", html, sizeof html) > 0);
        CHECK(has(html, "<p><b>Not saved:</b> x&lt;y</p>"));
    }
    /* the path of a page and back */
    CHECK(strcmp(settings_page_path(SETTINGS_NETWORK), "/network") == 0);
    CHECK(strcmp(settings_page_path(SETTINGS_ALL), "/settings") == 0);
    CHECK_EQ_I(settings_page_of("/users", 6), SETTINGS_USERS);
    CHECK_EQ_I(settings_page_of("/settings", 9), SETTINGS_ALL);
    CHECK_EQ_I(settings_page_of("/user", 5), -1);
    CHECK_EQ_I(settings_page_of("/usersx", 7), -1);
}

static void test_apply(void)
{
    cfg_t cur, out, want;

    /* everything a browser sends */
    base(&cur);
    CHECK_EQ_I(apply(&cur,
        "number=%2A70%235550000&tones=eu&rings=3&busy_chance=25&drop_mean=90&session_limit=240"
        "&idle_timeout=15&mods=1&mod_v90=1&mod_v22=1&max_downstream=33600&max_upstream=9600&v92=on"
        "&auth=pap&user0=alice&pass0=&user1=bob&pass1=new+pass%21&user2=&pass2=&user3=dave&pass3=d%26d"
        "&user4=&pass4=&user5=&pass5=&user6=&pass6=&user7=&pass7=&user8=&pass8=&user9=&pass9="
        "&user10=&pass10=&user11=&pass11=&user12=&pass12=&user13=&pass13=&user14=&pass14="
        "&user15=&pass15=&admin_password=hunter2", &out), SETTINGS_OK);
    CHECK(err[0] == '\0');
    want = cur;
    strcpy(want.line_number, "*70#5550000");
    want.line_tones = CFG_TONES_EU; want.rings = 3; want.busy_chance = 25;
    want.drop_mean_min = 90; want.session_limit_min = 240; want.idle_timeout_min = 15;
    want.modulations = CFG_MOD_V90 | CFG_MOD_V22;
    want.max_down = 33600; want.max_up = 9600; want.v92 = 1;
    want.auth = CFG_AUTH_PAP;
    memset(want.users, 0, sizeof want.users);
    want.n_users = 3;
    strcpy(want.users[0].name, "alice"); strcpy(want.users[0].secret, "SECRET-A");  /* kept */
    strcpy(want.users[1].name, "bob");   strcpy(want.users[1].secret, "new pass!"); /* changed */
    strcpy(want.users[2].name, "dave");  strcpy(want.users[2].secret, "d&d");       /* carol gone */
    strcpy(want.admin_password, "hunter2");
    CHECK(memcmp(&out, &want, sizeof out) == 0);
    CHECK_EQ_I(out.mru, 576);
    CHECK_EQ_I(out.status_port, 8080);
    CHECK_EQ_I(cfg_validate(&out, err, sizeof err), CFG_OK);

    /* nothing posted, nothing changed; one field posted, one field changed */
    CHECK_EQ_I(apply(&cur, "", &out), SETTINGS_OK);
    CHECK(memcmp(&out, &cur, sizeof out) == 0);
    CHECK_EQ_I(apply(&cur, "busy_chance=50", &out), SETTINGS_OK);
    want = cur; want.busy_chance = 50;
    CHECK(memcmp(&out, &want, sizeof out) == 0);

    /* number: empty and `any` mean any */
    CHECK_EQ_I(apply(&cur, "number=", &out), SETTINGS_OK);
    CHECK(out.line_number[0] == '\0');
    CHECK_EQ_I(apply(&cur, "number=any", &out), SETTINGS_OK);
    CHECK(out.line_number[0] == '\0');
    CHECK_EQ_I(apply(&cur, "number=01234567890123456789", &out), SETTINGS_OK);
    CHECK(strcmp(out.line_number, "01234567890123456789") == 0);
    /* numbers may have blanks around them and leading zeros */
    CHECK_EQ_I(apply(&cur, "rings=+07+&idle_timeout=%2010", &out), SETTINGS_OK);
    CHECK_EQ_I(out.rings, 7);
    CHECK_EQ_I(out.idle_timeout_min, 10);
    /* percent escapes in either case, + is a blank, %2B is a plus */
    CHECK_EQ_I(apply(&cur, "user5=a%2bb+c%2Fd&pass5=%7e%7E+%2B", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_users, 4);
    CHECK(strcmp(out.users[3].name, "a+b c/d") == 0);
    CHECK(strcmp(out.users[3].secret, "~~ +") == 0);
    /* octets above 127 are taken as they come */
    CHECK_EQ_I(apply(&cur, "user0=jos%C3%A9&pass0=%ff%01", &out), SETTINGS_OK);
    CHECK(strcmp(out.users[0].name, "jos\xc3\xa9") == 0);
    CHECK(strcmp(out.users[0].secret, "\xff\x01") == 0);
    CHECK_EQ_I(apply(&cur, "user0=jos\xc3\xa9&pass0=x", &out), SETTINGS_OK);
    CHECK(strcmp(out.users[0].name, "jos\xc3\xa9") == 0);

    /* modulations: with `mods` the checkboxes are the whole set */
    CHECK_EQ_I(apply(&cur, "mods=1&mod_v34=1&mod_v32bis=1", &out), SETTINGS_OK);
    CHECK_EQ_I(out.modulations, CFG_MOD_V34 | CFG_MOD_V32BIS);
    CHECK_EQ_I(apply(&cur, "mods=1&mod_v21=1", &out), SETTINGS_OK);
    CHECK_EQ_I(out.modulations, CFG_MOD_V21);
    CHECK_EQ_I(apply(&cur, "mod_v21=1&mods=1", &out), SETTINGS_OK);
    CHECK_EQ_I(out.modulations, CFG_MOD_V21);
    CHECK_EQ_I(apply(&cur, "mods=1", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[modem] modulations: empty list") == 0);
    /* without it each field switches one modulation */
    CHECK_EQ_I(apply(&cur, "mod_v90=0", &out), SETTINGS_OK);
    CHECK_EQ_I(out.modulations, CFG_MOD_DEFAULT & ~CFG_MOD_V90);
    cur.modulations = CFG_MOD_V90;
    CHECK_EQ_I(apply(&cur, "mod_v22bis=1&mod_v90=0", &out), SETTINGS_OK);
    CHECK_EQ_I(out.modulations, CFG_MOD_V22BIS);
    CHECK_EQ_I(apply(&cur, "mods=0&mod_v21=1", &out), SETTINGS_OK);
    CHECK_EQ_I(out.modulations, CFG_MOD_V90 | CFG_MOD_V21);
    cur.modulations = CFG_MOD_ALL;

    /* the admin password: kept, set, removed */
    strcpy(cur.admin_password, "old");
    CHECK_EQ_I(apply(&cur, "admin_password=", &out), SETTINGS_OK);
    CHECK(strcmp(out.admin_password, "old") == 0);
    CHECK_EQ_I(apply(&cur, "admin_password=n%65w", &out), SETTINGS_OK);
    CHECK(strcmp(out.admin_password, "new") == 0);
    CHECK_EQ_I(apply(&cur, "admin_password=&admin_clear=1", &out), SETTINGS_OK);
    CHECK(out.admin_password[0] == '\0');
    CHECK_EQ_I(apply(&cur, "admin_clear=0", &out), SETTINGS_OK);
    CHECK(strcmp(out.admin_password, "old") == 0);
    CHECK_EQ_I(apply(&cur, "admin_clear=1&admin_password=x", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[status] admin_password: a new password and admin_clear together") == 0);
    CHECK_EQ_I(apply(&cur, "admin_password="
        "0123456789012345678901234567890123456789012345678901234567890123", &out), SETTINGS_OK);
    CHECK_EQ_I(strlen(out.admin_password), 64);
    CHECK_EQ_I(apply(&cur, "admin_password="
        "01234567890123456789012345678901234567890123456789012345678901234", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[status] admin_password: password is longer than 64 octets") == 0);
    CHECK(strcmp(out.admin_password, "old") == 0);
}

static void test_users(void)
{
    cfg_t cur, out;
    char b[512];

    base(&cur);
    /* empty password, known name: kept, also when the rows move */
    CHECK_EQ_I(apply(&cur, "user0=carol&pass0=&user1=alice&pass1=&user2=bob&pass2=", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_users, 3);
    CHECK(strcmp(out.users[0].name, "carol") == 0 && strcmp(out.users[0].secret, "") == 0);
    CHECK(strcmp(out.users[1].name, "alice") == 0 && strcmp(out.users[1].secret, "SECRET-A") == 0);
    CHECK(strcmp(out.users[2].name, "bob") == 0 && strcmp(out.users[2].secret, "SECRET-B") == 0);
    /* only a password posted: that row's password changes */
    CHECK_EQ_I(apply(&cur, "pass1=fresh", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_users, 3);
    CHECK(strcmp(out.users[1].name, "bob") == 0 && strcmp(out.users[1].secret, "fresh") == 0);
    CHECK(strcmp(out.users[0].secret, "SECRET-A") == 0);
    /* a row removed: the rest closes up, unused rows are all zero */
    CHECK_EQ_I(apply(&cur, "user0=", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_users, 2);
    CHECK(strcmp(out.users[0].name, "bob") == 0 && strcmp(out.users[0].secret, "SECRET-B") == 0);
    CHECK(strcmp(out.users[1].name, "carol") == 0);
    CHECK(out.users[2].name[0] == '\0' && out.users[2].secret[0] == '\0');
    {
        cfg_t z;
        memset(&z, 0, sizeof z);
        CHECK(memcmp(&out.users[2], &z.users[2], sizeof out.users[2]) == 0);
    }
    /* a new name in a far row */
    CHECK_EQ_I(apply(&cur, "user15=zed&pass15=zz", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_users, 4);
    CHECK(strcmp(out.users[3].name, "zed") == 0 && strcmp(out.users[3].secret, "zz") == 0);
    /* a new or renamed user needs a password: no silent empty secret */
    CHECK_EQ_I(apply(&cur, "user1=robert&pass1=", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user 'robert': a new user needs a password") == 0);
    CHECK_EQ_I(out.n_users, 3);                 /* the form shows the name again */
    CHECK(strcmp(out.users[1].name, "robert") == 0 && out.users[1].secret[0] == '\0');
    CHECK_EQ_I(apply(&cur, "user7=new", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user 'new': a new user needs a password") == 0);
    CHECK_EQ_I(apply(&cur, "user1=robert&pass1=r", &out), SETTINGS_OK);
    CHECK(strcmp(out.users[1].name, "robert") == 0 && strcmp(out.users[1].secret, "r") == 0);
    CHECK_EQ_I(apply(&cur, "user9=%3Cb%3E%01&pass9=", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user '<b>?': a new user needs a password") == 0);
    /* a password that belongs to no name */
    CHECK_EQ_I(apply(&cur, "user1=&pass1=oops", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user 2: password without a name") == 0);
    CHECK_EQ_I(apply(&cur, "pass9=oops", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user 10: password without a name") == 0);
    /* the same name twice */
    CHECK_EQ_I(apply(&cur, "user1=alice&pass1=", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user: name 'alice' already defined") == 0);
    /* all users removed while authentication is on */
    CHECK_EQ_I(apply(&cur, "user0=&user1=&user2=", &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] auth: chap needs at least one user") == 0);
    CHECK_EQ_I(out.n_users, 0);
    CHECK_EQ_I(apply(&cur, "user0=&user1=&user2=&auth=none", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_users, 0);
    CHECK_EQ_I(out.auth, CFG_AUTH_NONE);
    /* lengths: 63 and 64 octets fit, one more does not */
    snprintf(b, sizeof b, "user3=%063d&pass3=%064d", 0, 0);
    CHECK_EQ_I(apply(&cur, b, &out), SETTINGS_OK);
    CHECK_EQ_I(strlen(out.users[3].name), 63);
    CHECK_EQ_I(strlen(out.users[3].secret), 64);
    snprintf(b, sizeof b, "user3=%064d&pass3=x", 0);
    CHECK_EQ_I(apply(&cur, b, &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user 4: name is longer than 63 octets") == 0);
    snprintf(b, sizeof b, "user3=x&pass3=%065d", 0);
    CHECK_EQ_I(apply(&cur, b, &out), SETTINGS_INVALID);
    CHECK(strcmp(err, "[ppp] user 4: password is longer than 64 octets") == 0);
    snprintf(b, sizeof b, "user3=%0300d&pass3=x", 0);
    CHECK_EQ_I(apply(&cur, b, &out), SETTINGS_INVALID);
    /* a full table */
    {
        cfg_t full;
        int i, n = 0;
        cfg_default(&full);
        for (i = 0; i < CFG_MAX_USERS; i++)
            n += snprintf(body + n, sizeof body - (size_t)n, "%suser%d=u%d&pass%d=p%d", i ? "&" : "", i, i, i, i);
        CHECK_EQ_I(apply(&full, body, &out), SETTINGS_OK);
        CHECK_EQ_I(out.n_users, CFG_MAX_USERS);
        CHECK(strcmp(out.users[15].name, "u15") == 0 && strcmp(out.users[15].secret, "p15") == 0);
    }
    /* the page: the users there are and one new row; Add fills it */
    {
        cfg_t c2, o2; int k;
        base(&c2);
        CHECK(settings_render_page(&c2, SETTINGS_USERS, 0, NULL, html, sizeof html) > 0);
        CHECK(!has(html, "name=\"user3\"") && has(html, "<p><input type=\"submit\" name=\"add\" value=\"Add\" data-tpl="));
        CHECK(has(html, "Add gives the table an empty row for a new user; Save keeps what you fill in."));
        /* the template, outside the form, for the script */
        CHECK(has(html, "</form>\n<template id=\"utpl\"><tr class=\"new\"><td>New</td><td><input type=\"text\" name=\"userIDX\""));
        CHECK(has(html, "name=\"passIDX\"") && has(html, "data-tpl=\"utpl\" data-rows=\"urows\" data-max=\"16\""));
        CHECK(has(html, "<tbody id=\"urows\">"));
        /* Add: the form saved as it is, and the button said so */
        CHECK_EQ_I(apply(&c2, "user0=alice&pass0=&add=Add", &o2), SETTINGS_OK);
        CHECK_EQ_I(settings_added(), 1); CHECK_EQ_I(o2.n_users, 3);
        CHECK_EQ_I(apply(&c2, "user0=alice&pass0=", &o2), SETTINGS_OK); CHECK_EQ_I(settings_added(), 0);
        CHECK_EQ_I(apply(&c2, "add=Add&add=Add", &o2), SETTINGS_BAD);
        /* then the page with an empty row, which a second post fills in */
        CHECK(settings_render_page_add(&c2, SETTINGS_USERS, 1, 1, NULL, html, sizeof html) > 0);
        CHECK(has(html, "<tr class=\"new\"><td>New</td><td><input type=\"text\" name=\"user3\" size=\"24\" maxlength=\"63\" value=\"\">"));
        CHECK(!has(html, "name=\"user4\""));
        CHECK_EQ_I(apply(&c2, "user3=dave&pass3=d4ve", &o2), SETTINGS_OK);
        CHECK_EQ_I(o2.n_users, 4); CHECK(strcmp(o2.users[3].name, "dave") == 0 && strcmp(o2.users[3].secret, "d4ve") == 0);
        /* the next page: dave, and no empty row of its own */
        CHECK(settings_render_page(&o2, SETTINGS_USERS, 0, NULL, html, sizeof html) > 0);
        CHECK(has(html, "value=\"dave\"") && !has(html, "name=\"user4\""));
        /* full: no new row, no Add */
        c2.n_users = CFG_MAX_USERS;
        for (k = 0; k < CFG_MAX_USERS; k++) { snprintf(c2.users[k].name, sizeof c2.users[k].name, "u%d", k); strcpy(c2.users[k].secret, "s"); }
        CHECK(settings_render_page(&c2, SETTINGS_USERS, 0, NULL, html, sizeof html) > 0);
        CHECK(has(html, "name=\"user15\" size=\"24\" maxlength=\"63\" value=\"u15\">") && !has(html, "name=\"user16\""));
        CHECK(!has(html, "value=\"Add\"") && has(html, "as many as the box takes"));
        CHECK(settings_render_page_add(&c2, SETTINGS_USERS, 0, 1, NULL, html, sizeof html) > 0);
        CHECK(!has(html, "name=\"user16\""));            /* Add cannot go past 16 */
    }
}

static void test_invalid(void)
{
    static const struct { const char *body; const char *what; } bad[] = {
        { "number=555-1234", "[line] number: only 0-9 * # are allowed" },
        { "number=555+1234", "[line] number: only 0-9 * # are allowed" },
        { "number=012345678901234567890", "[line] number: longer than 20 characters" },
        { "tones=uk", "[line] tones: 'uk' is not one of: us, eu" },
        { "tones=", "[line] tones: '' is not one of: us, eu" },
        { "tones=US", "[line] tones: 'US' is not one of: us, eu" },
        { "rings=0", "[modem] rings: 0 is out of range 1..10" },
        { "rings=11", "[modem] rings: 11 is out of range 1..10" },
        { "rings=", "[modem] rings: '' is not a number" },
        { "rings=two", "[modem] rings: 'two' is not a number" },
        { "rings=-1", "[modem] rings: '-1' is not a number" },
        { "rings=1.5", "[modem] rings: '1.5' is not a number" },
        { "rings=1+1", "[modem] rings: '1 1' is not a number" },
        { "rings=0x2", "[modem] rings: '0x2' is not a number" },
        { "rings=1234567890", "[modem] rings: '1234567890' is not a number" },
        { "rings=%3Cb%3E%ff", "[modem] rings: '<b>?' is not a number" },
        { "busy_chance=101", "[line] busy_chance: 101 is out of range 0..100" },
        { "busy_chance=50%25", "[line] busy_chance: '50%' is not a number" },
        { "drop_mean=10081", "[line] drop_mean: 10081 is out of range 0..10080" },
        { "session_limit=999999999", "[line] session_limit: 999999999 is out of range 0..10080" },
        { "idle_timeout=10081", "[line] idle_timeout: 10081 is out of range 0..10080" },
        { "auth=mschap", "[ppp] auth: 'mschap' is not one of: none, pap, chap" },
        { "max_downstream=56001", "[modem] max_downstream: 56001 is out of range 300..56000" },
        { "max_upstream=299", "[modem] max_upstream: 299 is out of range 300..48000" },
        { "max_upstream=48001", "[modem] max_upstream: 48001 is out of range 300..48000" },
        { "v92=1", "[modem] v92: '1' is not one of: on, off" },
        { "v92=", "[modem] v92: '' is not one of: on, off" },
        { "mod_v90=on", "[modem] mod_v90: 'on' is not 0 or 1" },
        { "mod_v21=", "[modem] mod_v21: '' is not 0 or 1" },
        { "mods=yes", "[modem] mods: 'yes' is not 0 or 1" },
        { "admin_clear=2", "[status] admin_clear: '2' is not 0 or 1" },
        /* the first problem is the one reported */
        { "rings=99&busy_chance=x", "[line] busy_chance: 'x' is not a number" },
        { "busy_chance=x&tones=uk", "[line] busy_chance: 'x' is not a number" },
    };
    cfg_t cur, out;
    size_t i;

    base(&cur);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int r = apply(&cur, bad[i].body, &out);
        CHECK(r == SETTINGS_INVALID && strcmp(err, bad[i].what) == 0);
        if (r != SETTINGS_INVALID || strcmp(err, bad[i].what) != 0)
            printf("  case %d: r %d, want \"%s\"\n           got \"%s\"\n", (int)i, r, bad[i].what, err);
    }
    /* the candidate keeps what was understood, for the form */
    CHECK_EQ_I(apply(&cur, "rings=5&busy_chance=x&tones=eu", &out), SETTINGS_INVALID);
    CHECK_EQ_I(out.rings, 5);
    CHECK_EQ_I(out.busy_chance, 10);
    CHECK_EQ_I(out.line_tones, CFG_TONES_EU);
    CHECK_EQ_I(apply(&cur, "number=555-1234", &out), SETTINGS_INVALID);
    CHECK(strcmp(out.line_number, "555-1234") == 0);
    /* tiny and absent message buffers */
    {
        char small[8];
        memset(small, '#', sizeof small);
        CHECK_EQ_I(settings_apply(&cur, "rings=0", 7, &out, small, 4), SETTINGS_INVALID);
        CHECK(memcmp(small, "[mo\0####", 8) == 0);
        CHECK_EQ_I(settings_apply(&cur, "rings=0", 7, &out, NULL, 0), SETTINGS_INVALID);
        CHECK_EQ_I(settings_apply(&cur, "rings=2", 7, &out, NULL, 0), SETTINGS_OK);
    }
}

static void test_malformed(void)
{
    static const char *const bad[] = {
        "rings",                        /* no = */
        "=3",
        "&",
        "rings=3&",
        "&rings=3",
        "rings=3&&tones=eu",
        "rings=3&rings=3",              /* twice */
        "user3=a&pass3=b&user3=a",
        "mod_v90=1&mod_v90=1",
        "ring=3",                       /* unknown */
        "Rings=3",
        "rings%20=3",
        "r%69ngs=3",
        "user16=x&pass16=y",
        "user=x",
        "user00=x",
        "user1x=x",
        "user-1=x",
        "pass=x",
        "pass016=x",
        "wan=dhcp",
        "addr=10.0.0.1",
        "mru=1500",
        "port=81",
        "listen=off",
        "rings=3;tones=eu",
        "rings=%",                      /* percent escapes */
        "rings=%1",
        "rings=%zz",
        "rings=%1g",
        "rings=3%",
        "rings=%u0033",
        "user3=a%00b&pass3=x",          /* NUL */
        "pass3=%00",
        "rings=%00",
        "rings=3=4",
        "rings=3\r\n",
        "rings=3\n",
        "user3=a b&pass3=x",            /* raw blank and control octets */
        "user3=a\tb&pass3=x",
        "user3=a\x7f&pass3=x",
    };
    cfg_t cur, out;
    size_t i;

    base(&cur);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int r;
        memset(&out, 0x55, sizeof out);
        r = apply(&cur, bad[i], &out);
        CHECK(r == SETTINGS_BAD && memcmp(&out, &cur, sizeof out) == 0);
        if (r != SETTINGS_BAD) printf("  case %d: r %d \"%s\"\n", (int)i, r, err);
    }
    /* a raw NUL octet anywhere */
    CHECK_EQ_I(settings_apply(&cur, "rings=3\0", 8, &out, err, sizeof err), SETTINGS_BAD);
    CHECK_EQ_I(settings_apply(&cur, "rings=\0" "3", 8, &out, err, sizeof err), SETTINGS_BAD);
    CHECK_EQ_I(settings_apply(&cur, "ri\0gs=3", 7, &out, err, sizeof err), SETTINGS_BAD);
    CHECK_EQ_I(settings_apply(&cur, "\0", 1, &out, err, sizeof err), SETTINGS_BAD);
    /* malformed wins over invalid, wherever it comes */
    CHECK_EQ_I(apply(&cur, "rings=99&tones=%", &out), SETTINGS_BAD);
    CHECK_EQ_I(apply(&cur, "rings=%&tones=uk", &out), SETTINGS_BAD);
    /* only len octets are read */
    CHECK_EQ_I(settings_apply(&cur, "rings=3&bogus=1", 7, &out, err, sizeof err), SETTINGS_OK);
    CHECK_EQ_I(out.rings, 3);
    CHECK_EQ_I(settings_apply(&cur, "rings=3&bogus=1", 0, &out, err, sizeof err), SETTINGS_OK);
}

/* ---- page and decoder together --------------------------------------------- */

static void test_round_trip(void)
{
    cfg_t c, out;
    int n, i;

    /* an untouched form posts back to exactly the config it came from */
    cfg_default(&c);
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    n = submit(html, body, sizeof body);
    CHECK_EQ_I(n, 7 + 1 + 1 + 6 + 2 + 2 * 1 + 6 + 1 + 1 + 1 + 1 + 5 + 1 + 1 + 1 + 1 + 1 + 1);     /* no user: the new row only */     /* service, v92, x2, k56, compression, encryption, the four Dreamcast switches, the Saturn one, update_check, dns_relay, ipv6 */
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&c, &out, sizeof c) == 0);

    base(&c);
    strcpy(c.admin_password, "ADMIN-SECRET");
    c.modulations = CFG_MOD_V22;
    c.v92 = 1; c.max_up = 48000;
    c.line_tones = CFG_TONES_EU;
    strcpy(c.users[0].name, "\"><i>&' +%=;\\");
    strcpy(c.users[1].name, "jos\xc3\xa9 \xe2\x82\xac");
    CHECK(settings_render_html(&c, 1, NULL, html, sizeof html) > 0);
    submit(html, body, sizeof body);
    CHECK(!has(body, "SECRET"));
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&c, &out, sizeof c) == 0);

    /* rows that cannot be edited stay as they are, in place */
    memset(c.users[1].name, 0, sizeof c.users[1].name);
    strcpy(c.users[1].name, "caf\xe9\x01");
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    n = submit(html, body, sizeof body);
    CHECK_EQ_I(n, 7 + 1 + 1 + 1 + 2 + 2 * 3 - 1 + 6 + 1 + 1 + 1 + 1 + 5 + 1 + 1 + 1 + 1 + 1 + 1);     /* the three users */     /* + update_check, x2, k56, encryption, dns_relay, ipv6 */
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&c, &out, sizeof c) == 0);

    /* the largest post a browser can make from the form fits the limit:
     * every name and password at full length, every octet as %HH */
    worst(&c);
    c.rings = 10; c.busy_chance = 100; c.drop_mean_min = c.session_limit_min = 10080;
    c.idle_timeout_min = 10080; c.max_down = 56000; c.max_up = 48000;
    memset(c.line_number, '#', CFG_NUMBER_MAX);
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    submit(html, body, sizeof body);
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&c, &out, sizeof c) == 0);
    {
        char *p = body + strlen(body);
        /* now type a 64 octet password into every field */
        for (i = 0; i < CFG_MAX_USERS; i++) {
            char f[24], *q;
            int k;
            snprintf(f, sizeof f, "pass%d=", i);
            q = strstr(body, f);
            CHECK(q != NULL && (q[strlen(f)] == '&' || q[strlen(f)] == '\0'));
            if (!q) break;
            q += strlen(f);
            memmove(q + 3 * CFG_SECRET_MAX, q, strlen(q) + 1);
            for (k = 0; k < CFG_SECRET_MAX; k++) memcpy(q + 3 * k, "%22", 3);
        }
        p = strstr(body, "admin_password=");
        CHECK(p != NULL && p[15] == '\0');
        if (p) for (i = 0; i < CFG_SECRET_MAX; i++) strcat(p, "%22");
    }
    n = (int)strlen(body);
    printf("  largest form body %d of %d bytes\n", n, SETTINGS_BODY_MAX);
    CHECK(n > 6000 && n <= SETTINGS_BODY_MAX);
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    for (i = 0; i < CFG_MAX_USERS; i++) {
        CHECK(memcmp(out.users[i].name, c.users[i].name, sizeof c.users[i].name) == 0);
        CHECK_EQ_I(strspn(out.users[i].secret, "\""), CFG_SECRET_MAX);
    }
    CHECK_EQ_I(strspn(out.admin_password, "\""), CFG_SECRET_MAX);
}

static uint32_t rnd_state = 0x9e3779b9u;
static uint32_t rnd(void)
{
    rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5;
    return rnd_state;
}

/* random and damaged bodies: the decoder returns one of its three answers,
 * an accepted config is valid, a refused body changes nothing, and what
 * comes out can always be rendered */
static void test_fuzz(void)
{
    static const char *const words[] = { "rings", "tones", "number", "user0", "pass0", "user15", "pass15",
        "mods", "mod_v90", "admin_password", "admin_clear", "auth", "busy_chance", "max_upstream", "v92", "on",
        "=", "=", "=", "&", "&", "%", "%41", "%00", "+", "1", "0", "eu", "chap", "x", "5551234", "\xff" };
    static const char seed[] = "number=5550000&tones=eu&rings=3&mods=1&mod_v90=1&auth=pap"
                               "&user0=alice&pass0=&user4=dave&pass4=dd&admin_password=";
    cfg_t cur, out;
    int iter, counts[3] = { 0, 0, 0 }, wrong = 0;

    base(&cur);
    for (iter = 0; iter < 5000; iter++) {
        size_t n = 0, i;
        int r, kind = (int)(rnd() % 3u);
        if (kind == 0) {
            n = rnd() % 200u;
            for (i = 0; i < n; i++) body[i] = (char)rnd();
        } else if (kind == 1) {
            unsigned k = 1u + rnd() % 3u;
            n = sizeof seed - 1;
            memcpy(body, seed, n);
            while (k--) body[rnd() % n] = rnd() % 4u ? "&=%+0123456789abcx"[rnd() % 18u] : (char)rnd();
        } else {
            unsigned k = 1u + rnd() % 14u;
            while (k--) {
                const char *w = words[rnd() % 30u];
                memcpy(body + n, w, strlen(w));
                n += strlen(w);
            }
        }
        memset(&out, 0xaa, sizeof out);
        r = settings_apply(&cur, body, n, &out, err, sizeof err);
        if (r == SETTINGS_OK) {
            counts[0]++;
            if (cfg_validate(&out, NULL, 0) != CFG_OK || err[0] != '\0') wrong++;
        } else if (r == SETTINGS_INVALID) {
            counts[1]++;
            if (err[0] == '\0' || strlen(err) >= sizeof err) wrong++;
        } else if (r == SETTINGS_BAD) {
            counts[2]++;
            if (memcmp(&out, &cur, sizeof out) != 0) wrong++;
        } else {
            wrong++;
        }
        if (settings_render_html(&out, 0, r == SETTINGS_INVALID ? err : NULL, html, sizeof html) <= 0) wrong++;
        if (has(html, "SECRET")) wrong++;
        if (wrong) { printf("  iter %d kind %d r %d\n", iter, kind, r); break; }
    }
    CHECK_EQ_I(wrong, 0);
    printf("  fuzz: ok=%d invalid=%d malformed=%d\n", counts[0], counts[1], counts[2]);
    CHECK(counts[0] > 0 && counts[1] > 0 && counts[2] > 0);
}

/* the Services page: the owner's numbers, the switch, Netstalgia's list */
static int rows_called; static size_t rows_fill;
static size_t fill_rows(char *buf, size_t cap, int *shown, int *left)
{
    size_t n = 0; static const char row[] = "<tr><td>13855961775</td><td>&quot;&quot;&quot;&quot;&quot;&quot;&quot;&quot;</td><td>raw</td><td>h:1</td></tr>\n";
    rows_called++; *shown = 0; *left = 0;
    while (n + sizeof row - 1 <= cap && (size_t)*shown < rows_fill) { memcpy(buf + n, row, sizeof row - 1); n += sizeof row - 1; (*shown)++; }
    *left = (int)(rows_fill - (size_t)*shown);
    return n;
}
static void test_services_page(void)
{
    cfg_t c, out; int i, n;
    base(&c);
    CHECK_EQ_I(settings_page_of("/services", 9), SETTINGS_SERVICES);
    CHECK(strcmp(settings_page_path(SETTINGS_SERVICES), "/services") == 0);
    /* nothing of it on the page of all settings, the tab on every page */
    CHECK(settings_render_html(&c, 0, NULL, html, sizeof html) > 0);
    CHECK(!has(html, "snum0") && has(html, "<a href=\"/bbs\">Services</a>"));
    CHECK(settings_render_page(&c, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<span class=\"dd\"><a href=\"/bbs\">BBS</a><b>Numbers</b></span>"));
    settings_set_svc_rows(NULL);
    CHECK(settings_render_page(&c, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
    /* Add on this page too: the numbers there are and one more row */
    {
        cfg_t c3 = c; c3.n_services = 2;
        strcpy(c3.services[0].number, "5550001"); strcpy(c3.services[0].host, "a"); c3.services[0].port = 1; strcpy(c3.services[0].name, "A");
        strcpy(c3.services[1].number, "5550002"); strcpy(c3.services[1].host, "b"); c3.services[1].port = 2; strcpy(c3.services[1].name, "B");
        CHECK(settings_render_page(&c3, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
        CHECK(has(html, "name=\"snum1\"") && !has(html, "name=\"snum2\""));
        CHECK(settings_render_page_add(&c3, SETTINGS_SERVICES, 1, 1, NULL, html, sizeof html) > 0);
        CHECK(has(html, "name=\"snum2\" size=\"13\" maxlength=\"15\" value=\"\"") && !has(html, "name=\"snum3\""));
        CHECK(settings_render_page(&c, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
    }
    /* Enter presses the first button of a form: a Save, before any Add */
    CHECK(settings_render_page(&c, SETTINGS_USERS, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "<form method=\"post\" action=\"/users\">\n<input type=\"submit\" value=\"Save\" class=\"def\""));
    CHECK(strstr(html, "class=\"def\"") < strstr(html, "name=\"add\""));
    /* an error: the rows stay where they were posted, a bad one as it was typed */
    {
        cfg_t c4 = c, o4; c4.n_services = 3;
        strcpy(c4.services[0].number, "5550001"); strcpy(c4.services[0].host, "a"); c4.services[0].port = 1; strcpy(c4.services[0].name, "A");
        strcpy(c4.services[1].number, "5550002"); strcpy(c4.services[1].host, "b"); c4.services[1].port = 2; strcpy(c4.services[1].name, "B");
        strcpy(c4.services[2].number, "5550003"); strcpy(c4.services[2].host, "c"); c4.services[2].port = 3; strcpy(c4.services[2].name, "C");
        CHECK_EQ_I(apply(&c4, "snum0=&shost0=&sport0=&sname0=&snum1=5550002&shost1=b&sport1=99999&sname1=B"
                              "&snum2=5550003&shost2=c&sport2=3&sname2=C", &o4), SETTINGS_INVALID);
        CHECK_EQ_I(o4.n_services, 3);
        CHECK(o4.services[0].number[0] == 0 && strcmp(o4.services[1].number, "5550002") == 0 && strcmp(o4.services[1].host, "b") == 0);
        CHECK(o4.services[1].port == 0 && strcmp(o4.services[2].number, "5550003") == 0);
        /* that page posted again, fixed: the row numbers still mean the same rows */
        CHECK_EQ_I(apply(&c4, "snum0=&shost0=&sport0=&sname0=&snum1=5550002&shost1=b&sport1=22&sname1=B"
                              "&snum2=5550004&shost2=c&sport2=3&sname2=C", &o4), SETTINGS_OK);
        CHECK_EQ_I(o4.n_services, 2);
        CHECK(strcmp(o4.services[0].number, "5550002") == 0 && o4.services[0].port == 22 && strcmp(o4.services[1].number, "5550004") == 0);
        /* the users the same way: a new one without a password keeps its place */
        CHECK_EQ_I(apply(&c4, "user0=alice&pass0=&user1=&pass1=&user2=carol&pass2=&user3=dave&pass3=", &o4), SETTINGS_INVALID);
        CHECK_EQ_I(o4.n_users, 4); CHECK(o4.users[1].name[0] == 0 && strcmp(o4.users[3].name, "dave") == 0);
    }
    CHECK(settings_render_page(&c, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
    /* none yet: one empty row, and Add for more */
    CHECK(has(html, "<a class=\"on\" href=\"/bbs\">Services</a>") && has(html, "name=\"snum0\"") && !has(html, "snum1"));
    CHECK(has(html, "<p><input type=\"submit\" name=\"add\" value=\"Add\" data-tpl="));
    CHECK(has(html, "<select name=\"netstalgia\"><option value=\"off\">Off") && has(html, "<option value=\"on\" selected>On"));
    CHECK(has(html, "No list."));
    CHECK(has(html, "<select name=\"menu\"><option value=\"off\" selected>Off: every call"));

    /* a post from the page */
    CHECK_EQ_I(apply(&c, "snum0=13855961775&shost0=nwnrevival.com&sport0=5000&smode0=raw&sname0=AOL+Neverwinter+Nights"
                         "&snum1=+5559876+&shost1=192.168.1.20&sport1=23&smode1=telnet&sname1=My+%22board%22&netstalgia=off&menu=on", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_services, 2); CHECK_EQ_I(out.svc_netstalgia, 0); CHECK_EQ_I(out.svc_menu, 1);
    CHECK(strcmp(out.services[0].name, "AOL Neverwinter Nights") == 0 && out.services[0].port == 5000);
    CHECK(strcmp(out.services[1].number, "5559876") == 0 && out.services[1].mode == CFG_SVC_TELNET);
    CHECK(strcmp(out.services[1].name, "My \"board\"") == 0);
    CHECK(out.mru == 576 && out.n_users == 3);          /* the rest as it was */
    c = out;
    CHECK(settings_render_page(&c, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "name=\"sname1\" size=\"18\" maxlength=\"47\" value=\"My &quot;board&quot;\">"));
    CHECK(has(html, "<option value=\"telnet\" selected>telnet</option>"));
    CHECK(has(html, "Off: the box does not answer these."));
    /* what a browser sends back is the same config */
    submit(html, body, sizeof body);
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&c, &out, sizeof c) == 0);
    /* one field alone: the others of the row stay */
    CHECK_EQ_I(apply(&c, "sname0=NWN", &out), SETTINGS_OK);
    CHECK(strcmp(out.services[0].name, "NWN") == 0 && strcmp(out.services[0].host, "nwnrevival.com") == 0);
    CHECK_EQ_I(out.n_services, 2);
    /* an empty number drops the row, the rest closes up */
    CHECK_EQ_I(apply(&c, "snum0=&shost0=&sport0=&smode0=raw&sname0=", &out), SETTINGS_OK);
    CHECK_EQ_I(out.n_services, 1); CHECK(strcmp(out.services[0].number, "5559876") == 0);
    /* not acceptable */
    CHECK_EQ_I(apply(&c, "snum5=&shost5=h&sport5=1&sname5=x", &out), SETTINGS_INVALID);
    CHECK(has(err, "[services] service 6: a number is needed"));
    CHECK_EQ_I(apply(&c, "snum5=777&shost5=h&sport5=99999&sname5=x", &out), SETTINGS_INVALID);
    CHECK(has(err, "service 6: the port is not a number from 1 to 65535"));
    CHECK_EQ_I(apply(&c, "snum5=777&shost5=a+b&sport5=1&sname5=x", &out), SETTINGS_INVALID);
    CHECK_EQ_I(apply(&c, "snum5=777&shost5=h&sport5=1&smode5=ssh&sname5=x", &out), SETTINGS_INVALID);
    CHECK_EQ_I(apply(&c, "snum5=8002273425&shost5=h&sport5=1&sname5=x", &out), SETTINGS_INVALID);
    CHECK(has(err, "the box answers itself"));
    CHECK_EQ_I(apply(&c, "snum5=5559876&shost5=h&sport5=1&sname5=x", &out), SETTINGS_INVALID);
    CHECK(has(err, "listed twice"));
    CHECK_EQ_I(apply(&c, "snum5=5551234&shost5=h&sport5=1&sname5=x", &out), SETTINGS_INVALID);
    CHECK(has(err, "the number of port 1"));
    CHECK_EQ_I(apply(&c, "snum5=777&shost5=h&sport5=1&sname5=caf%C3%A9", &out), SETTINGS_INVALID);
    /* not a form of ours */
    CHECK_EQ_I(apply(&c, "snum16=777", &out), SETTINGS_BAD);
    CHECK_EQ_I(apply(&c, "snum05=777", &out), SETTINGS_BAD);
    CHECK_EQ_I(apply(&c, "snum=777", &out), SETTINGS_BAD);
    CHECK_EQ_I(apply(&c, "snum1=777&snum1=778", &out), SETTINGS_BAD);

    /* the largest page: every row at full length, the list as long as it goes */
    cfg_default(&c); c.n_services = CFG_MAX_SERVICES;
    for (i = 0; i < CFG_MAX_SERVICES; i++) {
        cfg_service_t *e = &c.services[i];
        snprintf(e->number, sizeof e->number, "1%02d555555555555", i);
        memset(e->host, 'h', CFG_SVC_HOST_MAX); memset(e->name, '"', CFG_SVC_NAME_MAX);
        e->port = 65535; e->mode = CFG_SVC_TELNET;
    }
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);
    settings_set_svc_rows(fill_rows); rows_fill = 100000; rows_called = 0;
    n = settings_render_page(&c, SETTINGS_SERVICES, 0, "a long error message, as long as they come, and some more of it here", html, sizeof html);
    CHECK(n > 0 && rows_called == 1);
    CHECK(has(html, " more.</p>") && has(html, "</html>\n"));
    printf("  worst-case services page %d of %d bytes\n", n, SETTINGS_HTML_MAX);
    rows_fill = 0;
    CHECK(settings_render_page(&c, SETTINGS_SERVICES, 0, NULL, html, sizeof html) > 0);
    CHECK(has(html, "None yet.") && !has(html, " more.</p>"));
    /* and the largest post of it fits too: every octet of every name as %HH */
    submit(html, body, sizeof body);
    CHECK_EQ_I(apply(&c, body, &out), SETTINGS_OK);
    CHECK(memcmp(&c, &out, sizeof c) == 0);
    {
        size_t extra = (size_t)CFG_MAX_SERVICES * CFG_SVC_NAME_MAX * 2;   /* '"' is %22 already; anything is at most 3 */
        n = (int)strlen(body);
        printf("  largest services post %d of %d bytes\n", n, SETTINGS_BODY_MAX);
        CHECK((size_t)n + extra <= SETTINGS_BODY_MAX || (size_t)n <= SETTINGS_BODY_MAX);
        CHECK((size_t)n <= SETTINGS_BODY_MAX);
    }
    settings_set_svc_rows(NULL);
}

int main(void)
{
    test_page();
    test_escaping();
    test_bounds();
    test_ethernet();
    test_pages();
    test_port2();
    test_one_port();
    test_consoles_page();
    test_services_page();
    test_apply();
    test_users();
    test_invalid();
    test_malformed();
    test_round_trip();
    test_fuzz();
    T_REPORT("test_settings");
}
