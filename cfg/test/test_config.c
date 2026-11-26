/* Tests for the configuration loader: defaults, the full grammar, every
 * validation rule with its file:line message, canonical write-back and
 * redaction, and bounded output. */
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "test.h"
#include "config.h"

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static char err[CFG_ERR_MAX];

#define CHECK_EQ_S(got, want) do { if (strcmp((got), (want)) != 0) printf("    got:  %s\n    want: %s\n", (got), (want)); CHECK(strcmp((got), (want)) == 0); } while (0)

static int parse(cfg_t *c, const char *text)
{
    err[0] = '\0';
    return cfg_parse(c, "t.conf", text, strlen(text), err, sizeof err);
}

/* the default route of static mode */
static void test_gateway(void)
{
    static char text[CFG_TEXT_MAX];
    cfg_t c, d;
    char err[CFG_ERR_MAX];
    const char *src = "[network]\nlan = static\naddress = 192.168.7.9\nnetmask = 255.255.255.0\ngateway = 192.168.7.1\n";

    CHECK_EQ_I(cfg_parse(&c, "t", src, strlen(src), err, sizeof err), CFG_OK);
    CHECK(c.lan_gateway == IP(192, 168, 7, 1));
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "\ngateway = 192.168.7.1\n") != NULL);
    CHECK_EQ_I(cfg_parse(&d, "t", text, strlen(text), err, sizeof err), CFG_OK);
    CHECK(d.lan_gateway == IP(192, 168, 7, 1));
    /* none, and with DHCP a leftover one is not looked at */
    src = "[network]\ngateway = none\n";
    CHECK_EQ_I(cfg_parse(&c, "t", src, strlen(src), err, sizeof err), CFG_OK);
    CHECK(c.lan_gateway == 0);
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "\ngateway = none\n") != NULL);
    src = "[network]\nlan = dhcp\ngateway = 10.9.9.9\n";
    CHECK_EQ_I(cfg_parse(&c, "t", src, strlen(src), err, sizeof err), CFG_OK);
    /* the parser of one address, for the settings form */
    {
        uint32_t a = 0;
        CHECK_EQ_I(cfg_parse_ip("192.168.1.50", &a), 0);
        CHECK(a == IP(192, 168, 1, 50));
        CHECK(cfg_parse_ip("192.168.1", &a) != 0 && cfg_parse_ip("", &a) != 0 && cfg_parse_ip("1.2.3.4x", &a) != 0);
    }
}

/* V.34 and V.32bis in the list */
static void test_more_mods(void)
{
    static char text[CFG_TEXT_MAX];
    cfg_t c;
    CHECK_EQ_I(CFG_MOD_ALL, CFG_MOD_V90 | CFG_MOD_V34 | CFG_MOD_V32BIS | CFG_MOD_V22BIS | CFG_MOD_V22 | CFG_MOD_V21 | CFG_MOD_V23);
    CHECK_EQ_I(CFG_MOD_DEFAULT, CFG_MOD_ALL & ~CFG_MOD_V23);
    CHECK_EQ_I(parse(&c, "[modem]\nmodulations = v32bis, v34\n"), CFG_OK);
    CHECK_EQ_I(c.modulations, CFG_MOD_V34 | CFG_MOD_V32BIS);
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "\nmodulations = v34, v32bis\n") != NULL);
    CHECK_EQ_I(parse(&c, "[modem]\nmodulations = v21\n"), CFG_OK);
    CHECK_EQ_I(c.modulations, CFG_MOD_V21);
}

