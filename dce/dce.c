/* One answered call: modem + V.42 + PPP pumped from one sample clock.
 * See dce.h. */
#include <string.h>
#include "dce.h"

#define SAMPLES_PER_MS   8
/* PPP octets kept queued inside V.42 ahead of the line. Small, so that PPP
 * control packets are not stuck behind seconds of data, but more than one
 * I frame so the line never runs dry. */
#define V42_TX_TARGET    512
#define FLUSH_MS         5000
#define RELEASE_MS       3000
/* Line bits still sent once V.42 has nothing more to say, so that the last
 * frame makes it through the modem's transmitter and the far receiver
 * before the carrier stops: LINGER_BITS of them, or 50 ms worth at the
 * transmit rate where that is more (the delay through a modem is a matter
 * of time, not of bits: at V.90 speed 128 bits are 2.5 ms, which is just
 * about the transmit framing and no margin). And a time limit for that step. */
#define LINGER_BITS      128
#define LINGER_PER_S     20         /* 1/20 s */
#define LINGER_MS        6000
#define CLEARDOWN_MS     600     /* for a V.90 cleardown to go out (it takes about 130 ms) */
#define ODP_BYPASS_MS    5000    /* T400 when the caller bypasses ODP / ADP (V.92 9.3.1) */

static int linger_bits(const dce_t *d)
{
    int n = d->bitrate / LINGER_PER_S;
    return n > LINGER_BITS ? n : LINGER_BITS;
}

/* ------------------------------------------------- modem bit callbacks
 * getbit runs at the transmit rate, putbit at the receive rate; on a V.90
 * call these are far apart (see dce_status_t). */
#ifdef P34D_TRACE
void dce_trace_rx(int bit);
void dce_trace_tx(int bit);
#endif

static int line_getbit(void *user)
{
    dce_t *d = user;
    if (!d->v42_on) return 1;
    int bit;
    d->line_bits_tx++;
    bit = v90m_ec_tx_bit(d->v42);
#ifdef P34D_TRACE
    dce_trace_tx(bit);
#endif
    if (d->linger_bits > 0) {
        /* the countdown only runs once the last frame (the UA or DM that
         * answers a release) has left the HDLC transmitter */
        if (v90m_ec_tx_busy(d->v42)) d->linger_bits = linger_bits(d);
        else d->linger_bits--;
    }
    return bit;
}

static void line_putbit(void *user, int bit)
{
    dce_t *d = user;
    if (!d->v42_on) return;
    d->line_bits_rx++;
#ifdef P34D_TRACE
    dce_trace_rx(bit);
#endif
    v90m_ec_rx_bit(d->v42, bit);
}

/* ------------------------------------------------ PPP callback wrappers */
static void on_ip(void *ctx, const uint8_t *pkt, size_t len)
{
    dce_t *d = ctx;
    if (d->cfg.ppp.ip_rx) d->cfg.ppp.ip_rx(d->cfg.ppp.ctx, pkt, len);
}

static void on_phase(void *ctx, ppp_phase_t ph)
{
    dce_t *d = ctx;
    /* Dead while we still hold the lower layer up: PPP ended the session
     * itself. Acted upon in ms_step(), not from inside PPP. */
    if (ph == PPP_PHASE_DEAD && d->ppp_up) d->ppp_dead = 1;
    if (d->cfg.ppp.phase_cb) d->cfg.ppp.phase_cb(d->cfg.ppp.ctx, ph);
}

static int on_secret(void *ctx, const uint8_t *user, size_t ulen, uint8_t *secret, size_t max)
{
    dce_t *d = ctx;
    return d->cfg.ppp.secret_cb(d->cfg.ppp.ctx, user, ulen, secret, max);
}

static int on_pap(void *ctx, const uint8_t *user, size_t ulen, const uint8_t *pass, size_t plen)
{
    dce_t *d = ctx;
    return d->cfg.ppp.pap_cb(d->cfg.ppp.ctx, user, ulen, pass, plen);
}

