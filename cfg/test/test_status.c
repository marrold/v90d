/* Tests for the status struct and its HTML and JSON renderers: content,
 * escaping of hostile strings, bounded output at every buffer size. */
#include <string.h>
#include "test.h"
#include "page.h"
#include "status.h"

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static char html[STATUS_HTML_MAX], json[STATUS_JSON_MAX];

/* ---- a strict little JSON syntax checker (RFC 8259) ---------------------- */

static const char *jp;

static void j_ws(void) { while (*jp == ' ' || *jp == '\n' || *jp == '\t' || *jp == '\r') jp++; }
static int j_value(int depth);

static int j_string(void)
{
    if (*jp++ != '"') return 0;
    for (;;) {
        unsigned ch = (unsigned char)*jp++;
        if (ch == '"') return 1;
        if (ch < 0x20 || ch > 0x7e) return 0;   /* we also demand pure ASCII */
        if (ch == '\\') {
            ch = (unsigned char)*jp++;
            if (ch == 'u') {
                int i;
                for (i = 0; i < 4; i++, jp++)
                    if (!((*jp >= '0' && *jp <= '9') || (*jp >= 'a' && *jp <= 'f') ||
                          (*jp >= 'A' && *jp <= 'F'))) return 0;
            } else if (!strchr("\"\\/bfnrt", (int)ch) || ch == 0) {
                return 0;
            }
        }
    }
}

static int j_number(void)
{
    const char *b = jp;
    if (*jp == '-') jp++;
    if (*jp == '0') jp++;
    else if (*jp >= '1' && *jp <= '9') { while (*jp >= '0' && *jp <= '9') jp++; }
    else return 0;
    if (*jp == '.') {
        jp++;
        if (!(*jp >= '0' && *jp <= '9')) return 0;
        while (*jp >= '0' && *jp <= '9') jp++;
    }
    return jp > b;
}

static int j_value(int depth)
{
    if (depth > 8) return 0;
    j_ws();
    if (*jp == '{') {
        jp++; j_ws();
        if (*jp == '}') { jp++; return 1; }
        for (;;) {
            j_ws();
            if (!j_string()) return 0;
            j_ws();
            if (*jp++ != ':') return 0;
            if (!j_value(depth + 1)) return 0;
            j_ws();
            if (*jp == ',') { jp++; continue; }
            return *jp++ == '}';
        }
    }
    if (*jp == '[') {
        jp++; j_ws();
        if (*jp == ']') { jp++; return 1; }
        for (;;) {
            if (!j_value(depth + 1)) return 0;
            j_ws();
            if (*jp == ',') { jp++; continue; }
            return *jp++ == ']';
        }
    }
    if (*jp == '"') return j_string();
    if (!strncmp(jp, "true", 4)) { jp += 4; return 1; }
    if (!strncmp(jp, "false", 5)) { jp += 5; return 1; }
    if (!strncmp(jp, "null", 4)) { jp += 4; return 1; }
    return j_number();
}

static int json_ok(const char *text)
{
    jp = text;
    if (!j_value(0)) return 0;
    j_ws();
    return *jp == '\0';
}

static int ascii_only(const char *z)
{
    for (; *z; z++)
        if (!(*z == '\n' || (*z >= 0x20 && *z <= 0x7e))) return 0;
    return 1;
}

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

/* ---- tests ---------------------------------------------------------------- */

static void fill(status_t *s)
{
    status_init(s);
    s->uptime_s = 2 * 86400 + 3 * 3600 + 4 * 60 + 5;
    s->line = ST_LINE_CONNECTED;
    s->modulation = ST_MOD_V90;
    s->down_bps = 52000;
    s->up_bps = 31200;
    s->rtd_ms = 12;
    s->rx_level_x10 = -145;
    s->snr_x10 = 382;
    s->v42 = ST_V42_LAPM;
    s->compression = 1;
    s->ppp = ST_PPP_RUNNING;
    strcpy(s->peer_name, "alice");
    s->peer_ip = IP(10, 64, 64, 2);
    s->local_ip = IP(10, 64, 64, 1);
    s->rx_octets = 123456789012ull;
    s->tx_octets = 18446744073709551615ull;
    s->rx_packets = 1001; s->tx_packets = 2002;
    s->rx_errors = 3; s->tx_errors = 4;
}

static void test_checker(void)
{
    /* the checker itself must reject what it should */
    CHECK(json_ok("{\"a\":[1,-2.5,true,null,\"x\\u00e9\\\"\"],\"b\":{}}"));
    CHECK(!json_ok("{\"a\":1,}"));
    CHECK(!json_ok("{\"a\":01}"));
    CHECK(!json_ok("{\"a\":\"\x01\"}"));
    CHECK(!json_ok("{\"a\":\"\\x41\"}"));
    CHECK(!json_ok("{\"a\":1} x"));
    CHECK(!json_ok("{\"a\":\"unterminated}"));
}

static int calls_html(const status_t *s);

