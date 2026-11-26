/* One dial-up call on the answering side: the complete data path
 *
 *     G.711 octets <-> answer modem (V.8, then V.90 or V.22bis / V.22 / V.21)
 *                  <-> V.42 endpoint, answerer (LAPM + V.42 bis, or the
 *                      start-stop fallback for callers without error control)
 *                  <-> PPP server
 *                  <-> IP packets (cfg.ppp.ip_rx up, dce_send_ip down)
 *
 * The caller owns a dce_t (about 160 kB), calls dce_init() when the line has been answered
 * and then dce_tick_pcm() once per 8 kHz sample (dce_tick() is the same at
 * a linear sample interface, without V.90). The tick is the only clock: it
 * also advances the millisecond timers of V.42 and PPP.
 *
 * V.90 (cfg.modulations with V90M_MOD_V90): PCM downstream, V.34 upstream, so the
 * two directions run at different rates (dce_status_t.bitrate towards the
 * caller, .rx_bitrate from it). V.42 and PPP sit on it exactly as on
 * V.22bis: the modem's data mode is a transparent synchronous bit stream.
 *
 * V.92 (V90M_MOD_V92 next to V90M_MOD_V90): PCM both ways when the caller
 * asks for it in Phase 2; nothing changes above the modem, except that such
 * a caller skips the ODP / ADP exchange (V.92 9.3.1: both modems V.92 and
 * LAPM in V.8) and the detection timer is lengthened for it. Offer V.92
 * only with error control enabled: our JM always echoes LAPM, so with V.42
 * switched off here a V.92 caller would wait for a LAPM that never comes.
 *
 * Life of a call (dce_status_t.state):
 *   DCE_TRAINING     modem start-up
 *   DCE_NEGOTIATING  carrier up, V.42 detection / establishment running
 *   DCE_ONLINE       V.42 is up (error corrected or start-stop), PPP running
 *   DCE_CLOSING      the call is being ended in an orderly way while the
 *                    carrier is still there: PPP's last frames are flushed,
 *                    LAPM is released, the final UA gets out
 *   DCE_ENDED        over: the transmitter is silent, every layer is down,
 *                    end_reason tells why. The owner should go on-hook.
 *
 * Flow control: downwards PPP output is only handed to V.42 as it has room
 * and dce_send_ip() returns PPP_ERR_FULL (try again later) when PPP's
 * transmit buffer is full; nothing accepted is ever dropped. Upwards
 * everything V.42 delivers is consumed at once by PPP and the ip_rx
 * callback, so LAPM never has to hold the far end off.
 *
 * On a V.21 call (300 bit/s FSK; Bell 103 and V.23 likewise) there is no
 * error control: V.42 does not apply to it and the modem hands characters,
 * not a bit-transparent stream. The V.42 endpoint is then put straight into
 * its start-stop mode.
 *
 * Portable C99, integer only, no allocation, no globals, not reentrant per
 * object. The modem and the V.42 endpoint (libv90modem) live in the dce_t's
 * own memory (v90m_init, v90m_ec_init). Callbacks run inside the tick; from a callback only dce_send_ip
 * and dce_get_status may be called. */
#ifndef V90D_DCE_H
#define V90D_DCE_H
#include <stddef.h>
#include <stdint.h>
#include "v90modem.h"
#include "ppp.h"

typedef enum {
    DCE_TRAINING = 0,
    DCE_NEGOTIATING,
    DCE_ONLINE,
    DCE_CLOSING,
    DCE_ENDED
} dce_state_t;

/* why the call ended (first cause wins) */
typedef enum {
    DCE_END_NONE = 0,
    DCE_END_NO_CARRIER,        /* modem start-up failed: no usable caller */
    DCE_END_CARRIER_LOST,      /* carrier dropped after the modem connected */
    DCE_END_V42_FAILED,        /* error control failed: see v42_reason */
    DCE_END_V42_RELEASED,      /* far end released the LAPM connection */
    DCE_END_AUTH_FAILED,       /* PPP: bad credentials, refused or timed out */
    DCE_END_PEER_CLOSED,       /* PPP: peer sent Terminate-Request */
    DCE_END_PPP_FAILED,        /* PPP: negotiation, keepalive, loop: see ppp_end */
    DCE_END_LOCAL,             /* dce_hangup() */
    DCE_END_CLEARDOWN,         /* the calling modem cleared down (V.90 9.7): its user hung up */
    DCE_END_X2_FAILED,         /* x2: the client took it up and its start-up or a rate change failed */
    DCE_END_RETRAIN_FAILED,    /* a retrain or rate renegotiation could not complete (V.90/V.92
                                  past the retrain limit; or an x2 client's Tone A retrain in data
                                  mode, which x2 cannot serve) */
    DCE_END_K56_FAILED         /* K56flex: the client took it up and its start-up, a renegotiation
                                  or a retrain did not complete */
} dce_end_t;