/* ---------------------------------------------------------------- setup */
/* (Re)initialise PPP; the line rates are used once known (d->bitrate > 0).
 * PPP's Restart timer (3 s unless configured) has to cover a request and
 * its answer on the line. The automaton gives every retransmitted
 * Configure-Request a new identifier, so with a timer shorter than the
 * round trip the answers always come too late and LCP never converges:
 * that is the case at 300 bit/s. About 100 octets each way, each at the
 * rate of its direction, plus slack. */
static void ppp_setup(dce_t *d)
{
    ppp_config_t pc = d->cfg.ppp;

    pc.ctx = d;
    pc.ip_rx = on_ip;
    pc.phase_cb = on_phase;
    pc.secret_cb = d->cfg.ppp.secret_cb ? on_secret : NULL;
    pc.pap_cb = d->cfg.ppp.pap_cb ? on_pap : NULL;
    if (!d->cfg.fixed_timers && d->bitrate > 0 && d->rx_bitrate > 0) {
        uint32_t rt = 1000000u / (uint32_t)d->bitrate + 1000000u / (uint32_t)d->rx_bitrate + 1500u;
        if (pc.restart_ms == 0) pc.restart_ms = 3000;
        if (pc.restart_ms < rt) pc.restart_ms = rt;
        /* a CCP Reset-Request and its answer each wait for the frame that
         * is going out, at worst a full one of 1500 octets (10 bits each
         * without error control) */
        if (pc.comp_reset_ms == 0)
            pc.comp_reset_ms = pc.restart_ms + 15000000u / (uint32_t)d->bitrate + 15000000u / (uint32_t)d->rx_bitrate;
    }
    ppp_init(&d->ppp, &pc);
}

void dce_cfg_default(dce_cfg_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->tx_level = 3000;
    cfg->modulations = V90M_MOD_V22 | V90M_MOD_V21;
    v90m_ec_cfg_default(&cfg->v42, 0);
    ppp_config_default(&cfg->ppp);
}

void dce_init(dce_t *d, const dce_cfg_t *cfg)
{
    v90m_cfg mc;

    memset(d, 0, sizeof *d);
    d->cfg = *cfg;
    d->cfg.v42.originator = cfg->originate != 0;     /* V.42: the calling DCE is the originator */
    d->cfg.alaw = cfg->alaw != 0;
    if (!d->cfg.flush_ms) d->cfg.flush_ms = FLUSH_MS;
    if (!d->cfg.release_ms) d->cfg.release_ms = RELEASE_MS;
    d->state = DCE_TRAINING;

    v90m_cfg_default(&mc);
    if (cfg->originate) mc.role = V90M_ROLE_ORIGINATE;
    mc.tx_level = cfg->tx_level;
    mc.modulations = cfg->modulations;
    mc.alaw = d->cfg.alaw;
    mc.codec_linear_rx = cfg->rx_linear != 0;
    mc.max_tx_code = cfg->max_tx_code;
    mc.max_down_bps = cfg->max_down;
    mc.max_up_bps = cfg->max_up;
    mc.echo_delay = cfg->echo_delay;
    mc.getbit = line_getbit;
    mc.putbit = line_putbit;
    mc.user = d;
    d->modem = v90m_init(d->modem_mem.b, sizeof d->modem_mem.b, &mc, NULL);
    d->v42 = v90m_ec_init(d->v42_mem.b, sizeof d->v42_mem.b, &d->cfg.v42, NULL);
    if (!d->modem || !d->v42) {
        d->state = DCE_ENDED;           /* a configuration the modem refuses */
        d->end_reason = DCE_END_NO_CARRIER;
        return;
    }

    ppp_setup(d);
}

/* --------------------------------------------------------- the data pump */
/* PPP comes up on the error-controlled (or start-stop) link */
static void ppp_begin(dce_t *d)
{
    d->ppp_dead = 0;
    d->ppp_up = 1;
    /* Without error control a damaged frame is a lost one, and a lost
     * compressed packet takes those queued behind it along (ppp.h). Not
     * more than two seconds of line in that queue, and room for a packet or
     * two at the slow rates. */
    if (v90m_ec_state(d->v42) == V90M_EC_ASYNC) {
        size_t q = (size_t)(d->bitrate > 0 ? d->bitrate : 0) / 5;
        ppp_set_comp_queue(&d->ppp, q < 600 ? 600 : q);
    }
    ppp_lower_up(&d->ppp);
}