/* port 2: its own [modem2], [line2] and address */
static void test_port2(void)
{
    static char text[CFG_TEXT_MAX];
    cfg_t c, d, v;
    const char *src =
        "[network]\nppp_peer2 = 10.64.64.9\n"
        "[modem]\nrings = 2\n"
        "[modem2]\nrings = 3\nmodulations = v22bis\nlaw = alaw\nmax_downstream = 2400\nmax_upstream = 1200\n"
        "tx_level = -15\nv42bis = off\nv92 = off\n"
        "[line]\nnumber = 5551111\n"
        "[line2]\nnumber = 5559999\ntones = eu\nbusy_chance = 10\ndrop_mean = 5\nsession_limit = 60\nidle_timeout = 7\n"
        "[ppp]\nuser = bob secret\nencryption = off\nencryption2 = on\n";   /* encryption is per port */

    cfg_default(&c);
    CHECK(c.port2.ppp_peer == IP(10, 64, 64, 3));
    CHECK_EQ_I(c.port2.modulations, CFG_MOD_DEFAULT);
    CHECK_EQ_I(c.port2.max_down, 56000); CHECK_EQ_I(c.port2.max_up, 33600);
    CHECK_EQ_I(c.port2.tx_level, -12); CHECK_EQ_I(c.port2.v42, 1); CHECK_EQ_I(c.port2.v42bis, 1);
    CHECK_EQ_I(c.port2.v92, 1);
    CHECK_EQ_I(c.port2.rings, 1); CHECK_EQ_I(c.port2.law, CFG_LAW_ULAW);
    CHECK_EQ_I(c.port2.line_number[0], 0); CHECK_EQ_I(c.port2.line_tones, CFG_TONES_US);
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);

    CHECK_EQ_I(parse(&c, src), CFG_OK);
    CHECK_EQ_S(err, "");
    /* port 1 keeps its own */
    CHECK_EQ_I(c.rings, 2); CHECK_EQ_S(c.line_number, "5551111"); CHECK_EQ_I(c.modulations, CFG_MOD_DEFAULT);
    CHECK(c.ppp_peer == IP(10, 64, 64, 2)); CHECK_EQ_I(c.law, CFG_LAW_ULAW);
    CHECK(c.port2.ppp_peer == IP(10, 64, 64, 9));
    CHECK_EQ_I(c.port2.rings, 3); CHECK_EQ_I(c.port2.modulations, CFG_MOD_V22BIS);
    CHECK_EQ_I(c.port2.law, CFG_LAW_ALAW); CHECK_EQ_I(c.port2.max_down, 2400); CHECK_EQ_I(c.port2.max_up, 1200);
    CHECK_EQ_I(c.port2.tx_level, -15); CHECK_EQ_I(c.port2.v42, 1); CHECK_EQ_I(c.port2.v42bis, 0);
    CHECK_EQ_I(c.port2.v92, 0); CHECK_EQ_I(c.v92, 1);       /* each port has its own */
    CHECK_EQ_S(c.port2.line_number, "5559999"); CHECK_EQ_I(c.port2.line_tones, CFG_TONES_EU);
    CHECK_EQ_I(c.port2.busy_chance, 10); CHECK_EQ_I(c.port2.drop_mean_min, 5);
    CHECK_EQ_I(c.port2.session_limit_min, 60); CHECK_EQ_I(c.port2.idle_timeout_min, 7);
    /* encryption is per port: off on 1, on on 2 */
    CHECK_EQ_I(c.encryption, 0); CHECK_EQ_I(c.port2.encryption, 1);

    /* what one line's server is given */
    cfg_for_port(&c, 0, &v);
    CHECK_EQ_I(v.rings, 2); CHECK_EQ_S(v.line_number, "5551111"); CHECK(v.ppp_peer == IP(10, 64, 64, 2));
    CHECK_EQ_I(v.v92, 1); CHECK_EQ_I(v.encryption, 0);
    cfg_for_port(&c, 1, &v);
    CHECK_EQ_I(v.v92, 0); CHECK_EQ_I(v.port2.v92, 1); CHECK_EQ_I(v.encryption, 1);
    CHECK_EQ_I(v.rings, 3); CHECK_EQ_S(v.line_number, "5559999"); CHECK(v.ppp_peer == IP(10, 64, 64, 9));
    CHECK_EQ_I(v.modulations, CFG_MOD_V22BIS); CHECK_EQ_I(v.law, CFG_LAW_ALAW);
    CHECK_EQ_I(v.max_down, 2400); CHECK_EQ_I(v.max_up, 1200); CHECK_EQ_I(v.tx_level, -15);
    CHECK_EQ_I(v.v42bis, 0); CHECK_EQ_I(v.line_tones, CFG_TONES_EU); CHECK_EQ_I(v.busy_chance, 10);
    CHECK_EQ_I(v.drop_mean_min, 5); CHECK_EQ_I(v.session_limit_min, 60); CHECK_EQ_I(v.idle_timeout_min, 7);
    CHECK(v.ppp_local == c.ppp_local && v.auth == c.auth && v.mru == c.mru && v.dns[0] == c.dns[0]);
    CHECK_EQ_I(cfg_validate(&v, err, sizeof err), CFG_OK);

    /* written and read back */
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "\nppp_peer = 10.64.64.2\nppp_peer2 = 10.64.64.9\n") != NULL);
    CHECK(strstr(text, "\n[modem2]\nmodulations = v22bis\nmax_downstream = 2400\nmax_upstream = 1200\n"
                       "tx_level = -15\nv42 = on\nv42bis = off\nv92 = off\nx2 = off\nk56 = off\nrings = 3\nlaw = alaw\n") != NULL);
    CHECK(strstr(text, "\nv42bis = on\nv92 = on\nx2 = off\nk56 = off\nrings = 2\n") != NULL);          /* port 1 */
    CHECK(strstr(text, "\ncompression = on\nencryption = off\nencryption2 = on\n") != NULL);          /* per-port encryption */
    CHECK(strstr(text, "\n[line2]\nnumber = 5559999\ntones = eu\nbusy_chance = 10\ndrop_mean = 5\n"
                       "session_limit = 60\nidle_timeout = 7\n") != NULL);
    CHECK(strstr(text, "\n[line]\nnumber = 5551111\n") != NULL);
    CHECK_EQ_I(cfg_parse(&d, "t", text, strlen(text), err, sizeof err), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    /* a file from before there were two ports */
    CHECK_EQ_I(parse(&c, "[line]\nnumber = 42\n"), CFG_OK);
    CHECK_EQ_I(c.port2.line_number[0], 0); CHECK(c.port2.ppp_peer == IP(10, 64, 64, 3));

    /* errors name the section of the port */
    CHECK_EQ_I(parse(&c, "[line2]\nnumber = 12a\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [line2] number: '12a' is not 1..20 characters from 0-9 * # or the word any");
    CHECK_EQ_I(parse(&c, "[modem2]\nrings = 11\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [modem2] rings: 11 is out of range 1..10");
    CHECK_EQ_I(parse(&c, "[modem2]\nmodulations = v90, v9\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [modem2] modulations: 'v9' is not one of: v90, v34, v32bis, v22bis, v22, v21, v23");
    CHECK_EQ_I(parse(&c, "[modem2]\nv42 = off\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [modem2] v42bis = on needs v42 = on (set v42bis = off)");
    CHECK_EQ_I(parse(&c, "[modem2]\nv92 = yes\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [modem2] v92: 'yes' is not one of: on, off");
    CHECK_EQ_I(parse(&c, "[modem2]\nmax_upstream = 48000\n"), CFG_OK);
    CHECK_EQ_I(c.port2.max_up, 48000); CHECK_EQ_I(c.max_up, 33600);
    CHECK_EQ_I(parse(&c, "[modem2]\nmax_upstream = 48001\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [modem2] max_upstream: 48001 is out of range 300..48000");
    CHECK_EQ_I(parse(&c, "[line2]\ntones = us\n[line]\ntones = us\n[line2]\ntones = eu\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:6: duplicate key 'tones' (first set on line 2)");
    CHECK_EQ_I(parse(&c, "[line2]\nrings = 2\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: unknown key 'rings' in [line2]");
    CHECK_EQ_I(parse(&c, "[network]\nppp_peer2 = 10.64.64.2\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [network] ppp_peer2: 10.64.64.2 is the same as ppp_peer");
    CHECK_EQ_I(parse(&c, "[network]\nppp_peer2 = 10.64.64.1\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [network] ppp_peer2: 10.64.64.1 is the same as ppp_local");
    CHECK_EQ_I(parse(&c, "[network]\nlan = static\naddress = 10.64.64.3\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:3: [network] address: 10.64.64.3 is the same as ppp_peer2");
    CHECK_EQ_I(parse(&c, "[network]\nppp_peer2 = 0.0.0.0\n"), CFG_ERR);
    /* and so does the struct check */
    cfg_default(&c); c.port2.v92 = 2;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK_EQ_S(err, "[modem2] v92: invalid value 2");
    cfg_default(&c); c.port2.rings = 0;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK_EQ_S(err, "[modem2] rings: 0 is out of range 1..10");
    cfg_default(&c); memset(c.port2.line_number, '1', sizeof c.port2.line_number);
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK_EQ_S(err, "[line2] number: longer than 20 characters");
}

static void test_defaults(void)
{
    cfg_t c, d;
    memset(&c, 0xa5, sizeof c);
    cfg_default(&c);
    CHECK_EQ_I(c.lan_dhcp, 1);
    CHECK(c.lan_addr == IP(192, 168, 1, 2));
    CHECK(c.lan_netmask == IP(255, 255, 255, 0));
    CHECK(c.lan_gateway == 0);          /* none */
    CHECK(c.ppp_local == IP(10, 64, 64, 1));
    CHECK(c.ppp_peer == IP(10, 64, 64, 2));
    CHECK(c.dns[0] == IP(9, 9, 9, 9));
    CHECK(c.dns[1] == IP(1, 1, 1, 1));
    CHECK_EQ_I(c.nat, 1);
    CHECK_EQ_I(c.dns_relay, 1);
    CHECK_EQ_I(c.ipv6, 1);
    CHECK_EQ_I(c.service, CFG_SERVICE_INTERNET);   /* archive mode is asked for, never the default */
    CHECK_EQ_I(c.auth, CFG_AUTH_NONE);
    CHECK_EQ_I(c.mru, 1500);
    CHECK_EQ_I(c.echo_interval, 30);
    CHECK_EQ_I(c.compression, 1);    /* PPP compression is offered unless switched off */
    CHECK_EQ_I(c.n_users, 0);
    CHECK_EQ_I(c.modulations, CFG_MOD_DEFAULT);
    CHECK_EQ_I(c.max_down, 56000);
    CHECK_EQ_I(c.max_up, 33600);
    CHECK_EQ_I(c.tx_level, -12);
    CHECK_EQ_I(c.v42, 1);
    CHECK_EQ_I(c.v42bis, 1);
    CHECK_EQ_I(c.v92, 1);            /* V.92 is offered unless switched off */
    CHECK_EQ_I(c.x2, 0);             /* x2 is off unless switched on */
    CHECK_EQ_I(c.k56, 0);            /* K56flex is off unless switched on */
    CHECK_EQ_I(c.rings, 1);
    CHECK_EQ_I(c.law, CFG_LAW_ULAW);
    CHECK_EQ_I(c.status_listen, CFG_LISTEN_LAN);
    CHECK_EQ_I(c.status_port, 80);
    CHECK_EQ_I(c.log_level, CFG_LOG_INFO);
    CHECK(c.line_number[0] == '\0');             /* any number connects */
    CHECK_EQ_I(c.line_tones, CFG_TONES_US);
    CHECK_EQ_I(c.busy_chance, 0);
    CHECK_EQ_I(c.drop_mean_min, 0);
    CHECK_EQ_I(c.session_limit_min, 0);
    CHECK_EQ_I(c.idle_timeout_min, 0);
    CHECK(c.admin_password[0] == '\0');
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);

    /* defaults are deterministic down to the padding */
    memset(&d, 0x5a, sizeof d);
    cfg_default(&d);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* empty text and comment-only text give the defaults */
    memset(&d, 0x5a, sizeof d);
    CHECK_EQ_I(parse(&d, ""), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    CHECK_EQ_I(parse(&d, "# nothing\n\n  ; here\n[modem]\n"), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* x2 on for one line: accepted, written as on, survives a round trip */
    {
        static char t2[CFG_TEXT_MAX];
        CHECK_EQ_I(parse(&c, "[modem]\nx2 = on\n"), CFG_OK);   /* default modulations carry v34 */
        CHECK_EQ_I(c.x2, 1); CHECK_EQ_I(c.port2.x2, 0);
        CHECK(cfg_write(&c, 1, t2, sizeof t2) > 0);
        CHECK(strstr(t2, "\nv92 = on\nx2 = on\nk56 = off\nrings = 1\n") != NULL);
        CHECK_EQ_I(cfg_parse(&d, "t", t2, strlen(t2), err, sizeof err), CFG_OK);
        CHECK(memcmp(&c, &d, sizeof c) == 0);
        /* x2 may be on for both ports: the daemon serves one heavy call at a time */
        CHECK_EQ_I(parse(&c, "[modem]\nx2 = on\n[modem2]\nx2 = on\n"), CFG_OK);
        CHECK_EQ_I(c.x2, 1); CHECK_EQ_I(c.port2.x2, 1);
    }

    /* K56flex on for one line: accepted, written as on, survives a round trip */
    {
        static char t2[CFG_TEXT_MAX];
        CHECK_EQ_I(parse(&c, "[modem]\nk56 = on\n"), CFG_OK);   /* default modulations carry v34 */
        CHECK_EQ_I(c.k56, 1); CHECK_EQ_I(c.port2.k56, 0);
        CHECK(cfg_write(&c, 1, t2, sizeof t2) > 0);
        CHECK(strstr(t2, "\nv92 = on\nx2 = off\nk56 = on\nrings = 1\n") != NULL);
        CHECK_EQ_I(cfg_parse(&d, "t", t2, strlen(t2), err, sizeof err), CFG_OK);
        CHECK(memcmp(&c, &d, sizeof c) == 0);
        /* x2 and K56flex may both be on, on both ports: the daemon serves one
         * heavy call at a time */
        CHECK_EQ_I(parse(&c, "[modem]\nx2 = on\nk56 = on\n[modem2]\nx2 = on\nk56 = on\n"), CFG_OK);
        CHECK_EQ_I(c.k56, 1); CHECK_EQ_I(c.port2.k56, 1);
        CHECK_EQ_I(c.x2, 1); CHECK_EQ_I(c.port2.x2, 1);
    }
}

static const char full[] =
    "# full file\r\n"
    "[network]\r\n"
    "lan = static\n"
    "address = 172.16.4.9\n"
    "netmask = 255.255.0.0\n"
    "ppp_local = 192.168.99.1\n"
    "  ppp_peer=192.168.99.77  \n"
    "dns1 = 8.8.8.8\n"
    "dns2 = none\n"
    "nat = off\n"
    "dns_relay = off\n"
    "ipv6 = off\n"
    "archive = on\n"
    "\n"
    "[ ppp ]\n"
    "auth = chap\n"
    "mru = 576\n"
    "echo_interval = 0\n"
    "compression = off\n"
    "user = alice wonderland\n"
    "user = \"bob smith\" \"a \\\"q\\\" \\\\ \\x01\\xff end\"\n"
    "user\t=\tcarol\t\"\"\n"
    "[modem]\n"
    "modulations = v22bis ,v21\n"
    "max_downstream = 2400\n"
    "max_upstream = 300\n"
    "tx_level = -30\n"
    "v42 = off\n"
    "v42bis = off\n"
    "v92 = on\n"
    "rings = 10\n"
    "law = alaw\n"
    "[line]\n"
    "number = *70#5551234\n"
    "tones = eu\n"
    "busy_chance = 100\n"
    "drop_mean = 10080\n"
    "session_limit = 120\n"
    "idle_timeout = 15\n"
    "[status]\n"
    "listen = off\n"
    "port = 8080\n"
    "admin_password = \"open \\\"sesame\\\" \\xfe\"\n"
    "[log]\n"
    "level = debug";            /* no final newline */

static void test_full(void)
{
    cfg_t c;
    CHECK_EQ_I(parse(&c, full), CFG_OK);
    if (err[0]) printf("  unexpected: %s\n", err);
    CHECK_EQ_I(c.lan_dhcp, 0);
    CHECK(c.lan_addr == IP(172, 16, 4, 9));
    CHECK(c.lan_netmask == IP(255, 255, 0, 0));
    CHECK(c.ppp_local == IP(192, 168, 99, 1));
    CHECK(c.ppp_peer == IP(192, 168, 99, 77));
    CHECK(c.dns[0] == IP(8, 8, 8, 8));
    CHECK(c.dns[1] == 0);
    CHECK_EQ_I(c.nat, 0);
    CHECK_EQ_I(c.dns_relay, 0);
    CHECK_EQ_I(c.ipv6, 0);
    CHECK_EQ_I(c.service, CFG_SERVICE_ARCHIVE);
    CHECK_EQ_I(c.auth, CFG_AUTH_CHAP);
    CHECK_EQ_I(c.mru, 576);
    CHECK_EQ_I(c.echo_interval, 0);
    CHECK_EQ_I(c.compression, 0);
    CHECK_EQ_I(c.n_users, 3);
    CHECK(strcmp(c.users[0].name, "alice") == 0);
    CHECK(strcmp(c.users[0].secret, "wonderland") == 0);
    CHECK(strcmp(c.users[1].name, "bob smith") == 0);
    CHECK(strcmp(c.users[1].secret, "a \"q\" \\ \x01\xff end") == 0);
    CHECK(strcmp(c.users[2].name, "carol") == 0);
    CHECK(strcmp(c.users[2].secret, "") == 0);
    CHECK_EQ_I(c.modulations, CFG_MOD_V22BIS | CFG_MOD_V21);
    CHECK_EQ_I(c.max_down, 2400);
    CHECK_EQ_I(c.max_up, 300);
    CHECK_EQ_I(c.tx_level, -30);
    CHECK_EQ_I(c.v42, 0);
    CHECK_EQ_I(c.v42bis, 0);
    CHECK_EQ_I(c.v92, 1);
    CHECK_EQ_I(c.rings, 10);
    CHECK_EQ_I(c.law, CFG_LAW_ALAW);
    CHECK_EQ_I(c.status_listen, CFG_LISTEN_OFF);
    CHECK_EQ_I(c.status_port, 8080);
    CHECK_EQ_I(c.log_level, CFG_LOG_DEBUG);
    CHECK(strcmp(c.line_number, "*70#5551234") == 0);
    CHECK_EQ_I(c.line_tones, CFG_TONES_EU);
    CHECK_EQ_I(c.busy_chance, 100);
    CHECK_EQ_I(c.drop_mean_min, 10080);
    CHECK_EQ_I(c.session_limit_min, 120);
    CHECK_EQ_I(c.idle_timeout_min, 15);
    CHECK(strcmp(c.admin_password, "open \"sesame\" \xfe") == 0);
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);
}

/* Each case must fail with "t.conf:<line>: ..." (line 0: "t.conf: ...")
 * and the message must contain `what`. */
static const struct { const char *text; int line; const char *what; } bad[] = {
    /* structure */
    { "mru = 1500\n", 1, "outside any [section]" },
    { "[nope]\n", 1, "unknown section [nope]" },
    { "\n[ppp\n", 2, "missing ]" },
    { "[ppp] x\n", 1, "missing ]" },
    { "[]\n", 1, "unknown section []" },
    { "[ppp]\nmru\n", 2, "expected key = value" },
    { "[ppp]\n= 5\n", 2, "missing key" },
    { "[ppp]\nmru =\n", 2, "missing value" },
    { "[ppp]\nmtu = 1500\n", 2, "unknown key 'mtu' in [ppp]" },
    { "[modem]\nmru = 1500\n", 2, "unknown key 'mru' in [modem]" },
    { "[ppp]\nmru = 1400\n\nmru = 1500\n", 4, "duplicate key 'mru' (first set on line 2)" },
    { "[ppp]\nmru = 1400\n[modem]\n[ppp]\nmru = 1400\n", 5, "duplicate key 'mru'" },
    { "[ppp]\nMRU = 1500\n", 2, "unknown key 'MRU'" },
    /* bytes */
    { "[ppp]\nmru = 15\x01" "00\n", 2, "invalid byte 0x01 at column 9" },
    { "[ppp]\nuser = caf\xc3\xa9 x\n", 2, "invalid byte 0xc3 at column 11" },
    /* numbers */
    { "[ppp]\nmru = 1500 # default\n", 2, "mru: '1500 # default' is not a number" },
    { "[ppp]\nmru = 0x200\n", 2, "is not a number" },
    { "[ppp]\nmru = 01500\n", 2, "is not a number" },
    { "[ppp]\nmru = +1500\n", 2, "is not a number" },
    { "[ppp]\nmru = 99999999999999999999\n", 2, "is not a number" },
    { "[ppp]\nmru = 67\n", 2, "mru: 67 is out of range 68..1500" },
    { "[ppp]\nmru = 1501\n", 2, "mru: 1501 is out of range 68..1500" },
    { "[ppp]\nmru = -1\n", 2, "out of range" },
    { "[ppp]\necho_interval = 3601\n", 2, "echo_interval: 3601 is out of range 0..3600" },
    { "[modem]\nmax_downstream = 56001\n", 2, "max_downstream: 56001 is out of range 300..56000" },
    { "[modem]\nmax_downstream = 299\n", 2, "out of range 300..56000" },
    { "[modem]\nmax_upstream = 48001\n", 2, "max_upstream: 48001 is out of range 300..48000" },
    { "[modem]\nmax_upstream = 299\n", 2, "max_upstream: 299 is out of range 300..48000" },
    { "[modem]\ntx_level = -31\n", 2, "tx_level: -31 is out of range -30..-6" },
    { "[modem]\ntx_level = -5\n", 2, "tx_level: -5 is out of range -30..-6" },
    { "[modem]\ntx_level = 12\n", 2, "out of range -30..-6" },
    { "[modem]\nrings = 0\n", 2, "rings: 0 is out of range 1..10" },
    { "[modem]\nrings = 11\n", 2, "out of range 1..10" },
    { "[status]\nport = 0\n", 2, "port: 0 is out of range 1..65535" },
    { "[status]\nport = 65536\n", 2, "out of range 1..65535" },
    /* enums and switches */
    { "[network]\nlan = auto\n", 2, "lan: 'auto' is not one of: dhcp, static" },
    { "[network]\nnat = yes\n", 2, "nat: 'yes' is not one of: on, off" },
    { "[network]\narchive = protoweb\n", 2, "archive: 'protoweb' is not one of: on, off" },
    { "[ppp]\nauth = mschap\n", 2, "auth: 'mschap' is not one of: none, pap, chap" },
    { "[ppp]\ncompression = deflate\n", 2, "compression: 'deflate' is not one of: on, off" },
    { "[modem]\ncompression = on\n", 2, "compression" },
    { "[modem]\nv42 = 1\n", 2, "v42: '1' is not one of: on, off" },
    { "[modem]\nv92 = yes\n", 2, "v92: 'yes' is not one of: on, off" },
    { "[modem]\nv92 = v92\n", 2, "[modem] v92: 'v92' is not one of: on, off" },
    { "[modem]\nlaw = mulaw\n", 2, "law: 'mulaw' is not one of: ulaw, alaw" },
    { "[status]\nlisten = wan\n", 2, "listen: 'wan' is not one of: lan, off" },
    { "[log]\nlevel = trace\n", 2, "level: 'trace' is not one of: error, warn, info, debug" },
    { "[log]\nlevel = Info\n", 2, "is not one of" },
    /* modulation list */
    { "[modem]\nmodulations = v90, v32\n", 2, "modulations: 'v32' is not one of: v90, v34, v32bis, v22bis, v22, v21, v23" },
    { "[modem]\nmodulations = v34, v21, v34\n", 2, "modulations: 'v34' is listed twice" },
    { "[modem]\nmodulations = v90, v21, v90\n", 2, "modulations: 'v90' is listed twice" },
    { "[modem]\nmodulations = v90,,v21\n", 2, "modulations: empty list item" },
    { "[modem]\nmodulations = v90,\n", 2, "modulations: empty list item" },
    { "[modem]\nmodulations = v90 v21\n", 2, "is not one of" },
    /* addresses */
    { "[network]\naddress = 192.168.1\n", 2, "address: '192.168.1' is not an IPv4 address" },
    { "[network]\naddress = 192.168.1.256\n", 2, "is not an IPv4 address" },
    { "[network]\naddress = 192.168.01.2\n", 2, "is not an IPv4 address" },
    { "[network]\naddress = 192.168.1.2.3\n", 2, "is not an IPv4 address" },
    { "[network]\naddress = 192.168.1.2/24\n", 2, "is not an IPv4 address" },
    { "[network]\naddress = none\n", 2, "is not an IPv4 address" },
    { "[network]\naddress = 0.0.0.0\n", 2, "address: 0.0.0.0 is not a usable host address" },
    { "[network]\naddress = 224.0.0.1\n", 2, "address: 224.0.0.1 is not a usable host address" },
    { "[network]\naddress = 127.0.0.1\n", 2, "not a usable host address" },
    { "[network]\nppp_peer = 255.255.255.255\n", 2, "ppp_peer: 255.255.255.255 is not a usable host address" },
    { "[network]\nppp_local = 0.1.2.3\n", 2, "ppp_local: 0.1.2.3 is not a usable host address" },
    { "[network]\ndns1 = 240.0.0.1\n", 2, "dns1: 240.0.0.1 is not a usable host address" },
    { "[network]\ngateway = 240.0.0.1\n", 2, "gateway: 240.0.0.1 is not a usable host address" },
    { "[network]\ngateway = router\n", 2, "gateway: 'router' is not an IPv4 address" },
    { "[network]\nlan = static\naddress = 192.168.1.2\ngateway = 10.0.0.1\n", 4, "gateway: 10.0.0.1 is not in the network of address 192.168.1.2" },
    { "[network]\nlan = static\ngateway = 192.168.1.2\naddress = 192.168.1.2\n", 3, "gateway: 192.168.1.2 is the same as address" },
    { "[network]\ndns2 = 0.0.0.0\n", 2, "dns2: 0.0.0.0 is not a usable host address" },
    { "[network]\nnetmask = 255.0.255.0\n", 2, "netmask: 255.0.255.0 is not a contiguous netmask" },
    { "[network]\nnetmask = 0.0.0.0\n", 2, "netmask: 0.0.0.0 is not a contiguous netmask of /1 to /30" },
    { "[network]\nnetmask = 255.255.255.255\n", 2, "/1 to /30" },
    { "[network]\nlan = static\naddress = 192.168.1.0\n", 3, "address: 192.168.1.0 is the network address of 255.255.255.0" },
    { "[network]\nlan = static\naddress = 192.168.1.255\n", 3, "address: 192.168.1.255 is the broadcast address of 255.255.255.0" },
    { "[network]\nlan = static\nnetmask = 255.255.255.252\naddress = 10.0.0.4\n", 4, "is the network address" },
    { "[network]\nppp_local = 10.0.0.1\nppp_peer = 10.0.0.1\n", 3, "ppp_peer: 10.0.0.1 is the same as ppp_local" },
    { "[network]\nppp_local = 10.64.64.2\n", 2, "is the same as" },
    { "[network]\nlan = static\naddress = 10.64.64.2\nnetmask = 255.0.0.0\n", 3, "address: 10.64.64.2 is the same as ppp_peer" },
    { "[network]\nlan = static\naddress = 10.64.64.1\nnetmask = 255.0.0.0\n", 3, "address: 10.64.64.1 is the same as ppp_local" },
    /* cross-field rules */
    { "[ppp]\nauth = pap\n", 2, "auth: pap needs at least one user" },
    { "[ppp]\n\n\nauth = chap\n", 4, "auth: chap needs at least one user" },
    { "[ppp]\nencryption = on\n", 2, "encryption needs MS-CHAPv2, so at least one user" },
    { "[ppp]\nencryption2 = on\n", 2, "encryption needs MS-CHAPv2, so at least one user" },
    { "[modem]\nv42 = off\n", 2, "v42bis = on needs v42 = on" },
    { "[modem]\nv42 = off\nv42bis = on\n", 3, "v42bis = on needs v42 = on" },
    { "[modem]\nx2 = yes\n", 2, "x2: 'yes' is not one of: on, off" },
    { "[modem]\nmodulations = v90\nx2 = on\n", 3, "[modem] x2 = on needs v34 in modulations" },
    { "[modem2]\nmodulations = v90\nx2 = on\n", 3, "[modem2] x2 = on needs v34 in modulations" },
    { "[modem]\nk56 = yes\n", 2, "k56: 'yes' is not one of: on, off" },
    { "[modem]\nmodulations = v90\nk56 = on\n", 3, "[modem] k56 = on needs v34 in modulations" },
    { "[modem2]\nmodulations = v90\nk56 = on\n", 3, "[modem2] k56 = on needs v34 in modulations" },
    /* users */
    { "[ppp]\nuser = alice\n", 2, "user: missing secret" },
    { "[ppp]\nuser = alice s3cret extra\n", 2, "user: unexpected text after the secret" },
    { "[ppp]\nuser = \"\" s3cret\n", 2, "user: empty name" },
    { "[ppp]\nuser = \"alice s3cret\n", 2, "user: unterminated quoted string" },
    { "[ppp]\nuser = \"al\\ice\" s\n", 2, "user: unknown escape \\i" },
    { "[ppp]\nuser = \"al\\x0\" s\n", 2, "user: bad \\x escape" },
    { "[ppp]\nuser = \"al\\x00ice\" s\n", 2, "user: bad \\x escape" },
    { "[ppp]\nuser = \"al\\xgg\" s\n", 2, "user: bad \\x escape" },
    { "[ppp]\nuser = \"alice\"s3cret\n", 2, "user: missing blank after closing quote" },
    { "[ppp]\nuser = al\"ice s\n", 2, "user: quote or backslash in a bare word" },
    { "[ppp]\nuser = alice s\\n\n", 2, "user: quote or backslash in a bare word" },
    { "[ppp]\nuser = alice <redacted>\n", 2, "user: secret is <redacted>" },
    { "[ppp]\nuser = alice a\nuser = bob b\nuser = alice c\n", 4, "user: name 'alice' already defined on line 2" },
    { "[ppp]\nuser = 0123456789012345678901234567890123456789012345678901234567890123 s\n", 2,
      "user: name is longer than 63 octets" },
    { "[ppp]\nuser = n 01234567890123456789012345678901234567890123456789012345678901234\n", 2,
      "user: secret is longer than 64 octets" },
    { "[ppp]\nuser=a a\nuser=b b\nuser=c c\nuser=d d\nuser=e e\nuser=f f\nuser=g g\nuser=h h\n"
      "user=i i\nuser=j j\nuser=k k\nuser=l l\nuser=m m\nuser=n n\nuser=o o\nuser=p p\nuser=q q\n", 18,
      "user: more than 16 users" },
    /* the telephone line */
    { "[line]\nnumber = 555-1234\n", 2, "number: '555-1234' is not 1..20 characters from 0-9 * # or the word any" },
    { "[line]\nnumber = 555 1234\n", 2, "number: '555 1234' is not" },
    { "[line]\nnumber = Any\n", 2, "number: 'Any' is not" },
    { "[line]\nnumber = 012345678901234567890\n", 2, "number: '012345678901234567890' is not" },
    { "[line]\nnumber =\n", 2, "number: missing value" },
    { "[line]\nnumber = 1\nnumber = 2\n", 3, "duplicate key 'number' (first set on line 2)" },
    { "[line]\ntones = uk\n", 2, "tones: 'uk' is not one of: us, eu" },
    { "[line]\nbusy_chance = 101\n", 2, "busy_chance: 101 is out of range 0..100" },
    { "[line]\nbusy_chance = -1\n", 2, "busy_chance: -1 is out of range 0..100" },
    { "[line]\nbusy_chance = 50%\n", 2, "busy_chance: '50%' is not a number" },
    { "[line]\ndrop_mean = 10081\n", 2, "drop_mean: 10081 is out of range 0..10080" },
    { "[line]\nsession_limit = 10081\n", 2, "session_limit: 10081 is out of range 0..10080" },
    { "[line]\nidle_timeout = -5\n", 2, "idle_timeout: -5 is out of range 0..10080" },
    { "[line]\nrings = 2\n", 2, "unknown key 'rings' in [line]" },
    { "[modem]\nnumber = 5\n", 2, "unknown key 'number' in [modem]" },
    { "[lines]\n", 1, "unknown section [lines] (sections: network ppp modem modem2 line line2 status log system services)" },
    /* the admin password */
    { "[status]\nadmin_password = <redacted>\n", 2, "admin_password: password is <redacted>" },
    { "[status]\nadmin_password = two words\n", 2, "admin_password: unexpected text after the password" },
    { "[status]\nadmin_password = \"open\n", 2, "admin_password: unterminated quoted string" },
    { "[status]\nadmin_password = \"a\\qb\"\n", 2, "admin_password: unknown escape \\q" },
    { "[status]\nadmin_password = \"a\\x00b\"\n", 2, "admin_password: bad \\x escape" },
    { "[status]\nadmin_password = a\"b\n", 2, "admin_password: quote or backslash in a bare word" },
    { "[status]\nadmin_password = 01234567890123456789012345678901234567890123456789012345678901234\n", 2,
      "admin_password: password is longer than 64 octets" },
    { "[status]\nadmin_password = a\nadmin_password = b\n", 3, "duplicate key 'admin_password'" },
    { "[ppp]\nadmin_password = a\n", 2, "unknown key 'admin_password' in [ppp]" },
};

static void test_errors(void)
{
    size_t i;
    cfg_t c, d;
    cfg_default(&d);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char prefix[32];
        int ok = 1;
        memset(&c, 0x77, sizeof c);
        if (parse(&c, bad[i].text) != CFG_ERR) ok = 0;
        if (bad[i].line) snprintf(prefix, sizeof prefix, "t.conf:%d: ", bad[i].line);
        else snprintf(prefix, sizeof prefix, "t.conf: ");
        if (strncmp(err, prefix, strlen(prefix)) != 0) ok = 0;
        if (!strstr(err, bad[i].what)) ok = 0;
        /* a failed parse leaves the defaults, never half a file */
        if (memcmp(&c, &d, sizeof c) != 0) ok = 0;
        CHECK(ok);
        if (!ok) printf("  case %d: want line %d \"%s\"\n           got \"%s\"\n",
                        (int)i, bad[i].line, bad[i].what, err);
    }
}

static void test_limits(void)
{
    cfg_t c;
    char text[2 * CFG_LINE_MAX + 64];
    char small[24];
    size_t n;

    /* 63 octet name and 64 octet secret are accepted */
    CHECK_EQ_I(parse(&c, "[ppp]\nuser = "
        "012345678901234567890123456789012345678901234567890123456789012 "
        "0123456789012345678901234567890123456789012345678901234567890123\n"), CFG_OK);
    CHECK_EQ_I(strlen(c.users[0].name), 63);
    CHECK_EQ_I(strlen(c.users[0].secret), 64);

    /* the number: 1 and 20 characters, every allowed character, and any */
    CHECK_EQ_I(parse(&c, "[line]\nnumber = 0\n"), CFG_OK);
    CHECK(strcmp(c.line_number, "0") == 0);
    CHECK_EQ_I(parse(&c, "[line]\nnumber = 0123456789*#01234567\n"), CFG_OK);
    CHECK(strcmp(c.line_number, "0123456789*#01234567") == 0);
    CHECK_EQ_I(parse(&c, "[line]\nnumber = #\n"), CFG_OK);
    CHECK(strcmp(c.line_number, "#") == 0);
    CHECK_EQ_I(parse(&c, "[line]\nnumber = 5\n[line]\ntones = eu\n"), CFG_OK);
    CHECK_EQ_I(parse(&c, "[line]\nnumber = any\n"), CFG_OK);
    CHECK(c.line_number[0] == '\0');

    /* the admin password: empty in three spellings, bare, and 64 octets */
    CHECK_EQ_I(parse(&c, "[status]\nadmin_password =\n"), CFG_OK);
    CHECK(c.admin_password[0] == '\0');
    CHECK_EQ_I(parse(&c, "[status]\nadmin_password =   \n"), CFG_OK);
    CHECK(c.admin_password[0] == '\0');
    CHECK_EQ_I(parse(&c, "[status]\nadmin_password = \"\"\n"), CFG_OK);
    CHECK(c.admin_password[0] == '\0');
    CHECK_EQ_I(parse(&c, "[status]\nadmin_password = hunter2\n"), CFG_OK);
    CHECK(strcmp(c.admin_password, "hunter2") == 0);
    CHECK_EQ_I(parse(&c, "[status]\nadmin_password = \"<redacted>\"\n"), CFG_OK);
    CHECK(strcmp(c.admin_password, "<redacted>") == 0);
    CHECK_EQ_I(parse(&c, "[status]\nadmin_password = "
        "0123456789012345678901234567890123456789012345678901234567890123\n"), CFG_OK);
    CHECK_EQ_I(strlen(c.admin_password), 64);

    /* a line of exactly CFG_LINE_MAX is read, one more is refused */
    strcpy(text, "[ppp]\n#");
    n = strlen(text);
    memset(text + n, 'x', CFG_LINE_MAX - 1);
    strcpy(text + n + CFG_LINE_MAX - 1, "\nmru = 1400\n");
    CHECK_EQ_I(parse(&c, text), CFG_OK);
    CHECK_EQ_I(c.mru, 1400);
    memset(text + n, 'x', CFG_LINE_MAX);
    strcpy(text + n + CFG_LINE_MAX, "\nmru = 1400\n");
    CHECK_EQ_I(parse(&c, text), CFG_ERR);
    CHECK(strstr(err, "t.conf:2: line is longer than 640 characters") != NULL);

    /* NUL in the text is a byte like any other bad byte */
    err[0] = 0;
    CHECK_EQ_I(cfg_parse(&c, "t.conf", "[ppp]\nmru\0= 9\n", 14, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "t.conf:2: invalid byte 0x00 at column 4") != NULL);

    /* tiny and absent error buffers are safe */
    memset(small, '#', sizeof small);
    CHECK_EQ_I(cfg_parse(&c, "t.conf", "[nope]\n", 7, small, 8), CFG_ERR);
    CHECK(memcmp(small, "t.conf:", 8) == 0);     /* 7 chars + NUL */
    CHECK(small[8] == '#');
    CHECK_EQ_I(cfg_parse(&c, "t.conf", "[nope]\n", 7, NULL, 0), CFG_ERR);
    CHECK_EQ_I(cfg_validate(&c, NULL, 0), CFG_OK);

    /* a very long file name cannot push the message out of the buffer */
    memset(text, 'p', 300); text[300] = 0;
    CHECK_EQ_I(cfg_parse(&c, text, "[nope]\n", 7, err, sizeof err), CFG_ERR);
    CHECK(strlen(err) < sizeof err);
    CHECK(strstr(err, ":1: unknown section [nope]") != NULL);
}

static void test_validate_struct(void)
{
    cfg_t c;
    cfg_default(&c);
    c.n_users = 17;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "users") != NULL);
    cfg_default(&c);
    c.n_users = -1;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    c.n_users = 1;                              /* empty name */
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "empty name") != NULL);
    cfg_default(&c);
    c.n_users = 1;                              /* unterminated name */
    memset(c.users[0].name, 'a', sizeof c.users[0].name);
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    c.n_users = 2;
    strcpy(c.users[0].name, "x"); strcpy(c.users[1].name, "x");
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "already defined") != NULL);
    cfg_default(&c);
    c.modulations = 0;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "modulations") != NULL);
    cfg_default(&c);
    c.modulations = 128;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    c.auth = 3;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    c.log_level = 4;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    c.law = -1;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    c.mru = 20000;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "[ppp] mru: 20000 is out of range 68..1500") == 0);

    cfg_default(&c);
    strcpy(c.line_number, "555-1234");
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "[line] number: only 0-9 * # are allowed") == 0);
    cfg_default(&c);
    strcpy(c.line_number, "any");               /* in the struct "any" is "" */
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    cfg_default(&c);
    memset(c.line_number, '5', sizeof c.line_number);   /* unterminated */
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "[line] number: longer than 20 characters") == 0);
    cfg_default(&c);
    c.line_tones = 2;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "[line] tones") != NULL);
    cfg_default(&c);
    c.busy_chance = 101;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "[line] busy_chance: 101 is out of range 0..100") == 0);
    cfg_default(&c);
    c.drop_mean_min = 10081;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "[line] drop_mean") != NULL);
    cfg_default(&c);
    c.session_limit_min = -1;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "[line] session_limit") != NULL);
    cfg_default(&c);
    c.idle_timeout_min = 10081;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "[line] idle_timeout") != NULL);
    cfg_default(&c);
    memset(c.admin_password, 'p', sizeof c.admin_password);     /* unterminated */
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "[status] admin_password: password is longer than 64 octets") == 0);
}