typedef struct {
    /* modem */
    int      tx_level;         /* transmit RMS, 16-bit sample units */
    unsigned modulations;      /* what we offer: V90M_MOD_* */
    int      alaw;             /* G.711 law of the codec: 0 = mu-law, 1 = A-law */
    int      echo_delay;       /* V.90: samples until the echo of a codeword we send
                                  starts to come back (line driver buffering);
                                  -1 = measured in every call (see p34d.h) */
    int      max_down, max_up; /* V.90: rate ceilings in bit/s, 0 = none (V.92 upstream: up to 48000) */
    /* bench settings (v90modem.h); 0 = as it always was */
    int      rx_linear;        /* the codec receives in 16-bit linear: the owner uses dce_tick_lin */
    int      max_tx_code;      /* INFO0d maximum downstream power code, 1..31; 0 = 23 (-12 dBm0) */
    /* V.42: answerer settings (role is forced to answerer) */
    v90m_ec_cfg v42;
    /* 0 = raise two timers whose defaults are too short for slow lines to
     * suit the line rates once they are known (they are never lowered):
     *   LAPM T401 to one maximum frame at the transmit rate plus two at the
     *   receive rate plus 300 ms (1.66 s at 2400 bit/s),
     *   PPP Restart to 1000000 / transmit rate + 1000000 / receive rate
     *   + 1500 ms (8.2 s at 300 bit/s);
     * at V.90 rates both stay at their defaults;
     * 1 = use v42.lapm.t401_ms and ppp.restart_ms exactly as given */
    int      fixed_timers;
    /* PPP: an ordinary ppp_config_t. Its callbacks (ip_rx, phase_cb,
     * secret_cb, pap_cb) are called with ppp.ctx as usual. */
    ppp_config_t ppp;
    /* Not NULL: the call carries octets and no PPP (two game consoles
     * joined modem to modem, srv/saturn.h). No error control is offered;
     * what the caller's modem sends is handed to raw_rx (with ppp.ctx) as it
     * is, and dce_raw_write() sends to it. PPP stays down. */
    void   (*raw_rx)(void *ctx, const uint8_t *oct, size_t n);
    /* with raw_rx: 1 = error control and compression stay as v42 says (a
     * terminal program calling a bulletin board, srv/bbs.h); 0 = none */
    int      raw_ec;
    /* with raw_rx: 1 = a caller whose first octets begin a PPP frame (7E FF
     * 03, or 7E FF 7D 23 as Dial-Up Networking sends its first LCP) is
     * taken to PPP at once, those octets included, as dce_start_ppp() does;
     * any other start goes to raw_rx as it is. Nothing is dropped or sent
     * back while it decides: at most four octets wait, and a lone 7E no
     * longer than DCE_DETECT_MS. After a terminal start the call is still
     * watched (dce_ppp_watch): a PPP program that sent something else
     * first (chat's CR after CONNECT) goes to PPP with its first frame. */
    int      ppp_detect;
    /* bench: the box calls (the modem on the port was told ATA): the
     * modem in the calling role (V.34, V.32bis, V.22bis, V.22, V.21, V.23 so far) and V.42
     * as the originator */
    int      originate;
    /* closing sequence limits, ms; 0 = the defaults in brackets */
    uint32_t flush_ms;         /* wait for PPP's last frames to go out [5000] */
    uint32_t release_ms;       /* wait for the answer to DISC [3000] */
} dce_cfg_t;