/* an octet-mode call goes over to PPP (dce_start_ppp, ppp_detect) */
static void go_ppp(dce_t *d)
{
    d->raw = 0;
    d->to_ppp = 0;
    d->det_done = 1;
    d->raw_head = d->raw_n = 0;
    ppp_begin(d);
}

/* how the octets held so far compare with the start of a PPP frame:
 * 2 = they are one, 1 = they may become one, 0 = they are not */
static int det_match(const uint8_t *b, int n)
{
    static const uint8_t a[3] = { 0x7e, 0xff, 0x03 }, e[4] = { 0x7e, 0xff, 0x7d, 0x23 };
    int pa = n <= 3 && memcmp(b, a, (size_t)n) == 0, pe = n <= 4 && memcmp(b, e, (size_t)n) == 0;
    if ((pa && n == 3) || (pe && n == 4)) return 2;
    return pa || pe;
}

/* octets from the caller of an octet-mode call */
static void raw_in(dce_t *d, const uint8_t *b, size_t n)
{
    size_t i;
    if (d->to_ppp) { ppp_input_bytes(&d->ppp, b, n); return; }   /* PPP is asked for: the caller's are its */
    if (d->det_done) {
        for (i = 0; d->watch && i < n; i++) {
            if (b[i] == 0x7e) d->wn = 0;
            else if (!d->wn) continue;
            d->wb[d->wn++] = b[i];
            switch (det_match(d->wb, d->wn)) {
            case 0: d->wn = 0; break;
            case 2:                         /* a PPP program after all: from its frame on */
                d->cfg.raw_rx(d->cfg.ppp.ctx, b, i + 1);
                go_ppp(d);
                ppp_input_bytes(&d->ppp, d->wb, (size_t)d->wn);
                if (i + 1 < n) ppp_input_bytes(&d->ppp, b + i + 1, n - i - 1);
                return;
            }
        }
        d->cfg.raw_rx(d->cfg.ppp.ctx, b, n);
        return;
    }
    for (i = 0; i < n; i++) {
        int m;
        d->det[d->det_n++] = b[i];
        m = det_match(d->det, d->det_n);
        if (m == 1) continue;
        d->det_done = 1;
        if (m == 2) {                       /* Dial-Up Networking: its first frame goes to PPP whole */
            go_ppp(d);
            ppp_input_bytes(&d->ppp, d->det, (size_t)d->det_n);
            if (i + 1 < n) ppp_input_bytes(&d->ppp, b + i + 1, n - i - 1);
        } else {                            /* someone at a terminal: all of it, as it came */
            d->cfg.raw_rx(d->cfg.ppp.ctx, d->det, (size_t)d->det_n);
            if (i + 1 < n) raw_in(d, b + i + 1, n - i - 1);     /* watched (d->watch) */
        }
        return;
    }
}

static void pump_up(dce_t *d)
{
    uint8_t buf[256];
    size_t n;
    int k;
    /* at most 1 KiB a millisecond (a line with V.42bis brings some 30): a
     * backlog, after a retransmission or an expanding burst, is spread over
     * the next milliseconds instead of all going through PPP in one */
    for (k = 0; k < 4 && (n = v90m_ec_read(d->v42, buf, sizeof buf)) > 0; k++) {
        d->octets_up += n;
        if (d->raw) raw_in(d, buf, n);
        else ppp_input_bytes(&d->ppp, buf, n);
    }
}