static void test_idle(void)
{
    status_t s;
    int n;
    memset(&s, 0xff, sizeof s);
    status_init(&s);
    CHECK_EQ_I(s.line, ST_LINE_IDLE);
    CHECK_EQ_I(s.n_calls, 0);
    CHECK(status_call(&s, 0) == NULL);

    n = status_render_json(&s, json, sizeof json);
    CHECK(n > 0);
    CHECK_EQ_I((int)strlen(json), n);
    CHECK(json_ok(json));
    CHECK(has(json, "\"line\":\"idle\""));
    CHECK(has(json, "\"modulation\":\"none\""));
    CHECK(has(json, "\"peer_name\":\"\""));
    CHECK(has(json, "\"peer_ip\":null"));
    CHECK(has(json, "\"calls\":[]"));

    n = status_render_html(&s, html, sizeof html);
    CHECK(n > 0);
    CHECK_EQ_I((int)strlen(html), n);
    CHECK(strncmp(html, "<!DOCTYPE html>", 15) == 0);
    CHECK(has(html, "</html>\n"));
    CHECK(has(html, "<td>idle</td>"));
    /* self-contained: the box's one script, nothing inline, nothing fetched from anywhere else */
    CHECK(has(html, "<script src=\"/ui.js?v=" V90D_VERSION "\" defer></script>") && !has(html, "<script>"));
    CHECK(strstr(strstr(html, "src=") + 4, "src=") == NULL &&
          !has(html, "url(") && !has(html, "http:") && !has(html, "https:") && !has(html, "@import"));
    /* every link leads to a page of the same server; the first to the settings */
    {
        const char *a = strstr(html, "href=");
        int n_links = 0;
        CHECK(a != NULL && strncmp(a, "href=\"/\">Status</a>", 19) == 0);
        for (; a; a = strstr(a + 5, "href=")) { CHECK(a[6] == '/' && a[7] != '/'); n_links++; }
        CHECK_EQ_I(n_links, 14);                /* four sections, their pages but this one (Consoles hidden), Log out */
    }
    CHECK(has(html, "<meta http-equiv=\"refresh\" content=\"5\">"));
    /* the look: light and dark, a header that marks this page, the state
     * as a badge with a line under it, the call list free to scroll */
    CHECK(has(html, "prefers-color-scheme:dark"));
    CHECK(has(html, "<nav><span class=\"mn\"><a class=\"on\" href=\"/\">Status</a><span class=\"dd\"><b>Dashboard</b><a href=\"/calls\">Calls</a></span></span>"));
    CHECK(has(html, "nav .mn:hover .dd,nav .mn:focus-within .dd{display:block}"));      /* the drop-down, by CSS alone */
    CHECK(has(html, "<title>Nestaliga status</title>"));
    CHECK(has(html, "<h1><i></i>Nestaliga<small>dial-up server</small></h1>"));
    CHECK(has(html, "<span class=\"b s0\">idle</span>"));
    CHECK(has(html, "<p class=\"sum\">No call in progress</p>"));
    CHECK(has(html, "<small>Downstream</small><b>0</b> bit/s<div class=\"m\"><i style=\"width:0%\"></i></div>"));
    CHECK(has(html, "<div class=\"k\"><small>Calls</small><b>0</b></div>"));
    CHECK(!has(html, "Last calls"));                       /* the calls have their own page */
    CHECK(!has(html, "\xe2\x80\x94"));
}