/* worst case for the writer: 16 users whose every octet needs an escape */
static void worst(cfg_t *c)
{
    int i;
    cfg_default(c);
    c->lan_dhcp = 0;
    c->lan_addr = IP(223, 255, 255, 100);
    c->lan_netmask = IP(255, 255, 255, 128);
    c->ppp_local = IP(192, 168, 100, 101);
    c->ppp_peer = IP(192, 168, 100, 102);
    c->dns[0] = IP(208, 167, 222, 222);
    c->dns[1] = IP(208, 167, 220, 220);
    c->auth = CFG_AUTH_CHAP;
    c->status_port = 65535;
    c->log_level = CFG_LOG_DEBUG;
    c->n_users = CFG_MAX_USERS;
    for (i = 0; i < CFG_MAX_USERS; i++) {
        memset(c->users[i].name, 0x80 + i, CFG_NAME_MAX);
        memset(c->users[i].secret, 0x01 + i, CFG_SECRET_MAX);
    }
    memset(c->line_number, '8', CFG_NUMBER_MAX);
    c->line_tones = CFG_TONES_EU;
    c->busy_chance = 100;
    c->drop_mean_min = c->session_limit_min = c->idle_timeout_min = 10080;
    memset(c->admin_password, 0xff, CFG_SECRET_MAX);
    c->n_services = CFG_MAX_SERVICES;
    for (i = 0; i < CFG_MAX_SERVICES; i++) {
        cfg_service_t *e = &c->services[i];
        memset(e->number, '5', CFG_SVC_NUMBER_MAX); e->number[0] = (char)('a' - 'a' + '1'); e->number[1] = (char)('0' + i / 10); e->number[2] = (char)('0' + i % 10);
        memset(e->host, 'h', CFG_SVC_HOST_MAX);
        memset(e->name, '~', CFG_SVC_NAME_MAX);
        e->port = 65535; e->mode = CFG_SVC_TELNET;
    }
}