static void pump_down(dce_t *d)
{
    uint8_t buf[256];
    v90m_ec_status vs;
    size_t room, n;

    for (;;) {
        /* tx_queued is all we need; the status call is cheap enough per ms */
        v90m_ec_get_status(d->v42, &vs);
        if (vs.tx_queued >= V42_TX_TARGET) break;
        room = V42_TX_TARGET - vs.tx_queued;
        n = v90m_ec_write_space(d->v42);
        if (room > n) room = n;
        if (room > sizeof buf) room = sizeof buf;
        if (room == 0) break;
        if (d->raw) {
            for (n = 0; n < room && d->raw_n; n++) {
                buf[n] = d->raw_q[d->raw_head];
                d->raw_head = (d->raw_head + 1) % DCE_RAW_QUEUE; d->raw_n--;
            }
        } else {
            n = ppp_output_bytes(&d->ppp, buf, room);
        }
        if (n == 0) break;
        v90m_ec_write(d->v42, buf, n);       /* takes all of it: room was checked */
        d->octets_down += n;
    }
}

static void ppp_snapshot_ip(dce_t *d)
{
    ppp_status_t ps;
    ppp_get_status(&d->ppp, &ps);
    if (ps.ip_up) d->peer_ip = ps.peer_ip;
}

/* --------------------------------------------------------- ending a call */
static void set_reason(dce_t *d, dce_end_t why)
{
    if (d->end_reason == DCE_END_NONE) d->end_reason = why;
}

/* remember what PPP knew before it is taken down */
static void ppp_snapshot(dce_t *d)
{
    ppp_status_t ps;
    ppp_get_status(&d->ppp, &ps);
    if (ps.ip_up) d->peer_ip = ps.peer_ip;
    if (d->ppp_end == PPP_END_NONE) d->ppp_end = ps.end_reason;
}

static void ppp_down(dce_t *d)
{
    if (!d->ppp_up) return;
    ppp_snapshot(d);
    d->ppp_up = 0;                  /* first: on_phase must not take this for PPP's own end */
    ppp_lower_down(&d->ppp);
    if (d->ppp_end == PPP_END_NONE) d->ppp_end = PPP_END_LOWER_DOWN;
}

/* Stop the V.42 endpoint without a word to the far end. v42ec has no call
 * for "the line is gone", so an established or establishing connection is
 * aborted at the LAPM level and the endpoint left to notice. */
static void v42_down(dce_t *d)
{
    if (!d->v42_on) return;
    switch (v90m_ec_state(d->v42)) {
    case V90M_EC_DISCONNECTED:
        break;
    case V90M_EC_DETECT:
    case V90M_EC_ASYNC:
        v90m_ec_release(d->v42);
        break;
    default:
        v90m_ec_abort(d->v42);
        v90m_ec_tick(d->v42, 0);
        break;
    }
}

/* everything down, now */
static void end_now(dce_t *d, dce_end_t why)
{
    set_reason(d, why);
    ppp_down(d);
    v42_down(d);
    d->state = DCE_ENDED;
}

/* orderly ending with the carrier still up; step 0 flush, 1 release, 2 linger */
static void begin_close(dce_t *d, dce_end_t why, int step)
{
    set_reason(d, why);
    d->state = DCE_CLOSING;
    d->close_step = step;
    d->close_ms = 0;
    if (step == 2) d->linger_bits = linger_bits(d);
}

static dce_end_t reason_from_ppp(ppp_end_t e)
{
    switch (e) {
    case PPP_END_AUTH_REFUSED:
    case PPP_END_AUTH_FAILED:
    case PPP_END_AUTH_TIMEOUT:    return DCE_END_AUTH_FAILED;
    case PPP_END_PEER_TERMINATED: return DCE_END_PEER_CLOSED;
    case PPP_END_LOCAL_CLOSE:     return DCE_END_LOCAL;
    default:                      return DCE_END_PPP_FAILED;
    }
}

static dce_end_t reason_from_v42(int r)
{
    return r == V90M_EC_R_PEER_DISC || r == V90M_EC_R_PEER_DM ? DCE_END_V42_RELEASED
                                                              : DCE_END_V42_FAILED;
}

/* V.42 went to Disconnected under us */
static void v42_lost(dce_t *d)
{
    v90m_ec_status vs;
    v90m_ec_get_status(d->v42, &vs);
    set_reason(d, reason_from_v42(vs.reason));
    ppp_down(d);
    begin_close(d, d->end_reason, 2);       /* let the final UA or DM out */
}