static void test_values(void)
{
    status_t s;
    fill(&s);
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"uptime_s\":183845"));
    CHECK(has(json, "\"service\":\"internet\""));
    s.service = 2;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(has(json, "\"service\":\"dreamcast\""));
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<th>Service</th><td>Dreamcast</td>"));
    s.service = 1;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"service\":\"archive\""));
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<th>Service</th><td>Archive (Protoweb)</td>"));
    s.service = 0;
    /* V.92 allowed on this port: the setting, next to archive */
    CHECK(has(html, "<tr><th>V.92</th><td>off</td></tr>"));
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(has(json, "\"service\":\"internet\",\"v92\":false,"));
    s.v92 = 1;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"service\":\"internet\",\"v92\":true,"));
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<tr><th>V.92</th><td>on</td></tr>"));
    CHECK(has(html, "<th>Service</th><td>Internet</td>"));
    s.v92 = 0;
    /* PPP compression: the setting, and what this call makes of it */
    CHECK(has(json, "\"v92\":true,\"ppp_compression\":false,"));
    CHECK(has(html, "<tr><th>PPP compression</th><td>off</td></tr>"));
    CHECK(has(json, "\"local_ip\":\"10.64.64.1\",\"comp_tx\":\"none\",\"comp_rx\":\"none\","
                    "\"comp_tx_in\":0,\"comp_tx_out\":0,\"comp_rx_in\":0,\"comp_rx_out\":0}"));
    CHECK(has(html, "<tr><th>Compressed to caller</th><td>no</td></tr>"));
    CHECK(has(html, "<tr><th>Compressed from caller</th><td>no</td></tr>"));
    s.ppp_compression = 1;
    s.ppp_comp_tx = ST_COMP_DEFLATE; s.ppp_comp_rx = ST_COMP_MPPC;
    s.comp_tx_in = 322830; s.comp_tx_out = 93219; s.comp_rx_in = 1000; s.comp_rx_out = 2449;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"ppp_compression\":true,"));
    CHECK(has(json, "\"comp_tx\":\"deflate\",\"comp_rx\":\"mppc\","
                    "\"comp_tx_in\":322830,\"comp_tx_out\":93219,\"comp_rx_in\":1000,\"comp_rx_out\":2449}"));
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<tr><th>PPP compression</th><td>on</td></tr>"));
    CHECK(has(html, "<tr><th>Compressed to caller</th><td>Deflate: 322830 octets sent as 93219 (3.4 to 1)</td></tr>"));
    CHECK(has(html, "<tr><th>Compressed from caller</th><td>MPPC: 2449 octets received as 1000 (2.4 to 1)</td></tr>"));
    /* agreed and nothing through it yet: no ratio to show */
    s.comp_tx_in = s.comp_tx_out = 0; s.ppp_comp_tx = 77;
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<tr><th>Compressed to caller</th><td>no</td></tr>"));
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"comp_tx\":\"unknown\""));        /* as every enum out of range */
    s.ppp_comp_tx = ST_COMP_MPPC;
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<tr><th>Compressed to caller</th><td>MPPC</td></tr>"));
    /* MPPE encryption: the setting, and the per-direction report (no ratio) */
    s.ppp_encryption = 1; s.ppp_comp_tx = s.ppp_comp_rx = ST_COMP_MPPE;
    s.comp_tx_in = s.comp_tx_out = 0;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"ppp_encryption\":true,"));
    CHECK(has(json, "\"comp_tx\":\"mppe\",\"comp_rx\":\"mppe\","));
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<tr><th>PPP encryption</th><td>required (MPPE)</td></tr>"));
    CHECK(has(html, "<tr><th>Compressed to caller</th><td>MPPE (128-bit, encrypted)</td></tr>"));
    s.ppp_encryption = 0;
    s.ppp_compression = 0; s.ppp_comp_tx = s.ppp_comp_rx = 0; s.comp_rx_in = s.comp_rx_out = 0;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(has(json, "\"line\":\"connected\""));
    CHECK(has(json, "\"modulation\":\"v90\""));
    CHECK(has(json, "\"down_bps\":52000"));
    CHECK(has(json, "\"up_bps\":31200"));
    CHECK(has(json, "\"rtd_ms\":12"));
    CHECK(has(json, "\"rx_level_dbm0\":-14.5"));
    CHECK(has(json, "\"snr_db\":38.2"));
    CHECK(has(json, "\"v42\":\"lapm\""));
    CHECK(has(json, "\"compression\":true"));
    CHECK(has(json, "\"phase\":\"running\""));
    CHECK(has(json, "\"peer_name\":\"alice\""));
    CHECK(has(json, "\"peer_ip\":\"10.64.64.2\""));
    CHECK(has(json, "\"local_ip\":\"10.64.64.1\""));
    CHECK(has(json, "\"rx_octets\":123456789012"));
    CHECK(has(json, "\"tx_octets\":18446744073709551615"));
    CHECK(has(json, "\"rx_packets\":1001"));
    CHECK(has(json, "\"tx_packets\":2002"));
    CHECK(has(json, "\"rx_errors\":3"));
    CHECK(has(json, "\"tx_errors\":4"));

    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<td>connected</td>"));
    CHECK(has(html, "<span class=\"b s3\">connected</span>"));
    /* the tiles: the figures one looks for first, each rate and the SNR
     * with a bar (of 56000 and 33600 bit/s, of 45 dB) */
    CHECK(has(html, "<div class=\"k\"><small>Downstream</small><b>52000</b> bit/s"
                    "<div class=\"m\"><i style=\"width:92%\"></i></div></div>"));
    CHECK(has(html, "<div class=\"k\"><small>Upstream</small><b>31200</b> bit/s"
                    "<div class=\"m\"><i style=\"width:92%\"></i></div></div>"));
    CHECK(has(html, "<div class=\"k\"><small>Signal to noise</small><b>38.2</b> dB"
                    "<div class=\"m\"><i style=\"width:84%\"></i></div></div>"));
    CHECK(has(html, "<div class=\"k\"><small>Uptime</small><b>2d 03:04:05</b></div>"));
    CHECK(has(html, "<p class=\"sum\">V.90 &middot; 52000 / 31200 bit/s &middot; alice</p>"));
    CHECK(has(html, "<td>V.90</td>"));
    CHECK(has(html, "<td>52000 bit/s</td>"));
    CHECK(has(html, "<td>31200 bit/s</td>"));
    CHECK(has(html, "<td>12 ms</td>"));
    CHECK(has(html, "<td>-14.5 dBm0</td>"));
    CHECK(has(html, "<td>38.2 dB</td>"));
    CHECK(has(html, "<td>LAPM</td>"));
    CHECK(has(html, "<td>alice</td>"));
    CHECK(has(html, "<td>10.64.64.2</td>"));
    CHECK(has(html, "<td>10.64.64.1</td>"));
    CHECK(has(html, "<td>123456789012</td>"));
    CHECK(has(html, "<td>18446744073709551615</td>"));
    CHECK(has(html, "<td>2d 03:04:05</td>"));

    /* fixed-point corners */
    s.rx_level_x10 = -5; s.snr_x10 = 0;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"rx_level_dbm0\":-0.5"));
    CHECK(has(json, "\"snr_db\":0.0"));
    s.rx_level_x10 = (int32_t)0x80000000u; s.snr_x10 = 0x7fffffff;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"rx_level_dbm0\":-214748364.8"));
    CHECK(has(json, "\"snr_db\":214748364.7"));
}

