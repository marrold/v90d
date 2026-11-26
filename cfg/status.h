/* v90d/cfg - what the daemon knows about itself, and two renderers for it.
 *
 * status_t is a plain struct the daemon fills in; nothing here reads the
 * modem. The renderers write an HTML page (nothing from elsewhere, the
 * box's one script only, refreshes itself every 5 s with a meta tag, the
 * menu of page.h) and a JSON object
 * into a caller buffer. They never write past the buffer, always NUL
 * terminate (when cap > 0) and return the length without the NUL, or -1 if
 * the buffer is too small. STATUS_HTML_MAX and STATUS_JSON_MAX are large
 * enough for any struct content.
 *
 * Output is pure ASCII whatever the struct holds. Strings that come from
 * outside (the peer name, the dialed digits) are escaped for the format: in HTML the five
 * special characters become entities and octets outside printable ASCII are
 * shown as \xHH; in JSON they become \" \\ or \u00HH. Enum fields with
 * values outside their range are shown as "unknown". */
#ifndef V90D_CFG_STATUS_H
#define V90D_CFG_STATUS_H

#include <stddef.h>
#include <stdint.h>

#define STATUS_NAME_MAX   63
#define STATUS_MAX_CALLS  20
#define STATUS_HISTORY    20     /* calls on the page of both ports together */
#define STATUS_DIALED_MAX 20
#define STATUS_HTML_MAX   16384
#define STATUS_JSON_MAX   8192

typedef enum {
    ST_LINE_IDLE = 0, ST_LINE_RINGING, ST_LINE_TRAINING, ST_LINE_CONNECTED,
    ST_LINE_HANGUP
} st_line_t;

typedef enum {
    ST_MOD_NONE = 0, ST_MOD_V90, ST_MOD_V34, ST_MOD_V32BIS, ST_MOD_V22BIS,
    ST_MOD_V22, ST_MOD_V21,
    ST_MOD_V92,         /* appended: the values above stay as they were */
    ST_MOD_BELL103, ST_MOD_BELL212A,
    ST_MOD_X2,          /* appended: x2 (U.S. Robotics' 56k of 1997) */
    ST_MOD_K56FLEX,     /* appended: K56flex (the 1996 56k, experimental) */
    ST_MOD_V23          /* appended: V.23, 1200/75 bit/s asymmetric FSK */
} st_mod_t;

typedef enum {
    ST_V42_OFF = 0,     /* no error control */
    ST_V42_DETECT,      /* detection phase running */
    ST_V42_LAPM,        /* LAPM established */
    ST_V42_ASYNC        /* start-stop fallback */
} st_v42_t;

/* PPP compression/encryption (CCP) in one direction; the values of PPP_COMP_* */
typedef enum { ST_COMP_NONE = 0, ST_COMP_DEFLATE, ST_COMP_MPPC, ST_COMP_MPPE = 4 } st_comp_t;

typedef enum {
    ST_PPP_DEAD = 0, ST_PPP_ESTABLISH, ST_PPP_AUTHENTICATE, ST_PPP_NETWORK,
    ST_PPP_RUNNING, ST_PPP_TERMINATE
} st_ppp_t;

typedef enum {
    ST_END_NONE = 0,        /* unknown or still up */
    ST_END_CALLER_HANGUP,   /* on-hook seen */
    ST_END_CARRIER_LOST,
    ST_END_TRAIN_FAILED,    /* never reached data mode */
    ST_END_AUTH_FAILED,
    ST_END_PPP_CLOSED,      /* peer sent Terminate-Request */
    ST_END_ECHO_TIMEOUT,
    ST_END_LOCAL,           /* daemon shut down or cleared the call */
    ST_END_ERROR,
    ST_END_RETRAIN_FAILED   /* a retrain or rate renegotiation could not complete */
} st_end_t;

/* How a call ended at the telephone level, before or apart from what the
 * modem and PPP made of it (that is st_end_t). */
typedef enum {
    ST_TEL_ANSWERED = 0,    /* we answered; st_end_t tells the rest */
    ST_TEL_BUSY,            /* played the busy signal (busy_chance) */
    ST_TEL_WRONG_NUMBER,    /* dialed something else than the number */
    ST_TEL_ABANDONED,       /* caller hung up before we answered */
    ST_TEL_DROPPED,         /* simulated line drop (drop_mean) */
    ST_TEL_SESSION_LIMIT,   /* session_limit reached */
    ST_TEL_IDLE             /* idle_timeout reached */
} st_tel_t;