static void close_step(dce_t *d)
{
    d->close_ms++;
    switch (d->close_step) {
    case 0:     /* PPP is finished: get its last frames to the far end */
        pump_up(d);
        pump_down(d);
        if (v90m_ec_state(d->v42) == V90M_EC_DISCONNECTED) { begin_close(d, d->end_reason, 2); break; }
        if ((ppp_output_pending(&d->ppp) == 0 && v90m_ec_tx_idle(d->v42))
            || d->close_ms >= d->cfg.flush_ms) {
            v90m_ec_release(d->v42);
            begin_close(d, d->end_reason, 1);
        }
        break;
    case 1:     /* DISC sent: wait for the answer */
        pump_up(d);
        if (v90m_ec_state(d->v42) == V90M_EC_DISCONNECTED || d->close_ms >= d->cfg.release_ms)
            begin_close(d, d->end_reason, 2);
        break;
    case 2:     /* a few more line bits */
        if (d->linger_bits <= 0 || d->close_ms >= LINGER_MS) {
            /* then tell the calling modem, if the modulation can (V.90 9.7) */
            if (v90m_cleardown(d->modem) == V90M_OK) begin_close(d, d->end_reason, 3);
            else end_now(d, d->end_reason);
        }
        break;
    default:    /* the cleardown is on its way: the modem stops by itself */
        if (d->close_ms >= CLEARDOWN_MS) end_now(d, d->end_reason);
        break;
    }
}

/* T401 as shipped (1 s) is shorter than the time an acknowledgement needs
 * on a slow line: the frame itself (at our transmit rate), then the frame
 * the far end is in the middle of and the frame that carries the
 * acknowledgement back (both at our receive rate). */
static void v42_start(dce_t *d, int odp_bypass)
{
    v90m_ec_cfg vc = d->cfg.v42;
    long frame_bits = ((long)vc.lapm.n401_tx + 8) * 8;
    long txr = d->bitrate > 0 ? d->bitrate : 300, rxr = d->rx_bitrate > 0 ? d->rx_bitrate : 300;
    long t401 = frame_bits * 1000 / txr + 2 * frame_bits * 1000 / rxr + 300;

    vc.originator = d->cfg.originate != 0;
    if (!d->cfg.fixed_timers && vc.lapm.t401_ms < t401) vc.lapm.t401_ms = (int)t401;
    if (d->cfg.raw_rx && !d->cfg.raw_ec) {
        /* octets as they are: straight to start-stop, as below */
        vc.detect = 1; vc.fallback = 1; vc.t400_ms = 1;
        vc.lapm.v42bis_tx = 0; vc.lapm.v42bis_rx = 0; vc.lapm.v44_tx = 0; vc.lapm.v44_rx = 0;
    } else if (d->modulation == V90M_CONN_V21 || d->modulation == V90M_CONN_BELL103
               || d->modulation == V90M_CONN_V23) {
        /* No error control on FSK (V.21, Bell 103, V.23): the modem delivers characters,
         * not the bit-transparent stream LAPM needs. v42ec has no "start in
         * start-stop mode" switch; the shortest detection phase does it. */
        vc.detect = 1; vc.fallback = 1; vc.t400_ms = 1;
    } else if (odp_bypass && vc.detect && vc.t400_ms < ODP_BYPASS_MS) {
        /* V.92 9.3.1: the caller sends no ODP and goes straight to its XID
         * or SABME. The detection phase here takes flags or a frame without
         * an ODP as the start of the protocol (and never sends an ADP that
         * was not asked for), so it stays; but its timer must not send a
         * caller that is committed to LAPM into start-stop mode. */
        vc.t400_ms = ODP_BYPASS_MS;
    }
    v90m_ec_reset(d->v42, &vc);
    d->v42_on = 1;
    ppp_setup(d);
}