static void test_enums(void)
{
    static const char *const lines[] = { "idle", "ringing", "negotiating", "connected", "hanging up" };
    static const char *const mods[] = { "none", "v90", "v34", "v32bis", "v22bis", "v22", "v21", "v92", "bell103", "bell212a" };
    static const char *const modh[] = { "none", "V.90", "V.34", "V.32bis", "V.22bis", "V.22", "V.21", "V.92", "Bell 103", "Bell 212A" };
    static const char *const v42s[] = { "off", "detect", "lapm", "async" };
    static const char *const ppps[] = { "dead", "establish", "authenticate", "network", "running", "terminate" };
    status_t s;
    char want[96];
    int i;
    for (i = 0; i < 5; i++) {
        status_init(&s); s.line = (st_line_t)i; s.modulation = (st_mod_t)i;
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        snprintf(want, sizeof want, "\"line\":\"%s\"", lines[i]);
        CHECK(has(json, want));
        CHECK(status_render_html(&s, html, sizeof html) > 0);
        snprintf(want, sizeof want, "<td>%s</td>", lines[i]);
        CHECK(has(html, want));
    }
    CHECK_EQ_I(ST_MOD_V21, 6); CHECK_EQ_I(ST_MOD_V92, 7); CHECK_EQ_I(ST_MOD_BELL103, 8); CHECK_EQ_I(ST_MOD_BELL212A, 9);      /* appended: the old values stay */
    CHECK_EQ_I(ST_MOD_X2, 10); CHECK_EQ_I(ST_MOD_K56FLEX, 11); CHECK_EQ_I(ST_MOD_V23, 12);
    for (i = 0; i < 10; i++) {
        st_call_t call;
        status_init(&s); s.modulation = (st_mod_t)i;
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        snprintf(want, sizeof want, "\"modulation\":\"%s\",\"down_bps\":0,\"up_bps\":0,\"rtd_ms\"", mods[i]);
        CHECK(has(json, want));
        CHECK(status_render_html(&s, html, sizeof html) > 0);
        snprintf(want, sizeof want, "<tr><th>Modulation</th><td>%s</td></tr>", modh[i]);
        CHECK(has(html, want));
        /* and in a row of the call history */
        status_init(&s);
        memset(&call, 0, sizeof call);
        call.modulation = (st_mod_t)i; call.down_bps = 50000; call.up_bps = 40000;
        status_add_call(&s, &call);
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        CHECK(json_ok(json));
        snprintf(want, sizeof want, "\"modulation\":\"%s\",\"down_bps\":50000,\"up_bps\":40000,", mods[i]);
        CHECK(has(json, want));
        CHECK(calls_html(&s) > 0);
        snprintf(want, sizeof want, "<td>answered</td><td>%s</td><td>50000</td><td>40000</td>", modh[i]);
        CHECK(has(html, want));
    }
    /* a V.92 call as the page shows it */
    status_init(&s); s.line = ST_LINE_CONNECTED; s.modulation = ST_MOD_V92; s.down_bps = 52000; s.up_bps = 44000;
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<p class=\"sum\">V.92 &middot; 52000 / 44000 bit/s</p>"));
    /* the upstream bar is scaled to 48000 for V.92: 44000 is 91% of it */
    CHECK(has(html, "<small>Upstream</small><b>44000</b> bit/s<div class=\"m\"><i style=\"width:91%\"></i></div></div>"));
    s.modulation = (st_mod_t)13;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(has(json, "\"modulation\":\"unknown\""));
    status_init(&s); s.line = ST_LINE_CONNECTED; s.modulation = ST_MOD_V34;
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "V.34"));
    s.modulation = ST_MOD_V32BIS;
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "V.32bis"));
    for (i = 0; i < 4; i++) {
        status_init(&s); s.v42 = (st_v42_t)i;
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        snprintf(want, sizeof want, "\"v42\":\"%s\"", v42s[i]);
        CHECK(has(json, want));
    }
    for (i = 0; i < 6; i++) {
        status_init(&s); s.ppp = (st_ppp_t)i;
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        snprintf(want, sizeof want, "\"phase\":\"%s\"", ppps[i]);
        CHECK(has(json, want));
    }
    /* values outside the enums do not index anything */
    status_init(&s);
    s.line = (st_line_t)5; s.modulation = (st_mod_t)-1; s.v42 = (st_v42_t)1000000;
    s.ppp = (st_ppp_t)6; s.compression = 77;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"line\":\"unknown\""));
    CHECK(has(json, "\"modulation\":\"unknown\""));
    CHECK(has(json, "\"v42\":\"unknown\""));
    CHECK(has(json, "\"phase\":\"unknown\""));
    CHECK(has(json, "\"compression\":true"));
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<td>unknown</td>"));
}

static void test_escaping(void)
{
    status_t s;
    int i, base;

    fill(&s);
    strcpy(s.peer_name, "<script>alert('x')</script>&\"\\");
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(!has(html, "<script>"));
    CHECK(!has(html, "alert('"));
    CHECK(has(html, "<td>&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt;&amp;&quot;\\</td>"));
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"peer_name\":\"\\u003cscript\\u003ealert('x')\\u003c/script\\u003e\\u0026\\\"\\\\\""));

    /* a name that tries to close the JSON string and add a member */
    strcpy(s.peer_name, "x\",\"line\":\"pwned");
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(!has(json, ",\"line\":\"pwned"));
    CHECK(has(json, "\"peer_name\":\"x\\\",\\\"line\\\":\\\"pwned\""));

    /* control and 8-bit octets */
    strcpy(s.peer_name, "a\x01\n\x7f\x80\xff" "b");
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(has(html, "<td>a\\x01\\x0a\\x7f\\x80\\xffb</td>"));
    CHECK(ascii_only(html));
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"peer_name\":\"a\\u0001\\u000a\\u007f\\u0080\\u00ffb\""));

    /* every octet value 1..255, 63 at a time */
    for (base = 1; base < 256; base += STATUS_NAME_MAX) {
        memset(s.peer_name, 0, sizeof s.peer_name);
        for (i = 0; i < STATUS_NAME_MAX && base + i < 256; i++) s.peer_name[i] = (char)(base + i);
        CHECK(status_render_html(&s, html, sizeof html) > 0);
        CHECK(ascii_only(html));
        CHECK(!has(html, "<td><") && strstr(strstr(html, "Peer name"), "</td></tr>") != NULL);
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        CHECK(ascii_only(json));
        CHECK(json_ok(json));
    }

    /* a name without its NUL: exactly 64 octets are shown, nothing beyond */
    memset(s.peer_name, 'N', sizeof s.peer_name);
    s.peer_ip = IP(78, 78, 78, 78);
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    {
        const char *p = strstr(json, "\"peer_name\":\"");
        CHECK(p != NULL);
        if (p) { p += 13; CHECK_EQ_I(strspn(p, "N"), 64); CHECK(p[64] == '"'); }
    }
    CHECK(status_render_html(&s, html, sizeof html) > 0);
}