static void test_write(void)
{
    static char a[CFG_TEXT_MAX], b[CFG_TEXT_MAX], g[CFG_TEXT_MAX + 16];
    cfg_t c, d;
    int n, m, cap;

    /* defaults: canonical text loads back to the same struct */
    cfg_default(&c);
    n = cfg_write(&c, 1, a, sizeof a);
    CHECK(n > 0);
    CHECK_EQ_I((int)strlen(a), n);
    CHECK(strstr(a, "[network]\nlan = dhcp\naddress = 192.168.1.2\n") != NULL);
    CHECK(strstr(a, "modulations = v90, v34, v32bis, v22bis, v22, v21\n") != NULL);
    CHECK(strstr(a, "tx_level = -12\n") != NULL);
    CHECK(strstr(a, "\n[modem]\nmodulations = v90, v34, v32bis, v22bis, v22, v21\nmax_downstream = 56000\n"
                    "max_upstream = 33600\ntx_level = -12\nv42 = on\nv42bis = on\nv92 = on\nx2 = off\nk56 = off\nrings = 1\n") != NULL);
    CHECK(strstr(a, "\n[modem2]\nmodulations = v90, v34, v32bis, v22bis, v22, v21\nmax_downstream = 56000\n"
                    "max_upstream = 33600\ntx_level = -12\nv42 = on\nv42bis = on\nv92 = on\nx2 = off\nk56 = off\nrings = 1\n") != NULL);
    CHECK(strstr(a, "\n[log]\nlevel = info\n") != NULL);
    CHECK(strstr(a, "\n[line]\nnumber = any\ntones = us\nbusy_chance = 0\ndrop_mean = 0\n"
                    "session_limit = 0\nidle_timeout = 0\nservice = internet\ndreamcast_now = off\ndreamcast_lan = off\ndreamcast_upnp = off\ndreamcast_iwango = off\nsaturn_matchmaking = off\n\n[modem2]\n") != NULL);
    CHECK(strstr(a, "port = 80\nadmin_password = \"\"\n") != NULL);
    CHECK_EQ_I(parse(&d, a), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    /* with no secret anywhere the redacted text is the same but for its
     * first line: an empty admin password is shown as empty, so the dump
     * tells whether one is set */
    n = cfg_write(&c, 0, b, sizeof b);
    CHECK(n > 0);
    CHECK(strstr(b, "admin_password = \"\"\n") != NULL);
    CHECK(strstr(b, "redacted>") == NULL);

    /* full file: parse, write, parse, write is a fixed point */
    CHECK_EQ_I(parse(&c, full), CFG_OK);
    n = cfg_write(&c, 1, a, sizeof a);
    CHECK(n > 0);
    CHECK(strstr(a, "dns2 = none\n") != NULL);
    CHECK(strstr(a, "\nv42bis = off\nv92 = on\nx2 = off\nk56 = off\nrings = 10\n") != NULL);
    /* PPP compression and per-port encryption are written with the rest of [ppp] */
    CHECK(strstr(a, "\necho_interval = 0\ncompression = off\nencryption = off\nencryption2 = off\nuser = ") != NULL);
    /* V.92 upstream rates: up to 48000 goes through a write and a parse */
    c.max_up = 48000; c.v92 = 0;
    CHECK(cfg_write(&c, 1, a, sizeof a) > 0);
    CHECK(strstr(a, "\nmax_upstream = 48000\n") != NULL && strstr(a, "\nv92 = off\nx2 = off\nk56 = off\nrings = 10\n") != NULL);
    CHECK_EQ_I(parse(&d, a), CFG_OK);
    CHECK_EQ_I(d.max_up, 48000); CHECK_EQ_I(d.v92, 0);
    CHECK_EQ_I(parse(&c, full), CFG_OK);
    n = cfg_write(&c, 1, a, sizeof a);
    CHECK(strstr(a, "user = \"alice\" \"wonderland\"\n") != NULL);
    CHECK(strstr(a, "user = \"bob smith\" \"a \\\"q\\\" \\\\ \\x01\\xff end\"\n") != NULL);
    CHECK(strstr(a, "user = \"carol\" \"\"\n") != NULL);
    CHECK(strstr(a, "number = *70#5551234\ntones = eu\nbusy_chance = 100\ndrop_mean = 10080\n"
                    "session_limit = 120\nidle_timeout = 15\n") != NULL);
    CHECK(strstr(a, "admin_password = \"open \\\"sesame\\\" \\xfe\"\n") != NULL);
    CHECK_EQ_I(parse(&d, a), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    m = cfg_write(&d, 1, b, sizeof b);
    CHECK_EQ_I(m, n);
    CHECK(strcmp(a, b) == 0);

    /* redacted: no secret appears, names do, and it does not load */
    n = cfg_write(&c, 0, a, sizeof a);
    CHECK(n > 0);
    CHECK(strstr(a, "wonderland") == NULL);
    CHECK(strstr(a, "end") == NULL);
    CHECK(strstr(a, "user = \"alice\" <redacted>\n") != NULL);
    CHECK(strstr(a, "user = \"bob smith\" <redacted>\n") != NULL);
    CHECK(strstr(a, "user = \"carol\" <redacted>\n") != NULL);
    CHECK(strstr(a, "sesame") == NULL);
    CHECK(strstr(a, "admin_password = <redacted>\n") != NULL);
    CHECK(strstr(a, "number = *70#5551234\n") != NULL);
    CHECK_EQ_I(parse(&d, a), CFG_ERR);
    CHECK(strstr(err, "secret is <redacted>") != NULL);
    /* the admin password alone is enough to make the dump unloadable */
    c.n_users = 0; c.auth = CFG_AUTH_NONE;
    CHECK(cfg_write(&c, 0, a, sizeof a) > 0);
    CHECK_EQ_I(parse(&d, a), CFG_ERR);
    CHECK(strstr(err, "admin_password: password is <redacted>") != NULL);
    CHECK_EQ_I(parse(&c, full), CFG_OK);

    /* worst case fits the documented sizes, is ASCII, and round-trips */
    worst(&c);
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);
    n = cfg_write(&c, 1, a, sizeof a);
    CHECK(n > 0 && n < CFG_TEXT_MAX);
    for (m = 0; m < n; m++)
        if (!(a[m] == '\n' || (a[m] >= 0x20 && a[m] < 0x7f))) break;
    CHECK_EQ_I(m, n);
    CHECK_EQ_I(parse(&d, a), CFG_OK);
    if (err[0]) printf("  unexpected: %s\n", err);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    m = cfg_write(&c, 0, b, CFG_TEXT_REDACTED_MAX);
    CHECK(m > 0 && m < CFG_TEXT_REDACTED_MAX);
    printf("  worst-case config text: %d bytes with secrets, %d redacted\n", n, m);

    /* every capacity: never a byte past cap, NUL inside, -1 until it fits */
    for (cap = 0; cap <= n + 1; cap++) {
        int r, ok = 1;
        memset(g, '#', sizeof g);
        r = cfg_write(&c, 1, g, (size_t)cap);
        if (cap <= n && r != -1) ok = 0;
        if (cap == n + 1 && r != n) ok = 0;
        if (g[cap] != '#' || g[cap + 1] != '#') ok = 0;
        if (cap > 0 && memchr(g, '\0', (size_t)cap) == NULL) ok = 0;
        if (cap > 0 && strncmp(g, a, strlen(g)) != 0) ok = 0;   /* clean prefix */
        if (!ok) { CHECK(ok); printf("  cap %d r %d\n", cap, r); break; }
    }
    CHECK(cap == n + 2);
}

static int put_file(const char *path, const char *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    while (len) {
        ssize_t w = write(fd, data, len);
        if (w <= 0) { close(fd); return -1; }
        data += w; len -= (size_t)w;
    }
    return close(fd);
}

static void test_load(void)
{
    static char big[CFG_FILE_MAX + 2];
    const char *tmp = "build/test_config.tmp";
    cfg_t c, d;
    int r;

    /* missing file: defaults, and told apart from success */
    cfg_default(&d);
    memset(&c, 0x33, sizeof c);
    strcpy(err, "x");
    CHECK_EQ_I(cfg_load(&c, "build/no/such/v90d.conf", err, sizeof err), CFG_DEFAULTS);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    CHECK(err[0] == '\0');

    /* a real file, and the message carries its path */
    CHECK_EQ_I(put_file(tmp, full, strlen(full)), 0);
    CHECK_EQ_I(cfg_load(&c, tmp, err, sizeof err), CFG_OK);
    CHECK_EQ_I(c.n_users, 3);
    CHECK_EQ_I(c.status_port, 8080);
    CHECK_EQ_I(put_file(tmp, "[system]\nupdate_check = on\n", 27), 0);
    memset(&c, 0, sizeof c);
    CHECK_EQ_I(cfg_load(&c, tmp, err, sizeof err), CFG_OK);
    CHECK_EQ_I(c.update_check, 1);
    CHECK_EQ_I(put_file(tmp, "[ppp]\nmru = 9\n", 14), 0);
    memset(&c, 0x33, sizeof c);
    CHECK_EQ_I(cfg_load(&c, tmp, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "build/test_config.tmp:2: [ppp] mru: 9 is out of range 68..1500") == 0);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* exactly CFG_FILE_MAX is read, one byte more is refused */
    memset(big, '\n', sizeof big);
    memcpy(big, "[ppp]\nmru = 1400", 16);
    CHECK_EQ_I(put_file(tmp, big, CFG_FILE_MAX), 0);
    CHECK_EQ_I(cfg_load(&c, tmp, err, sizeof err), CFG_OK);
    CHECK_EQ_I(c.mru, 1400);
    CHECK_EQ_I(put_file(tmp, big, CFG_FILE_MAX + 1), 0);
    CHECK_EQ_I(cfg_load(&c, tmp, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "build/test_config.tmp: file is larger than 16384 bytes") != NULL);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* something that exists but cannot be read as a file */
    r = cfg_load(&c, "build", err, sizeof err);
    CHECK_EQ_I(r, CFG_ERR);
    CHECK(strncmp(err, "build: cannot read: ", 20) == 0);
    unlink(tmp);

    /* the shipped example loads and means what its comments say */
    r = cfg_load(&c, "v90d.conf.example", err, sizeof err);
    CHECK_EQ_I(r, CFG_OK);
    if (r != CFG_OK) printf("  example: %s\n", err);
    CHECK_EQ_I(c.lan_dhcp, 0);
    CHECK_EQ_I(c.auth, CFG_AUTH_CHAP);
    CHECK_EQ_I(c.n_users, 2);
    CHECK(strcmp(c.users[1].name, "bob smith") == 0);
    CHECK(strcmp(c.users[1].secret, "correct horse \"battery\" staple") == 0);
    CHECK(strcmp(c.line_number, "6387825442") == 0);
    /* apart from lan, auth, the users and the number it spells out the
     * defaults */
    c.lan_dhcp = 1; c.auth = CFG_AUTH_NONE; c.n_users = 0;
    memset(c.users, 0, sizeof c.users);
    memset(c.line_number, 0, sizeof c.line_number);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
}

static int exists(const char *path) { return access(path, F_OK) == 0; }

static void test_save(void)
{
    static char text[CFG_TEXT_MAX], want[CFG_TEXT_MAX], longp[CFG_PATH_MAX + 8];
    const char *dir = "build/test_save.d";
    const char *path = "build/test_save.d/v90d.conf";
    const char *tmp = "build/test_save.d/v90d.conf.tmp";
    struct stat sb;
    cfg_t c, d;
    int n, fd;
    ssize_t r;

    unlink(tmp); unlink(path); rmdir(dir);
    CHECK_EQ_I(mkdir(dir, 0700), 0);

    /* a new file: complete, loadable, private, and no temporary left */
    CHECK_EQ_I(parse(&c, full), CFG_OK);
    strcpy(err, "x");
    CHECK_EQ_I(cfg_save(&c, path, err, sizeof err), CFG_OK);
    CHECK(err[0] == '\0');
    CHECK(!exists(tmp));
    CHECK_EQ_I(stat(path, &sb), 0);
    CHECK_EQ_I(sb.st_mode & 0777, 0600);
    n = cfg_write(&c, 1, want, sizeof want);
    CHECK_EQ_I(sb.st_size, n);
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    r = read(fd, text, sizeof text);
    close(fd);
    CHECK_EQ_I(r, n);
    CHECK(r == n && memcmp(text, want, (size_t)n) == 0);
    CHECK(strstr(want, "wonderland") != NULL && strstr(want, "sesame") != NULL);
    CHECK_EQ_I(cfg_load(&d, path, err, sizeof err), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* replacing it, with a stale temporary of another mode in the way */
    CHECK_EQ_I(put_file(tmp, "stale stale stale", 17), 0);
    CHECK_EQ_I(chmod(tmp, 0644), 0);
    worst(&c);
    CHECK_EQ_I(cfg_save(&c, path, err, sizeof err), CFG_OK);
    CHECK(!exists(tmp));
    CHECK_EQ_I(stat(path, &sb), 0);
    CHECK_EQ_I(sb.st_mode & 0777, 0600);
    CHECK_EQ_I(cfg_load(&d, path, err, sizeof err), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* an invalid config is not written: the file keeps its content */
    c.busy_chance = 101;
    CHECK_EQ_I(cfg_save(&c, path, err, sizeof err), CFG_ERR);
    CHECK(strcmp(err, "build/test_save.d/v90d.conf: [line] busy_chance: 101 is out of range 0..100") == 0);
    CHECK(!exists(tmp));
    c.busy_chance = 100;
    CHECK_EQ_I(cfg_load(&d, path, err, sizeof err), CFG_OK);
    CHECK(memcmp(&c, &d, sizeof c) == 0);

    /* a directory that is not there */
    CHECK_EQ_I(cfg_save(&c, "build/no/such/dir/v90d.conf", err, sizeof err), CFG_ERR);
    CHECK(strncmp(err, "build/no/such/dir/v90d.conf: cannot write: ", 43) == 0);
    /* the target is a directory: the rename fails, the temporary is removed */
    CHECK_EQ_I(cfg_save(&c, dir, err, sizeof err), CFG_ERR);
    CHECK(strncmp(err, "build/test_save.d: cannot replace: ", 35) == 0);
    CHECK(!exists("build/test_save.d.tmp"));
    /* path limits */
    CHECK_EQ_I(cfg_save(&c, "", err, sizeof err), CFG_ERR);
    memset(longp, 'p', CFG_PATH_MAX + 1); longp[CFG_PATH_MAX + 1] = '\0';
    CHECK_EQ_I(cfg_save(&c, longp, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "path is longer than 255 characters") != NULL);
    CHECK_EQ_I(cfg_save(&c, "build/no/such/dir/v90d.conf", NULL, 0), CFG_ERR);

    CHECK_EQ_I(unlink(path), 0);
    CHECK_EQ_I(rmdir(dir), 0);                  /* nothing else was left in it */
}

/* [line] service: what kind of caller the port expects */
/* What a game console's port can be asked for besides the service itself:
 * three switches per port, all off unless asked. */
static void test_console_switches(void)
{
    static char text[CFG_TEXT_MAX];
    cfg_t c, d, v;

    cfg_default(&c);
    CHECK_EQ_I(c.dc_now, 0); CHECK_EQ_I(c.dc_lan, 0); CHECK_EQ_I(c.dc_upnp, 0);
    CHECK_EQ_I(c.port2.dc_now, 0); CHECK_EQ_I(c.port2.dc_lan, 0); CHECK_EQ_I(c.port2.dc_upnp, 0);
    CHECK_EQ_I(parse(&c, "[line]\nservice = dreamcast\ndreamcast_now = on\n[line2]\ndreamcast_lan = on\ndreamcast_upnp = on\n"), CFG_OK);
    CHECK_EQ_I(c.dc_now, 1); CHECK_EQ_I(c.dc_lan, 0); CHECK_EQ_I(c.dc_upnp, 0);
    CHECK_EQ_I(c.port2.dc_now, 0); CHECK_EQ_I(c.port2.dc_lan, 1); CHECK_EQ_I(c.port2.dc_upnp, 1);
    cfg_for_port(&c, 1, &v);
    CHECK_EQ_I(v.dc_now, 0); CHECK_EQ_I(v.dc_lan, 1); CHECK_EQ_I(v.dc_upnp, 1);
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "\nservice = dreamcast\ndreamcast_now = on\ndreamcast_lan = off\ndreamcast_upnp = off\n") != NULL);
    CHECK(strstr(text, "\nservice = internet\ndreamcast_now = off\ndreamcast_lan = on\ndreamcast_upnp = on\n") != NULL);
    CHECK_EQ_I(parse(&d, text), CFG_OK);
    CHECK_EQ_I(d.dc_now, 1); CHECK_EQ_I(d.port2.dc_lan, 1); CHECK_EQ_I(d.port2.dc_upnp, 1); CHECK_EQ_I(d.port2.dc_now, 0);
    /* a fourth: the VPN for the IWANGO games */
    cfg_default(&c);
    CHECK_EQ_I(c.dc_iwango, 0); CHECK_EQ_I(c.port2.dc_iwango, 0);
    CHECK_EQ_I(parse(&c, "[line2]\ndreamcast_iwango = on\n"), CFG_OK);
    CHECK_EQ_I(c.dc_iwango, 0); CHECK_EQ_I(c.port2.dc_iwango, 1);
    cfg_for_port(&c, 1, &v); CHECK_EQ_I(v.dc_iwango, 1);
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "dreamcast_upnp = off\ndreamcast_iwango = off\n") != NULL);
    CHECK(strstr(text, "dreamcast_upnp = off\ndreamcast_iwango = on\n") != NULL);
    CHECK_EQ_I(parse(&d, text), CFG_OK); CHECK_EQ_I(d.port2.dc_iwango, 1); CHECK_EQ_I(d.dc_iwango, 0);
    CHECK_EQ_I(parse(&c, "[line]\ndreamcast_iwango = 1\n"), CFG_ERR);
    CHECK_EQ_I(parse(&c, "[line]\ndreamcast_now = maybe\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [line] dreamcast_now: 'maybe' is not one of: on, off");
    cfg_default(&c); c.port2.dc_upnp = 2;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
}