typedef struct {
    dce_state_t state;
    dce_end_t   end_reason;
    uint32_t    call_ms;       /* since dce_init */
    uint32_t    online_ms;     /* time spent in DCE_ONLINE */

    /* modem */
    int modem_connected;       /* carrier up, data mode */
    int used_v8;
    int modulation;            /* V90M_CONN_* (kept after the call ends; V.92 and V.90 can
                                  follow each other across a retrain) */
    /* line rates, bit/s (kept after the call ends). They differ on a V.90
     * call (for example 50666 and 28800) and are equal otherwise. */
    int snr_db10;              /* receiver's signal to error ratio, 0.1 dB; -1 = not known */
    int rtd;                   /* round-trip delay from the handshake, samples; -1 = not known */
    int bitrate;               /* transmit: towards the caller, "downstream" */
    int rx_bitrate;            /* receive: from the caller, "upstream" */
    uint32_t connect_ms;       /* call_ms when the modem connected (0 = it never did) */

    /* V.42 */
    int v42_state;             /* V90M_EC_* */
    int v42_reason;            /* V90M_EC_R_* once disconnected */
    int error_control;         /* 1 = LAPM connection up, 0 = none / start-stop */
    int comp_tx, comp_rx;      /* compression in force towards / from the caller */
    int comp_alg;              /* which: V90M_EC_COMP_V42BIS or V90M_EC_COMP_V44; 0 = none */
    unsigned long lapm_tx_i, lapm_rx_i;      /* I frames */
    unsigned long lapm_retx;                 /* I frames sent again */
    unsigned long lapm_rx_bad;               /* frames discarded by the HDLC receiver */
    unsigned long lapm_t401_expiries;        /* acknowledgement timer ran out */
    unsigned long async_overruns, async_framing;

    /* PPP */
    ppp_phase_t ppp_phase;
    ppp_end_t   ppp_end;
    int         authenticated;
    int         ip_up;
    char        peer_name[PPP_NAME_MAX + 1];
    uint32_t    our_ip;
    uint32_t    peer_ip;       /* as assigned; kept after the call ends */
    unsigned    ppp_comp_tx, ppp_comp_rx;  /* PPP compression (CCP) in force now, towards /
                                              from the caller: PPP_COMP_NONE, _DEFLATE, _MPPC, _MPPE
                                              (ppp_counters keeps the figures after the call) */
    ppp_counters_t ppp_counters;

    /* data path counters */
    uint64_t line_bits_tx, line_bits_rx;       /* data-mode bits to / from the modem */
    unsigned long octets_down, octets_up;      /* PPP octets handed to / taken from V.42 */
    int raw;                   /* a raw_rx call still in its octet mode (not PPP yet) */
} dce_status_t;

#define DCE_RAW_QUEUE 2048
#define DCE_DETECT_MS 2000

/* The call object. Contents are private; it is defined here so the caller
 * can allocate it. */
typedef struct dce {
    dce_cfg_t   cfg;
    dce_state_t state;
    dce_end_t   end_reason;
    v90m_t     *modem;         /* in modem_mem */
    v90m_ec_t  *v42;           /* in v42_mem */
    ppp_t       ppp;
    int         v42_on;        /* v42ec_init done */
    int         moving;        /* the modem carried data at the last sample */
    int         ppp_up;        /* ppp_lower_up done, ppp_lower_down not yet */
    int         ppp_dead;      /* PPP reached Dead by itself */
    int         hangup_req;
    int         sub;           /* samples into the current millisecond */
    int         close_step;    /* 0 flush, 1 release, 2 linger */
    uint32_t    close_ms;      /* time in the current closing step */
    int         linger_bits;   /* line bits still to send in the linger step */
    uint32_t    call_ms, online_ms, connect_ms;
    int         modulation;
    int         bitrate, rx_bitrate;   /* transmit, receive */
    uint32_t    peer_ip;
    ppp_end_t   ppp_end;
    uint64_t    line_bits_tx, line_bits_rx;
    unsigned long octets_down, octets_up;
    uint8_t     raw_q[DCE_RAW_QUEUE];  /* raw_rx set: octets waiting for the line */
    size_t      raw_head, raw_n;
    int         raw;           /* online, octets to and from raw_rx (not PPP) */
    int         to_ppp;        /* dce_start_ppp asked: PPP once raw_q is out */
    uint8_t     det[4];        /* ppp_detect: the first octets, held while it decides */
    int         det_n, det_done;
    int         watch;         /* ppp_detect, after a terminal start: a frame's start still takes the call to PPP */
    uint8_t     wb[4];         /* the watch: what may be a frame's start so far */
    int         wn;
    uint32_t    det_ms;
    union { v90m_align a; uint8_t b[V90M_MEM_SIZE]; } modem_mem;
    union { v90m_align a; uint8_t b[V90M_EC_MEM_SIZE]; } v42_mem;
} dce_t;