/* the call history page of a box with the one port s */
static int calls_html(const status_t *s)
{
    const status_t *one[2] = { s, NULL };
    return status_render_calls(one, html, sizeof html);
}

static void test_calls(void)
{
    status_t s;
    st_call_t c;
    const st_call_t *q;
    unsigned i;
    const char *a, *b;

    status_init(&s);
    for (i = 1; i <= 24; i++) {
        memset(&c, 0, sizeof c);
        c.start_time = 1700000000u + i * 1000u;
        c.duration_s = 7000u + i;
        c.down_bps = 40000u + i;
        c.up_bps = 30000u + i;
        c.modulation = ST_MOD_V90;
        c.end = ST_END_CALLER_HANGUP;
        strcpy(c.dialed, "5551234");
        c.tel_result = ST_TEL_ANSWERED;
        status_add_call(&s, &c);
        CHECK_EQ_I(s.n_calls, i < STATUS_MAX_CALLS ? i : STATUS_MAX_CALLS);
        q = status_call(&s, 0);
        CHECK(q && q->duration_s == 7000u + i);
    }
    CHECK_EQ_I(s.calls_total, 24);
    for (i = 0; i < STATUS_MAX_CALLS; i++) {
        q = status_call(&s, i);
        CHECK(q && q->duration_s == 7024u - i);
    }
    CHECK(status_call(&s, STATUS_MAX_CALLS) == NULL);
    CHECK(status_call(&s, 0xffffffffu) == NULL);

    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(has(json, "\"calls_total\":24"));
    CHECK(has(json, "\"duration_s\":7024"));
    CHECK(has(json, "\"duration_s\":7005"));
    CHECK(!has(json, "\"duration_s\":7004"));
    a = strstr(json, "\"duration_s\":7024"); b = strstr(json, "\"duration_s\":7005");
    CHECK(a && b && a < b);                         /* newest first */
    CHECK(has(json, "{\"start\":\"2023-11-15T04:53:20Z\",\"start_unix\":1700024000,\"duration_s\":7024,"
                    "\"dialed\":\"5551234\",\"tel_result\":\"answered\","
                    "\"modulation\":\"v90\",\"down_bps\":40024,\"up_bps\":30024,\"end\":\"caller_hangup\"}"));
    CHECK(calls_html(&s) > 0);
    CHECK(has(html, "<th>Start (UTC)</th><th>Duration</th><th>Dialed</th><th>Result</th>"
                    "<th>Modulation</th><th>Down bit/s</th><th>Up bit/s</th><th>Ended</th>"));
    CHECK(has(html, "<td>2023-11-15 04:53:20</td><td>1:57:04</td><td>5551234</td><td>answered</td>"
                    "<td>V.90</td><td>40024</td><td>30024</td><td>caller hung up</td>"));
    CHECK(!has(html, "<td>30004</td>"));

    /* the dashboard has no table of calls any more: they are on /calls */
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    CHECK(!has(html, "Last calls") && !has(html, "Start (UTC)") && !has(html, "<td>40024</td>"));
    CHECK(has(html, "<tr><th>Calls</th><td>24</td></tr>"));    /* the count stays */

    /* dates and end reasons */
    {
        static const struct { uint32_t t; const char *iso; } d[] = {
            { 1u, "1970-01-01T00:00:01Z" },
            { 951782400u, "2000-02-29T00:00:00Z" },
            { 951868799u, "2000-02-29T23:59:59Z" },
            { 1700000000u, "2023-11-14T22:13:20Z" },
            { 1709251199u, "2024-02-29T23:59:59Z" },
            { 1735689600u, "2025-01-01T00:00:00Z" },
            { 4107542399u, "2100-02-28T23:59:59Z" },
            { 4107542400u, "2100-03-01T00:00:00Z" },
            { 4294967295u, "2106-02-07T06:28:15Z" },
        };
        static const char *const ends[] = { "none", "caller_hangup", "carrier_lost", "train_failed",
            "auth_failed", "ppp_closed", "echo_timeout", "local", "error", "retrain_failed", "unknown" };
        char want[64];
        for (i = 0; i < sizeof d / sizeof d[0]; i++) {
            status_init(&s);
            memset(&c, 0, sizeof c);
            c.start_time = d[i].t;
            status_add_call(&s, &c);
            CHECK(status_render_json(&s, json, sizeof json) > 0);
            snprintf(want, sizeof want, "\"start\":\"%s\"", d[i].iso);
            CHECK(has(json, want));
            if (!has(json, want)) printf("  want %s\n", want);
        }
        for (i = 0; i < 11; i++) {
            status_init(&s);
            memset(&c, 0, sizeof c);
            c.end = (st_end_t)i;
            c.modulation = (st_mod_t)(i == 10 ? 99 : 0);
            status_add_call(&s, &c);
            CHECK(status_render_json(&s, json, sizeof json) > 0);
            CHECK(json_ok(json));
            snprintf(want, sizeof want, "\"end\":\"%s\"", ends[i]);
            CHECK(has(json, want));
            CHECK(has(json, "\"start\":null,\"start_unix\":0"));    /* clock not set */
            CHECK(calls_html(&s) > 0);
        }
    }

    /* how the call ended at the telephone level */
    {
        static const struct { int r; const char *j, *h; } tel[] = {
            { ST_TEL_ANSWERED, "answered", "answered" },
            { ST_TEL_BUSY, "busy", "busy" },
            { ST_TEL_WRONG_NUMBER, "wrong_number", "wrong number" },
            { ST_TEL_ABANDONED, "abandoned", "abandoned" },
            { ST_TEL_DROPPED, "dropped", "line dropped" },
            { ST_TEL_SESSION_LIMIT, "session_limit", "session limit" },
            { ST_TEL_IDLE, "idle", "idle timeout" },
            { 7, "unknown", "unknown" },
            { -1, "unknown", "unknown" },
        };
        char want[96];
        CHECK_EQ_I(ST_TEL_ANSWERED, 0);
        for (i = 0; i < sizeof tel / sizeof tel[0]; i++) {
            status_init(&s);
            memset(&c, 0, sizeof c);
            strcpy(c.dialed, "*67#0800");
            c.tel_result = tel[i].r;
            status_add_call(&s, &c);
            CHECK(status_render_json(&s, json, sizeof json) > 0);
            CHECK(json_ok(json));
            snprintf(want, sizeof want, "\"dialed\":\"*67#0800\",\"tel_result\":\"%s\",", tel[i].j);
            CHECK(has(json, want));
            CHECK(calls_html(&s) > 0);
            snprintf(want, sizeof want, "<td>*67#0800</td><td>%s</td>", tel[i].h);
            CHECK(has(html, want));
        }
        /* nothing dialed (the daemon did not see digits) */
        status_init(&s);
        memset(&c, 0, sizeof c);
        status_add_call(&s, &c);
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        CHECK(has(json, "\"dialed\":\"\","));
        CHECK(calls_html(&s) > 0);
        CHECK(has(html, "<td>0:00:00</td><td>-</td><td>answered</td>"));
        /* the digits come from the caller: escaped like the peer name, and
         * a field without its NUL is read to its end and no further */
        strcpy(s.calls[0].dialed, "<b>\"&'\x01\xff");
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        CHECK(json_ok(json));
        CHECK(has(json, "\"dialed\":\"\\u003cb\\u003e\\\"\\u0026'\\u0001\\u00ff\","));
        CHECK(calls_html(&s) > 0);
        CHECK(has(html, "<td>&lt;b&gt;&quot;&amp;&#39;\\x01\\xff</td>"));
        CHECK(ascii_only(html));
        memset(s.calls[0].dialed, '7', sizeof s.calls[0].dialed);
        s.calls[0].tel_result = ST_TEL_BUSY;
        CHECK(status_render_json(&s, json, sizeof json) > 0);
        CHECK(json_ok(json));
        CHECK(has(json, "\"dialed\":\"777777777777777777777\",\"tel_result\":\"busy\""));
        CHECK(calls_html(&s) > 0);
        CHECK(has(html, "<td>777777777777777777777</td><td>busy</td>"));
    }

    /* a corrupt ring header cannot make the renderers read outside it */
    status_init(&s);
    s.n_calls = 1000; s.call_next = 0xfffffff0u;
    CHECK(status_render_json(&s, json, sizeof json) > 0);
    CHECK(json_ok(json));
    CHECK(calls_html(&s) > 0);
    CHECK(status_call(&s, STATUS_MAX_CALLS) == NULL);
    memset(&c, 0, sizeof c);
    status_add_call(&s, &c);
    CHECK(s.n_calls <= STATUS_MAX_CALLS && s.call_next < STATUS_MAX_CALLS);
}