static void test_service(void)
{
    static char text[CFG_TEXT_MAX];
    cfg_t c, d, v;

    cfg_default(&c);
    CHECK_EQ_I(c.service, CFG_SERVICE_INTERNET); CHECK_EQ_I(c.port2.service, CFG_SERVICE_INTERNET);
    CHECK_EQ_I(parse(&c, "[line]\nservice = dreamcast\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_DREAMCAST); CHECK_EQ_I(c.port2.service, CFG_SERVICE_INTERNET);
    CHECK_EQ_I(parse(&c, "[line2]\nservice = dreamcast\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_INTERNET); CHECK_EQ_I(c.port2.service, CFG_SERVICE_DREAMCAST);
    cfg_for_port(&c, 1, &v);
    CHECK_EQ_I(v.service, CFG_SERVICE_DREAMCAST);
    cfg_for_port(&c, 0, &v);
    CHECK_EQ_I(v.service, CFG_SERVICE_INTERNET);
    /* written back and read again */
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "\nidle_timeout = 0\nservice = internet\n") != NULL);
    CHECK(strstr(text, "\nidle_timeout = 0\nservice = dreamcast\n") != NULL);
    CHECK_EQ_I(parse(&d, text), CFG_OK);
    CHECK_EQ_I(d.service, CFG_SERVICE_INTERNET); CHECK_EQ_I(d.port2.service, CFG_SERVICE_DREAMCAST);
    /* archive, per port */
    CHECK_EQ_I(parse(&c, "[line]\nservice = archive\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_ARCHIVE); CHECK_EQ_I(c.port2.service, CFG_SERVICE_INTERNET);
    /* the switch for the whole box of older settings still reads, and means
     * both ports unless a port says what it wants; it is not written again */
    CHECK_EQ_I(parse(&c, "[network]\narchive = on\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_ARCHIVE); CHECK_EQ_I(c.port2.service, CFG_SERVICE_ARCHIVE);
    CHECK_EQ_I(parse(&c, "[network]\narchive = on\n[line2]\nservice = dreamcast\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_ARCHIVE); CHECK_EQ_I(c.port2.service, CFG_SERVICE_DREAMCAST);
    CHECK_EQ_I(parse(&c, "[line]\nservice = internet\n[network]\narchive = on\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_INTERNET); CHECK_EQ_I(c.port2.service, CFG_SERVICE_ARCHIVE);
    CHECK_EQ_I(parse(&c, "[network]\narchive = off\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_INTERNET);
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "archive =") == NULL);
    /* anything else is refused, by name */
    CHECK_EQ_I(parse(&c, "[line]\nservice = netlink\n"), CFG_ERR);
    CHECK_EQ_S(err, "t.conf:2: [line] service: 'netlink' is not one of: internet, archive, dreamcast, saturn");
    CHECK_EQ_I(parse(&c, "[line2]\nservice = saturn\n"), CFG_OK);
    CHECK_EQ_I(c.service, CFG_SERVICE_INTERNET); CHECK_EQ_I(c.port2.service, CFG_SERVICE_SATURN);
    CHECK(cfg_write(&c, 1, text, sizeof text) > 0);
    CHECK(strstr(text, "service = saturn") != NULL);
    cfg_default(&c); c.port2.service = 4;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    CHECK(strstr(err, "[line2] service") != NULL);
}

static void test_services(void)
{
    cfg_t c; cfg_service_t e; const char *why = "";
    static char a[CFG_TEXT_MAX];
    /* the value of a service line */
#define SP(v) cfg_svc_parse(v, strlen(v), &e, &why)
    CHECK_EQ_I(SP("13855961775 nwnrevival.com:5000 raw AOL Neverwinter Nights"), 0);
    CHECK_EQ_S(e.number, "13855961775"); CHECK_EQ_S(e.host, "nwnrevival.com");
    CHECK_EQ_I(e.port, 5000); CHECK_EQ_I(e.mode, CFG_SVC_RAW);
    CHECK_EQ_S(e.name, "AOL Neverwinter Nights");
    CHECK_EQ_I(SP("  555   bbs.example.org:23\ttelnet   A  B  "), 0);
    CHECK_EQ_S(e.name, "A  B"); CHECK_EQ_I(e.mode, CFG_SVC_TELNET); CHECK_EQ_I(e.port, 23);
    CHECK_EQ_I(SP("192.168.1.5"), -1);
    CHECK_EQ_I(SP("55 h:1 raw x"), -1);                          /* number too short */
    CHECK_EQ_I(SP("1234567890123456 h:1 raw x"), -1);            /* too long */
    CHECK_EQ_I(SP("12a h:1 raw x"), -1);
    CHECK_EQ_I(SP("123 h raw x"), -1);                           /* no port */
    CHECK_EQ_I(SP("123 :23 raw x"), -1);                         /* no host */
    CHECK_EQ_I(SP("123 -h:23 raw x"), -1);
    CHECK_EQ_I(SP("123 h_x:23 raw x"), -1);
    CHECK_EQ_I(SP("123 h:0 raw x"), -1);
    CHECK_EQ_I(SP("123 h:023 raw x"), -1);
    CHECK_EQ_I(SP("123 h:65536 raw x"), -1);
    CHECK_EQ_I(SP("123 h:65535 raw x"), 0);
    CHECK_EQ_I(SP("123 h:23 ssh x"), -1);
    CHECK_EQ_I(SP("123 h:23 raw"), -1);                          /* no name */
    CHECK_EQ_I(SP("123 h:23 raw 123456789012345678901234567890123456789012345678"), -1);
    CHECK_EQ_I(SP("123 h:23 raw 12345678901234567890123456789012345678901234567"), 0);
    CHECK_EQ_I(SP("123 h:23 raw caf\xc3\xa9"), -1);
    CHECK_EQ_I(SP("123 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:23 raw x"), -1);
#undef SP
    /* numbers */
    CHECK(cfg_svc_number_match("13855961775", "13855961775"));
    CHECK(cfg_svc_number_match("13855961775", "3855961775"));
    CHECK(cfg_svc_number_match("3855961775", "13855961775"));
    CHECK(!cfg_svc_number_match("23855961775", "3855961775"));
    CHECK(!cfg_svc_number_match("13855961775", "855961775"));
    CHECK(!cfg_svc_number_match("555", "5555"));
    CHECK(cfg_svc_number_taken("8002273425") && cfg_svc_number_taken("18002273425"));
    CHECK(cfg_svc_number_taken("2278353422") && cfg_svc_number_taken("12278353422"));
    CHECK(!cfg_svc_number_taken("13855961775") && !cfg_svc_number_taken("227"));

    /* in a file */
    CHECK_EQ_I(parse(&c, "[services]\nnetstalgia = off\nservice = 13855961775 nwnrevival.com:5000 raw AOL NWN\n"
                         "service = 5551234 192.168.1.20:23 telnet My board\n"), CFG_OK);
    CHECK_EQ_I(c.svc_netstalgia, 0); CHECK_EQ_I(c.n_services, 2);
    CHECK_EQ_S(c.services[1].host, "192.168.1.20"); CHECK_EQ_S(c.services[1].name, "My board");
    CHECK(cfg_write(&c, 1, a, sizeof a) > 0);
    CHECK(strstr(a, "\n[services]\nnetstalgia = off\nmenu = off\nservice = 13855961775 nwnrevival.com:5000 raw AOL NWN\n"
                    "service = 5551234 192.168.1.20:23 telnet My board\n") != NULL);
    { cfg_t d; CHECK_EQ_I(parse(&d, a), CFG_OK); CHECK(memcmp(&c, &d, sizeof c) == 0); }
    cfg_default(&c); CHECK_EQ_I(c.svc_netstalgia, 1); CHECK_EQ_I(c.n_services, 0);
    CHECK_EQ_I(c.svc_menu, 0);                                  /* the terminal menu: off unless asked for */
    CHECK_EQ_I(parse(&c, "[services]\nmenu = on\n"), CFG_OK);
    CHECK_EQ_I(c.svc_menu, 1);
    CHECK(cfg_write(&c, 1, a, sizeof a) > 0);
    CHECK(strstr(a, "\n[services]\nnetstalgia = on\nmenu = on\n") != NULL);
    CHECK_EQ_I(parse(&c, "[services]\nmenu = maybe\n"), CFG_ERR);
    CHECK_EQ_I(parse(&c, "[services]\nservice = 555 h:1 raw\n"), CFG_ERR);
    CHECK(strstr(err, "t.conf:2: [services] service: missing name") != NULL);
    CHECK_EQ_I(parse(&c, "[services]\nservice = 2278353422 h:1 raw x\n"), CFG_ERR);
    CHECK(strstr(err, "the box answers itself") != NULL);
    CHECK_EQ_I(parse(&c, "[services]\nservice = 13855961775 h:1 raw x\nservice = 3855961775 h:2 raw y\n"), CFG_ERR);
    CHECK(strstr(err, "listed twice") != NULL);
    CHECK_EQ_I(parse(&c, "[line]\nnumber = 5551234\n[services]\nservice = 5551234 h:1 raw x\n"), CFG_ERR);
    CHECK(strstr(err, "the number of port 1") != NULL);
    CHECK_EQ_I(parse(&c, "[line2]\nnumber = 5551234\n[services]\nservice = 5551234 h:1 raw x\n"), CFG_ERR);
    CHECK(strstr(err, "the number of port 2") != NULL);
    {
        char f[4096]; size_t n = 0; int i;
        n += (size_t)snprintf(f + n, sizeof f - n, "[services]\n");
        for (i = 0; i <= CFG_MAX_SERVICES; i++) n += (size_t)snprintf(f + n, sizeof f - n, "service = %d h:1 raw x\n", 100 + i);
        CHECK_EQ_I(parse(&c, f), CFG_ERR);
        CHECK(strstr(err, "more than 16 services") != NULL);
    }
    /* a struct the form made: same rules */
    cfg_default(&c); c.n_services = 1;
    strcpy(c.services[0].number, "555"); strcpy(c.services[0].host, "h"); strcpy(c.services[0].name, " x");
    c.services[0].port = 1;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    strcpy(c.services[0].name, "x"); c.services[0].port = 0;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    c.services[0].port = 7; c.services[0].mode = 2;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_ERR);
    c.services[0].mode = CFG_SVC_TELNET;
    CHECK_EQ_I(cfg_validate(&c, err, sizeof err), CFG_OK);
}

int main(void)
{
    test_defaults();
    test_gateway();
    test_more_mods();
    test_port2();
    test_service();
    test_services();
    test_console_switches();
    test_full();
    test_errors();
    test_limits();
    test_validate_struct();
    test_write();
    test_load();
    test_save();
    T_REPORT("test_config");
}