static void ms_step(dce_t *d, int retraining)
{
    int vs;

    d->call_ms++;
    if (retraining) {
        /* The modem is retraining: no data moves for some seconds. Error
         * control and PPP are held as they are, timers included, the way a
         * modem holds its error control over a retrain. */
        if (d->hangup_req) end_now(d, DCE_END_LOCAL);
        return;
    }
    if (d->v42_on) v90m_ec_tick(d->v42, 1);
    ppp_tick(&d->ppp, 1);

    switch (d->state) {
    case DCE_TRAINING:
        if (d->hangup_req) end_now(d, DCE_END_LOCAL);
        break;
    case DCE_NEGOTIATING:
        vs = v90m_ec_state(d->v42);
        if (d->hangup_req) {
            v90m_ec_release(d->v42);
            begin_close(d, DCE_END_LOCAL, 1);
        } else if (d->cfg.raw_rx && (vs == V90M_EC_CONNECTED || vs == V90M_EC_ASYNC)) {
            d->raw_head = 0; d->raw_n = 0;
            d->raw = 1;
            d->det_n = 0; d->det_ms = 0; d->det_done = !d->cfg.ppp_detect;
            d->watch = d->cfg.ppp_detect; d->wn = 0;
            d->state = DCE_ONLINE;
        } else if (vs == V90M_EC_CONNECTED || vs == V90M_EC_ASYNC) {
            ppp_begin(d);
            d->state = DCE_ONLINE;
        } else if (vs == V90M_EC_DISCONNECTED) {
            v42_lost(d);
        }
        break;
    case DCE_ONLINE:
        d->online_ms++;
        pump_up(d);
        if (d->raw) {
            /* a lone 7E (a terminal user's "~") is not held for ever */
            if (!d->det_done && d->det_n > 0 && ++d->det_ms >= DCE_DETECT_MS) {
                d->det_done = 1;
                d->cfg.raw_rx(d->cfg.ppp.ctx, d->det, (size_t)d->det_n);
            }
            pump_down(d);
            if (v90m_ec_state(d->v42) == V90M_EC_DISCONNECTED) v42_lost(d);
            else if (d->hangup_req) { v90m_ec_release(d->v42); begin_close(d, DCE_END_LOCAL, 1); }
            else if (d->to_ppp && d->raw_n == 0) go_ppp(d);    /* the text is out: PPP from here */
            break;
        }
        if (d->hangup_req == 1) {           /* ask PPP to terminate, once */
            d->hangup_req = 2;
            ppp_close(&d->ppp);
        }
        pump_down(d);
        if ((d->call_ms & 15) == 0 && d->peer_ip == 0) ppp_snapshot_ip(d);
        if (v90m_ec_state(d->v42) == V90M_EC_DISCONNECTED) {
            v42_lost(d);
        } else if (d->ppp_dead) {
            ppp_snapshot(d);
            begin_close(d, d->hangup_req ? DCE_END_LOCAL : reason_from_ppp(d->ppp_end), 0);
        }
        break;
    case DCE_CLOSING:
        close_step(d);
        break;
    default:
        break;
    }
}

/* the modem's modulation is the one in force (during a retrain it still
 * names the one before) */
static int moving_mod(const dce_t *d, const v90m_state *ms) { return d->moving && ms->modulation != V90M_CONN_NONE; }