/* the largest text each renderer can produce */
static void worst(status_t *s)
{
    unsigned i;
    memset(s, 0xff, sizeof *s);         /* every counter and rate at its maximum */
    s->line = ST_LINE_HANGUP;
    s->modulation = ST_MOD_V22BIS;
    s->v42 = ST_V42_DETECT;
    s->ppp = ST_PPP_AUTHENTICATE;
    s->rx_level_x10 = (int32_t)0x80000000u;
    s->snr_x10 = (int32_t)0x80000000u;
    memset(s->peer_name, '"', sizeof s->peer_name);     /* 6 chars each, both formats */
    s->n_calls = STATUS_MAX_CALLS;
    s->call_next = 0;
    for (i = 0; i < STATUS_MAX_CALLS; i++) {
        s->calls[i].start_time = 4294967295u;
        s->calls[i].modulation = ST_MOD_V22BIS;
        s->calls[i].end = ST_END_CALLER_HANGUP;
        memset(s->calls[i].dialed, '"', sizeof s->calls[i].dialed);
        s->calls[i].tel_result = ST_TEL_WRONG_NUMBER;
    }
}

static void test_bounds(void)
{
    static char ref[STATUS_HTML_MAX], g[STATUS_HTML_MAX + 16];
    status_t s;
    int pass, n, cap;

    worst(&s);
    n = status_render_html(&s, html, sizeof html);
    CHECK(n > 0 && n < STATUS_HTML_MAX);
    printf("  worst-case HTML %d of %d bytes", n, STATUS_HTML_MAX);
    n = status_render_json(&s, json, sizeof json);
    CHECK(n > 0 && n < STATUS_JSON_MAX);
    CHECK(json_ok(json));
    printf(", JSON %d of %d bytes\n", n, STATUS_JSON_MAX);
    /* JSON is at its longest with octets that take a \u escape */
    memset(s.peer_name, '\x80', sizeof s.peer_name);
    for (n = 0; n < STATUS_MAX_CALLS; n++) memset(s.calls[n].dialed, '<', sizeof s.calls[n].dialed);
    CHECK(status_render_html(&s, html, sizeof html) > 0);
    n = status_render_json(&s, json, sizeof json);
    CHECK(n > 0 && n < STATUS_JSON_MAX);
    CHECK(json_ok(json));
    printf("  worst-case JSON with \\u escapes %d bytes\n", n);

    /* every capacity from 0 up: nothing past cap, NUL inside, a clean
     * prefix of the full text, -1 until the whole text fits */
    for (pass = 0; pass < 2; pass++) {
        int (*render)(const status_t *, char *, size_t) =
            pass ? status_render_json : status_render_html;
        n = render(&s, ref, sizeof ref);
        CHECK(n > 0);
        for (cap = 0; cap <= n + 1; cap++) {
            int r, ok = 1;
            memset(g, '#', sizeof g);
            r = render(&s, g, (size_t)cap);
            if (cap <= n && r != -1) ok = 0;
            if (cap == n + 1 && r != n) ok = 0;
            if (g[cap] != '#' || g[cap + 1] != '#') ok = 0;
            if (cap > 0 && memchr(g, '\0', (size_t)cap) == NULL) ok = 0;
            if (cap > 0 && strncmp(g, ref, strlen(g)) != 0) ok = 0;
            if (!ok) { CHECK(ok); printf("  pass %d cap %d r %d\n", pass, cap, r); break; }
        }
        CHECK(cap == n + 2);
    }
}