typedef struct {
    uint32_t start_time;    /* UTC seconds since 1970, 0 = clock not set */
    uint32_t duration_s;
    uint32_t down_bps;
    uint32_t up_bps;
    st_mod_t modulation;
    st_end_t end;
    char     dialed[STATUS_DIALED_MAX + 1];  /* digits the caller dialed, ""
                               = not known; the renderers stop at NUL or at
                               21 octets */
    int      tel_result;    /* ST_TEL_* */
} st_call_t;

/* IPv4 addresses in host byte order, 0 = none. */
typedef struct {
    uint32_t  uptime_s;
    /* line and modem */
    st_line_t line;
    st_mod_t  modulation;
    uint32_t  down_bps;
    uint32_t  up_bps;
    uint32_t  rtd_ms;           /* round-trip delay */
    int32_t   rx_level_x10;     /* receive level, tenths of dBm0 */
    int32_t   snr_x10;          /* receive SNR, tenths of dB */
    /* error control */
    st_v42_t  v42;
    int       compression;      /* V.42bis in use */
    int       service;          /* the port's service, 0 internet, 1 archive, 2 dreamcast, 3 saturn (the setting) */
    int       v92;              /* V.92 is allowed on this port (the setting, not a measurement) */
    /* PPP */
    st_ppp_t  ppp;
    char      peer_name[STATUS_NAME_MAX + 1];  /* as the peer sent it; the
                                    renderers stop at NUL or at 64 octets */
    uint32_t  peer_ip;
    uint32_t  local_ip;
    int       ppp_compression;  /* PPP compression is allowed (the setting, not a measurement) */
    int       ppp_encryption;   /* MPPE encryption is required (the setting, not a measurement) */
    int       ppp_comp_tx, ppp_comp_rx;     /* ST_COMP_*: agreed towards / from the caller */
    uint64_t  comp_tx_in, comp_tx_out;      /* this call: octets of IP given to the compressor,
                                               and what went out for them */
    uint64_t  comp_rx_in, comp_rx_out;      /* octets that arrived compressed, and the IP in them */
    /* counters, since start-up */
    uint64_t  rx_octets, tx_octets;
    uint64_t  rx_packets, tx_packets;
    uint64_t  rx_errors, tx_errors;
    /* call history: ring of the last STATUS_MAX_CALLS finished calls */
    uint32_t  calls_total;
    unsigned  n_calls;          /* valid entries, <= STATUS_MAX_CALLS */
    unsigned  call_next;        /* slot the next call goes to */
    st_call_t calls[STATUS_MAX_CALLS];
} status_t;

/* all zero: idle line, no history */
void status_init(status_t *s);

/* append a finished call, dropping the oldest when the ring is full */
void status_add_call(status_t *s, const st_call_t *call);

/* i-th most recent call (0 = newest), NULL when i >= number kept */
const st_call_t *status_call(const status_t *s, unsigned i);

int status_render_html(const status_t *s, char *buf, size_t cap);
/* The same page for a box with two ports: both ports' state at a glance,
 * links between the two views (/ and /port2), then everything else about
 * port `sel` (0 or 1). */
int status_render_ports(const status_t *const ports[2], int sel, char *buf, size_t cap);
/* The page of the last calls: the newest STATUS_HISTORY finished calls of
 * both ports in one table, newest first (by the time they began; port 1
 * first when two began in the same second), each with the port it came in
 * on. ports[1] is NULL on a box with one port. The history lives in memory:
 * a restart of the box empties it. */
int status_render_calls(const status_t *const ports[2], char *buf, size_t cap);
/* The BBS page: what to dial for the bulletin boards, and the boards, in
 * pages of STATUS_BBS_PAGE with a search. rows writes the boards that
 * answer the search q (letters in lower case and digits, "" = all) as <tr>
 * rows of three cells (name, name at the prompt, number) into its buffer:
 * it passes over the first skip of them, writes at most max, and says how
 * many it wrote and how many more follow; NULL = there is no list. query
 * is what followed the ? in the address (p = page from 1, q = search),
 * NULL if nothing did. */
#define STATUS_BBS_PAGE 25
/* (a rows that cannot give the boards now, while a call is in progress,
 * says *left = -1 and gives none) */
typedef size_t (*status_bbs_rows)(void *ctx, const char *q, int skip, int max, char *buf, size_t cap, int *shown, int *left);
int status_render_bbs(status_bbs_rows rows, void *ctx, const char *query, size_t qlen, char *buf, size_t cap);
int status_render_json(const status_t *s, char *buf, size_t cap);

#endif