/* after every modem sample; returns 1 while the call is alive */
static int after_sample(dce_t *d)
{
    v90m_state ms;
    v90m_get_state(d->modem, &ms);
    if (ms.end != V90M_END_NONE) {
        end_now(d, ms.end == V90M_END_X2_FAILED ? DCE_END_X2_FAILED
                 : ms.end == V90M_END_K56_FAILED ? DCE_END_K56_FAILED
                 : ms.end == V90M_END_RETRAIN_FAILED ? DCE_END_RETRAIN_FAILED
                 : d->state == DCE_TRAINING ? DCE_END_NO_CARRIER
                 : ms.end == V90M_END_CLEARDOWN_FAR ? DCE_END_CLEARDOWN : DCE_END_CARRIER_LOST);
        return 0;
    }
    {
        /* data stops for a retrain or a rate renegotiation and comes back:
         * tell error control at both edges so it holds the frame the stop
         * cut and reopens with whole flags, then LAPM asks at once where the
         * caller stands */
        /* (V.90 and V.92: both directions out of a rate renegotiation,
         * which leaves the modem "connected" and not "retraining") */
        if (ms.moving && !d->moving && d->v42_on) v90m_ec_line_restored(d->v42);
        else if (!ms.moving && d->moving && d->v42_on) v90m_ec_line_stopped(d->v42);
        d->moving = ms.moving;
    }
    if (d->state != DCE_TRAINING && ms.connected
        && (ms.tx_bps != d->bitrate || (moving_mod(d, &ms) && (int)ms.modulation != d->modulation)
            || (moving_mod(d, &ms) && ms.modulation == V90M_CONN_V92 && ms.rx_bps != d->rx_bitrate))) {
        /* a rate renegotiation (with V.92 it may change the upstream rate
         * alone: CPd carries both, V.92 9.8); or a retrain after which the
         * caller chose the other upstream (V.92 PCM or V.90's V.34, 9.3) */
        if (moving_mod(d, &ms)) d->modulation = (int)ms.modulation;
        d->bitrate = ms.tx_bps;
        d->rx_bitrate = ms.rx_bps;
    }
    if (d->state == DCE_TRAINING && ms.connected) {
        d->modulation = (int)ms.modulation;
        d->bitrate = ms.tx_bps;
        d->rx_bitrate = ms.rx_bps;
        d->connect_ms = d->call_ms;
        v42_start(d, ms.odp_bypass);
        d->state = DCE_NEGOTIATING;
    }
    if (++d->sub == SAMPLES_PER_MS) {
        d->sub = 0;
        ms_step(d, ms.retraining);
        if (d->state == DCE_ENDED) return 0;
    }
    return 1;
}

int dce_tick_pcm(dce_t *d, uint8_t rx, uint8_t *tx)
{
    if (d->state != DCE_ENDED) {
        v90m_process_g711(d->modem, &rx, tx, 1);
        if (after_sample(d)) return 1;
    }
    *tx = v90m_g711_encode(d->cfg.alaw, 0);
    return 0;
}

int dce_tick_lin(dce_t *d, int16_t rx, uint8_t *tx)
{
    if (d->state != DCE_ENDED) {
        v90m_process_lin_rx(d->modem, &rx, tx, 1);
        if (after_sample(d)) return 1;
    }
    *tx = v90m_g711_encode(d->cfg.alaw, 0);
    return 0;
}

int dce_tick(dce_t *d, int16_t rx, int16_t *tx)
{
    if (d->state != DCE_ENDED) {
        v90m_process_linear(d->modem, &rx, tx, 1);
        if (after_sample(d)) return 1;
    }
    *tx = 0;
    return 0;
}

int dce_send_ip(dce_t *d, const uint8_t *pkt, size_t len)
{
    if (d->state != DCE_ONLINE) return PPP_ERR_DOWN;
    return ppp_send_ip(&d->ppp, pkt, len);
}

size_t dce_raw_write(dce_t *d, const uint8_t *oct, size_t n)
{
    size_t i;
    if (d->state != DCE_ONLINE || !d->raw || d->to_ppp) return 0;
    for (i = 0; i < n && d->raw_n < DCE_RAW_QUEUE; i++) {
        d->raw_q[(d->raw_head + d->raw_n) % DCE_RAW_QUEUE] = oct[i];
        d->raw_n++;
    }
    return i;
}

int dce_start_ppp(dce_t *d)
{
    if (d->state != DCE_ONLINE || !d->raw) return -1;
    if (!d->det_done && d->det_n > 0)          /* typed before the switch: still the owner's */
        d->cfg.raw_rx(d->cfg.ppp.ctx, d->det, (size_t)d->det_n);
    d->det_done = 1;
    d->to_ppp = 1;
    if (d->raw_n == 0) go_ppp(d);             /* nothing of the text left to send */
    return 0;
}

void dce_hangup(dce_t *d) { if (!d->hangup_req) d->hangup_req = 1; }