/* the dashboard of a box with two ports */
static void test_ports(void)
{
    static status_t p1, p2;
    const status_t *both[2] = { &p1, &p2 };
    st_call_t call;
    int i, n;

    status_init(&p1); status_init(&p2);
    p1.uptime_s = 100; p2.uptime_s = 100;
    p2.line = ST_LINE_CONNECTED; p2.modulation = ST_MOD_V34; p2.down_bps = 28800; p2.up_bps = 26400;
    strcpy(p2.peer_name, "bob"); p2.calls_total = 7;

    n = status_render_ports(both, 0, html, sizeof html);
    CHECK(n > 0 && (size_t)n == strlen(html));
    CHECK(has(html, "<meta http-equiv=\"refresh\" content=\"5\">"));
    /* both ports at a glance, whichever is selected */
    CHECK(has(html, "<section class=\"hero\"><h2>Port 1</h2>\n<span class=\"b s0\">idle</span>\n<p class=\"sum\">No call in progress</p></section>"));
    CHECK(has(html, "<section class=\"hero\"><h2>Port 2</h2>\n<span class=\"b s3\">connected</span>\n"
                    "<p class=\"sum\">V.34 &middot; 28800 / 26400 bit/s &middot; bob</p></section>"));
    CHECK(has(html, "<p class=\"sw\"><b>Port 1</b> <a href=\"/port2\">Port 2</a></p>"));
    /* the details are port 1's */
    CHECK(has(html, "<tr><th>Modulation</th><td>none</td></tr>"));
    CHECK(has(html, "<tr><th>Calls</th><td>0</td></tr>"));

    n = status_render_ports(both, 1, html, sizeof html);
    CHECK(n > 0);
    CHECK(has(html, "<section class=\"hero\"><h2>Port 1</h2>"));
    CHECK(has(html, "<p class=\"sw\"><a href=\"/\">Port 1</a> <b>Port 2</b></p>"));
    CHECK(has(html, "<tr><th>Modulation</th><td>V.34</td></tr>"));
    CHECK(has(html, "<tr><th>Calls</th><td>7</td></tr>"));
    CHECK(has(html, "<tr><th>Peer name</th><td>bob</td></tr>"));

    /* the largest page still fits */
    for (i = 0; i < 2; i++) {
        status_t *s = i ? &p2 : &p1;
        int k;
        memset(s, 0xff, sizeof *s);
        s->line = ST_LINE_CONNECTED; s->modulation = ST_MOD_V32BIS; s->v42 = ST_V42_DETECT; s->ppp = ST_PPP_AUTHENTICATE;
        s->n_calls = STATUS_MAX_CALLS; s->call_next = 0;
        for (k = 0; k < STATUS_MAX_CALLS; k++) {
            memset(&call, 0xff, sizeof call);
            call.modulation = ST_MOD_V32BIS; call.end = ST_END_CARRIER_LOST; call.tel_result = ST_TEL_WRONG_NUMBER;
            s->calls[k] = call;
        }
    }
    n = status_render_ports(both, 1, html, sizeof html);
    CHECK(n > 0 && n < STATUS_HTML_MAX);
    printf("  largest two-port dashboard: %d octets of %d\n", n, STATUS_HTML_MAX);
    CHECK_EQ_I(status_render_ports(both, 1, html, 4000), -1);
}