/* Defaults: transmit level 3000, V.22bis and V.21 offered (V.90 is not: add
 * V90M_MOD_V90 to modulations when the owner uses dce_tick_pcm), mu-law, V.42 defaults for
 * an answerer (detection, fallback to start-stop, V.42 bis both ways),
 * ppp_config_default(). Fill in cfg->ppp (auth, users, addresses, ip_rx). */
void dce_cfg_default(dce_cfg_t *cfg);

/* Start a call: the line has just been answered. cfg is copied; what
 * cfg->ppp points to (strings, user table, callbacks) must stay valid. */
void dce_init(dce_t *d, const dce_cfg_t *cfg);

/* One 8 kHz sample at the G.711 interface, the one to use with a codec:
 * rx is the codeword received from the line, *tx receives the codeword to
 * transmit, both in the law of cfg.alaw. Returns 1 while the call is alive,
 * 0 once it has ended (then *tx is the codeword for silence and nothing
 * else happens). A V.90 call needs this interface and an octet-transparent
 * path to the network: the caller's modem decodes the exact codewords. */
int dce_tick_pcm(dce_t *d, uint8_t rx, uint8_t *tx);

/* The G.711 interface of a codec whose receive side is 16-bit linear (the
 * box's SLIC in linear mode): rx is the received sample on the 16-bit scale
 * of the G.711 tables (v90_octet_linear), *tx the codeword to transmit as in
 * dce_tick_pcm. Everything works through it, V.90 and V.92 included; set
 * cfg.rx_linear so that the V.92 design leaves the G.711 quantiser out. */
int dce_tick_lin(dce_t *d, int16_t rx, uint8_t *tx);

/* The same with linear samples, for everything but V.90: rx is the sample
 * received from the line, *tx receives the sample to transmit (0 once the
 * call has ended). Use one of the two tick functions for a call, not both.
 * Do not enable V90M_MOD_V90 with this one. */
int dce_tick(dce_t *d, int16_t rx, int16_t *tx);

/* Queue an IP packet for the caller. Returns 0, or PPP_ERR_DOWN (IP is not
 * up), PPP_ERR_SIZE (larger than the peer's MRU), PPP_ERR_FULL (no room
 * right now: keep the packet and try again later). */
int dce_send_ip(dce_t *d, const uint8_t *pkt, size_t len);

/* A call with raw_rx, once it is DCE_ONLINE: octets for the caller's modem.
 * Returns how many were taken (0..n): the rest later. 0 once the call has
 * gone over to PPP (or been asked to). */
size_t dce_raw_write(dce_t *d, const uint8_t *oct, size_t n);
/* A call with raw_rx, online in its octet mode: go over to PPP on the same
 * connection (V.42 and its compression as they are), with cfg.ppp as
 * configured, as if the call had been a PPP call from the start. What
 * dce_raw_write queued still goes out first; from then on nothing goes to
 * raw_rx, and dce_send_ip and the PPP callbacks work as for any call.
 * Returns 0, or -1 when the call is not in its octet mode. */
int dce_start_ppp(dce_t *d);
/* ppp_detect: whether a PPP frame's start (7E FF 03, 7E FF 7D 23) from the
 * caller still takes an octet-mode call to PPP after it began as a
 * terminal's (on from the start). The octets go to raw_rx as well, at
 * once. Off while the caller's octets are for something else whose data
 * may hold those octets (a file sent to a board), on again after. */
void dce_ppp_watch(dce_t *d, int on);
/* End the call from our side, in order: PPP Terminate, LAPM release, then
 * DCE_ENDED with DCE_END_LOCAL. Takes effect in the following ticks. */
void dce_hangup(dce_t *d);

void dce_get_status(const dce_t *d, dce_status_t *st);

/* short English text for a dce_end_t, for logs */
const char *dce_end_name(dce_end_t why);

#endif