void dce_ppp_watch(dce_t *d, int on) { d->watch = on && d->cfg.ppp_detect; d->wn = 0; }

void dce_get_status(const dce_t *d, dce_status_t *st)
{
    ppp_status_t ps;
    v90m_ec_status vs;

    memset(st, 0, sizeof *st);
    st->state = d->state;
    st->end_reason = d->end_reason;
    st->call_ms = d->call_ms;
    st->online_ms = d->online_ms;
    st->raw = d->raw;

    if (d->modem) {
        v90m_state ms;
        v90m_stats mx;
        v90m_get_state(d->modem, &ms);
        v90m_get_stats(d->modem, &mx);
        st->modem_connected = d->state != DCE_ENDED && ms.connected;
        st->used_v8 = ms.used_v8;
        st->snr_db10 = st->modem_connected ? mx.snr_db10 : -1;
        st->rtd = st->modem_connected ? mx.rtd : -1;
    } else {
        st->snr_db10 = st->rtd = -1;
    }
    st->modulation = d->modulation;
    st->bitrate = d->bitrate;
    st->rx_bitrate = d->rx_bitrate;
    st->connect_ms = d->connect_ms;

    if (d->v42_on) {
        v90m_ec_get_status(d->v42, &vs);
        st->v42_state = vs.state;
        st->v42_reason = vs.reason;
        st->error_control = vs.state == V90M_EC_CONNECTED || vs.state == V90M_EC_RELEASING;
        st->comp_tx = st->error_control && vs.enc_on;
        st->comp_rx = st->error_control && vs.dec_on;
        st->comp_alg = st->comp_tx || st->comp_rx ? vs.alg_on : V90M_EC_COMP_NONE;
        st->lapm_tx_i = vs.tx_i; st->lapm_rx_i = vs.rx_i;
        st->lapm_retx = vs.tx_i_retx;
        st->lapm_rx_bad = vs.rx_frames_bad;
        st->lapm_t401_expiries = vs.t401_expiries;
        st->async_overruns = vs.async_overruns;
        st->async_framing = vs.async_framing;
    } else {
        st->v42_state = d->state == DCE_ENDED ? V90M_EC_DISCONNECTED : V90M_EC_DETECT;
    }

    ppp_get_status(&d->ppp, &ps);
    st->ppp_phase = ps.phase;
    st->ppp_end = d->ppp_end != PPP_END_NONE ? d->ppp_end : ps.end_reason;
    st->authenticated = ps.authenticated;
    st->ip_up = ps.ip_up;
    st->ppp_comp_tx = ps.comp_tx; st->ppp_comp_rx = ps.comp_rx;
    memcpy(st->peer_name, ps.peer_name, sizeof st->peer_name);
    st->our_ip = ps.our_ip;
    st->peer_ip = ps.ip_up ? ps.peer_ip : d->peer_ip;
    st->ppp_counters = ps.counters;

    st->line_bits_tx = d->line_bits_tx; st->line_bits_rx = d->line_bits_rx;
    st->octets_down = d->octets_down; st->octets_up = d->octets_up;
}

const char *dce_end_name(dce_end_t why)
{
    switch (why) {
    case DCE_END_NONE:         return "none";
    case DCE_END_NO_CARRIER:   return "no carrier";
    case DCE_END_CARRIER_LOST: return "carrier lost";
    case DCE_END_V42_FAILED:   return "error control failed";
    case DCE_END_V42_RELEASED: return "released by the far end";
    case DCE_END_AUTH_FAILED:  return "authentication failed";
    case DCE_END_PEER_CLOSED:  return "closed by the peer";
    case DCE_END_PPP_FAILED:   return "PPP failed";
    case DCE_END_LOCAL:        return "local hangup";
    case DCE_END_CLEARDOWN:    return "caller cleared down";
    case DCE_END_X2_FAILED:    return "x2 failed";
    case DCE_END_RETRAIN_FAILED: return "retrain failed";
    case DCE_END_K56_FAILED:   return "k56flex failed";
    }
    return "?";
}