static unsigned count_of(const char *hay, const char *needle)
{
    unsigned n = 0; size_t l = strlen(needle);
    for (hay = strstr(hay, needle); hay; hay = strstr(hay + l, needle)) n++;
    return n;
}

/* the page of the last calls of both ports together */
static void test_history(void)
{
    static status_t p1, p2;
    const status_t *both[2] = { &p1, &p2 }, *one[2] = { &p1, NULL };
    st_call_t c;
    const char *a, *b, *d, *e, *f;
    unsigned i;
    int n;

    status_init(&p1); status_init(&p2);
    n = status_render_calls(both, html, sizeof html);
    CHECK(n > 0 && (size_t)n == strlen(html));
    CHECK(has(html, "<h2>Call history</h2>"));
    CHECK(has(html, "<tr><th>Port</th><th>Start (UTC)</th><th>Duration</th><th>Dialed</th><th>Result</th>"
                    "<th>Modulation</th><th>Down bit/s</th><th>Up bit/s</th><th>Ended</th></tr>"));
    CHECK(has(html, "<tr><td colspan=\"9\">none yet</td></tr>"));
    CHECK(has(html, "<b>Calls</b>"));                      /* its tab is the active one */

    /* port 1: 15 calls at even seconds, port 2: 12 at the odd ones between */
    for (i = 1; i <= 15; i++) {
        memset(&c, 0, sizeof c);
        c.start_time = 1700002000u + 2 * i; c.duration_s = 60; c.down_bps = 40000u + i; c.up_bps = 30000;
        c.modulation = ST_MOD_V90; c.end = ST_END_CALLER_HANGUP; c.tel_result = ST_TEL_ANSWERED;
        strcpy(c.dialed, "6387825442");
        status_add_call(&p1, &c);
        if (i > 12) continue;
        c.start_time++; c.down_bps = 50000u + i; c.modulation = ST_MOD_V34;
        status_add_call(&p2, &c);
    }
    n = status_render_calls(both, html, sizeof html);
    CHECK(n > 0 && (size_t)n == strlen(html));
    CHECK(!has(html, "none yet"));
    /* the newest STATUS_HISTORY of the 27, whichever port they came in on */
    CHECK_EQ_I(STATUS_HISTORY, 20);
    CHECK_EQ_I(count_of(html, "<tr><td>"), 20);
    CHECK_EQ_I(count_of(html, "<tr><td>1</td><td>"), 11);
    CHECK_EQ_I(count_of(html, "<tr><td>2</td><td>"), 9);
    /* newest first across the two ports */
    a = strstr(html, "<td>40015</td>"); b = strstr(html, "<td>40014</td>"); d = strstr(html, "<td>40013</td>");
    e = strstr(html, "<td>50012</td>"); f = strstr(html, "<td>40012</td>");
    CHECK(a && b && d && e && f && a < b && b < d && d < e && e < f);
    /* the seven oldest are gone: port 1's first four, port 2's first three */
    CHECK(!has(html, "<td>40004</td>") && has(html, "<td>40005</td>"));
    CHECK(!has(html, "<td>50003</td>") && has(html, "<td>50004</td>"));
    /* a row says its port first, then what the dashboard's table says */
    CHECK(has(html, "<tr><td>2</td><td>2023-11-14 22:47:05</td>"));
    CHECK(has(html, "<td>6387825442</td><td>answered</td><td>V.34</td><td>50012</td><td>30000</td>"));

    /* a box with one port */
    n = status_render_calls(one, html, sizeof html);
    CHECK(n > 0);
    CHECK_EQ_I(count_of(html, "<tr><td>1</td><td>"), 15);
    CHECK_EQ_I(count_of(html, "<tr><td>2</td><td>"), 0);

    /* a port keeps STATUS_HISTORY calls itself, so one busy port fills the page */
    CHECK(STATUS_MAX_CALLS >= STATUS_HISTORY);
    for (i = 16; i <= 32; i++) { c.start_time = 1700002000u + 2 * i; c.down_bps = 40000u + i; status_add_call(&p1, &c); }
    n = status_render_calls(both, html, sizeof html);
    CHECK_EQ_I(count_of(html, "<tr><td>1</td><td>"), 20);
    CHECK_EQ_I(count_of(html, "<tr><td>2</td><td>"), 0);

    /* the largest page fits, a buffer too small is refused */
    for (i = 0; i < 2; i++) {
        status_t *s = i ? &p2 : &p1;
        int k;
        memset(s, 0xff, sizeof *s);
        s->n_calls = STATUS_MAX_CALLS; s->call_next = 0;
        for (k = 0; k < STATUS_MAX_CALLS; k++) {
            memset(&c, 0xff, sizeof c);
            c.modulation = ST_MOD_V32BIS; c.end = ST_END_CARRIER_LOST; c.tel_result = ST_TEL_WRONG_NUMBER;
            s->calls[k] = c;
        }
    }
    n = status_render_calls(both, html, sizeof html);
    CHECK(n > 0 && n < STATUS_HTML_MAX);
    printf("  largest call history: %d octets of %d\n", n, STATUS_HTML_MAX);
    CHECK_EQ_I(status_render_calls(both, html, 2000), -1);
}

int main(void)
{
    test_ports();
    test_history();
    test_checker();
    test_idle();
    test_values();
    test_enums();
    test_escaping();
    test_calls();
    test_bounds();
    T_REPORT("test_status");
}
