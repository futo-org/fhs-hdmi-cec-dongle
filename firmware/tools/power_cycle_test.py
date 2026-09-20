#!/usr/bin/env python3
"""TV power cycle soak test over CEC via the board's shell.

Each cycle: `cec off`, settle, `cec tv status` until the TV says standby;
then `cec on`, settle, `cec tv status` until it says on. With --audio the
cycle continues while the TV is on: `cec vol up 2`, `cec vol down 2`,
`cec vol mute` twice, each checked against `cec vol status` (the soundbar's
<Report Audio Status>, exact level and mute flag). A half fails when the
device never reports the expected state within the retry budget.

    power_cycle_test.py [--port /dev/ttyACM0] [--cycles 100] [--audio] [--out test-results/x]
                        [--strict] [--variations] [--stop-on-fail]

--strict: the power command itself must go out exactly once and be acked
first time (no firmware retry, no shell rejection); only the status query may
be repeated. --variations rotates the audio block per cycle (step size,
order, or power only). --stop-on-fail ends the run at the first FAIL so the
cause can be fixed before a fresh run.

Writes <out>.csv (one row per cycle), <out>.raw.log (every serial byte) and
<out>.md (summary). One exclusive fd for the whole run, see README.
"""

import argparse
import re
import sys
import time
from datetime import datetime

import serial

SETTLE_S = 10.0        # TV NO ACKs mid transition, README says wait ~10 s
QUERY_WINDOW_S = 3.0   # how long to collect the answer to `cec tv status`
QUERY_RETRIES = 8      # extra queries if answer is transitional/missing
RETRY_GAP_S = 3.0
PWR_RETRY_MARK = "TV not ready (no ack), retry"      # firmware resent the power command
PWR_ACCEPTED_LATE = "TV accepted it on attempt"

# Audio block variations, rotated by cycle number with --variations.
# (label, step, order) order: "vol" = up/down then mute/unmute, "mute" = mute/unmute first
VARIATIONS = [
    ("power only", 0, None),
    ("up/down 1, mute", 1, "vol"),
    ("up/down 2, mute", 2, "vol"),
    ("mute, up/down 2", 2, "mute"),
    ("up/down 3, mute", 3, "vol"),
    ("mute, up/down 5", 5, "mute"),
]
CMD_WINDOW_S = 1.5
BUSY_RETRIES = 3       # old firmware said "busy" when the auto-responder held the tx slot
BUSY_GAP_S = 1.0
BUSY = "busy, or no own address"
REJECTED = "rejected:"          # new firmware: a frame is already parked / keys running
VOL_STEP = 2
VOL_KEYS_WINDOW_S = 3.0
AUDIO_WINDOW_S = 2.0
AUDIO_RETRIES = 3               # soundbar still waking, or no answer

POWER_RE = re.compile(r"Report Power Status: (on|standby|turning on|turning off)")
AUDIO_RE = re.compile(r"Report Audio Status: volume (\d+)(, muted)?")
RESULT = r"\]\s+(ok|reply ok|NO ACK[^\n]*|REJECTED[^\n]*|FAILED[^\n]*)"
TX_RE = {                      # our own frame per command, so auto-replies do not count
    "cec off": re.compile(r"TX \[40 36" + RESULT),
    "cec on": re.compile(r"TX \[40 04" + RESULT),
    "cec tv status": re.compile(r"TX \[40 8f" + RESULT),
}
KEY_TX_RE = re.compile(r"TX \[[0-9a-f]5 4[45]( 4[123])?" + RESULT)     # press/release to Audio(5)


class Board:
    def __init__(self, port, baud, rawlog):
        self.ser = serial.Serial(exclusive=True, timeout=0.1)
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.open()
        self.ser.reset_input_buffer()
        self.raw = rawlog
        time.sleep(3)              # DTR toggle on open, let VCP settle
        self.collect(1.0)

    def collect(self, seconds):
        end = time.time() + seconds
        buf = b""
        while time.time() < end:
            chunk = self.ser.read(4096)
            if chunk:
                buf += chunk
        text = buf.decode(errors="replace")
        if text:
            self.raw.write(text)
            self.raw.flush()
        return text

    def cmd(self, line, window):
        self.raw.write("\n### %s  %s\n" % (datetime.now().isoformat(timespec="milliseconds"), line))
        self.ser.write((line + "\r").encode())
        return self.collect(window)

    def cmd_retry_busy(self, line, window):
        """Resend when the shell rejects. Returns (output, reject_count)."""
        busy = 0
        for _ in range(BUSY_RETRIES + 1):
            out = self.cmd(line, window)
            if BUSY not in out and REJECTED not in out:
                return out, busy
            busy += 1
            time.sleep(BUSY_GAP_S)
        return out, busy


def query_until(board, want):
    """Ask power status until the TV answers `want`. Returns (final_state, attempts, notes)."""
    state = None
    notes = []
    for attempt in range(1, QUERY_RETRIES + 2):
        out, busy = board.cmd_retry_busy("cec tv status", QUERY_WINDOW_S)
        if busy:
            notes.append("q%d:busy x%d" % (attempt, busy))
        tx = TX_RE["cec tv status"].findall(out)
        if tx and not tx[-1].startswith(("ok", "reply ok")):
            notes.append("q%d:%s" % (attempt, tx[-1].split(",")[0]))
        hits = POWER_RE.findall(out)
        if hits:
            state = hits[-1]
        else:
            notes.append("q%d:no report" % attempt)
        if state == want:
            return state, attempt, notes
        time.sleep(RETRY_GAP_S)
    return state, QUERY_RETRIES + 1, notes


def half(board, command, want, strict=False):
    t0 = time.time()
    out, busy = board.cmd_retry_busy(command, CMD_WINDOW_S)
    tx = TX_RE[command].findall(out)
    tx_result = tx[0] if tx else "no tx line"
    if BUSY in out or REJECTED in out:
        tx_result = "rejected x%d" % busy
    elif busy:
        tx_result += " after %d rejects" % busy
    out += board.collect(SETTLE_S - CMD_WINDOW_S)
    state, attempts, notes = query_until(board, want)
    sent = len(TX_RE[command].findall(out))
    strict_ok = True
    if strict:
        if sent != 1:
            notes.insert(0, "cmd sent %dx" % sent)
            strict_ok = False
        if not tx_result.startswith("ok"):
            strict_ok = False
        if PWR_RETRY_MARK in out or PWR_ACCEPTED_LATE in out:
            notes.insert(0, "firmware retried cmd")
            strict_ok = False
        if "waits for the slot" in out:
            notes.append("parked")          # still sent once, informational
    return {
        "want": want,
        "tx": tx_result.split(",")[0],
        "state": state or "none",
        "attempts": attempts,
        "elapsed": round(time.time() - t0, 1),
        "notes": ";".join(notes),
        "ok": state == want and strict_ok,
    }


def audio_status(board, notes, tag):
    """(volume, muted) from `cec vol status`, or (None, None). Retries while the
    soundbar has not claimed the volume yet (keys would go to the TV)."""
    for attempt in range(1, AUDIO_RETRIES + 2):
        out, busy = board.cmd_retry_busy("cec vol status", AUDIO_WINDOW_S)
        if busy:
            notes.append("%s:busy x%d" % (tag, busy))
        if "volume -> tv" in out:
            notes.append("%s:soundbar not ready" % tag)
            time.sleep(RETRY_GAP_S)
            continue
        hits = AUDIO_RE.findall(out)
        if hits:
            vol, muted = hits[-1]
            return int(vol), bool(muted)
        notes.append("%s:no report" % tag)
        time.sleep(RETRY_GAP_S)
    return None, None


def keys(board, command, expect_frames, notes, tag):
    """Send a key command; count acknowledged press/release frames."""
    out, busy = board.cmd_retry_busy(command, VOL_KEYS_WINDOW_S)
    if busy:
        notes.append("%s:busy x%d" % (tag, busy))
    if "volume -> tv" in out:
        notes.append("%s:went to TV" % tag)
    tx = KEY_TX_RE.findall(out)
    good = sum(1 for _, r in tx if r.startswith("ok"))
    bad = [r.split(",")[0] for _, r in tx if not r.startswith("ok")]
    if bad:
        notes.append("%s:%s" % (tag, "/".join(bad)))
    if good != expect_frames:
        notes.append("%s:%d/%d frames ok" % (tag, good, expect_frames))
    if "volume keys done" not in out:
        notes.append("%s:no done" % tag)
    return good == expect_frames


def audio_block(board, step=VOL_STEP, order="vol"):
    """Volume up, down, mute, unmute, each verified via Report Audio Status.
    order "mute" does mute/unmute before the volume keys."""
    t0 = time.time()
    notes = []
    base, base_m = audio_status(board, notes, "base")
    r = {"base": base, "up": None, "down": None, "mute": None, "unmute": None}
    if base is None:
        return dict(r, elapsed=round(time.time() - t0, 1), notes=";".join(notes), ok=False)
    if base_m:
        notes.append("base:already muted")

    def do_vol():
        k1 = keys(board, "cec vol up %d" % step, 2 * step, notes, "up")
        r["up"], _ = audio_status(board, notes, "up")
        k2 = keys(board, "cec vol down %d" % step, 2 * step, notes, "down")
        r["down"], _ = audio_status(board, notes, "down")
        return k1, k2

    def do_mute():
        k1 = keys(board, "cec vol mute", 2, notes, "mute")
        r["mute"], m1 = audio_status(board, notes, "mute")
        k2 = keys(board, "cec vol mute", 2, notes, "unmute")
        r["unmute"], m2 = audio_status(board, notes, "unmute")
        return k1, k2, m1, m2

    if order == "mute":
        k_mute, k_unmute, m1, m2 = do_mute()
        k_up, k_down = do_vol()
    else:
        k_up, k_down = do_vol()
        k_mute, k_unmute, m1, m2 = do_mute()

    ok_up = r["up"] == base + step
    ok_down = r["down"] == base
    ok_mute = m1 is True
    ok_unmute = m2 is False
    for name, good in (("up", ok_up), ("down", ok_down), ("mute", ok_mute), ("unmute", ok_unmute)):
        if not good:
            notes.append("%s:wrong" % name)
    ok = all((k_up, k_down, k_mute, k_unmute, ok_up, ok_down, ok_mute, ok_unmute))
    r["mute"] = "%s%s" % (r["mute"], "m" if m1 else "") if r["mute"] is not None else None
    r["unmute"] = "%s%s" % (r["unmute"], "m" if m2 else "") if r["unmute"] is not None else None
    return dict(r, elapsed=round(time.time() - t0, 1), notes=";".join(notes), ok=ok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cycles", type=int, default=100)
    ap.add_argument("--audio", action="store_true", help="volume up/down + mute/unmute each cycle")
    ap.add_argument("--strict", action="store_true", help="power command must go out exactly once, acked first time")
    ap.add_argument("--variations", action="store_true", help="rotate audio variations per cycle (implies --audio)")
    ap.add_argument("--stop-on-fail", action="store_true", help="end the run at the first FAIL")
    ap.add_argument("--out", default="test-results/power-cycle-" + datetime.now().strftime("%Y%m%d-%H%M"))
    args = ap.parse_args()
    if args.variations:
        args.audio = True

    raw = open(args.out + ".raw.log", "w")
    csv = open(args.out + ".csv", "w")
    head = "cycle,off_tx,off_state,off_attempts,off_s,off_notes,on_tx,on_state,on_attempts,on_s,on_notes"
    if args.audio:
        head += ",vol_base,vol_up,vol_down,vol_mute,vol_unmute,audio_s,audio_notes,audio_ok,variation"
    csv.write(head + ",result\n")
    csv.flush()

    board = Board(args.port, args.baud, raw)
    status = board.cmd("status", 1.5)
    print(status.strip(), flush=True)
    start = datetime.now()
    rows = []
    aborted = None
    try:
        for i in range(1, args.cycles + 1):
            off = half(board, "cec off", "standby", args.strict)
            on = half(board, "cec on", "on", args.strict)
            row = [i, off["tx"], off["state"], off["attempts"], off["elapsed"], off["notes"],
                   on["tx"], on["state"], on["attempts"], on["elapsed"], on["notes"]]
            ok = off["ok"] and on["ok"]
            line = "cycle %3d  off:%-8s (%d q, %.0fs)  on:%-8s (%d q, %.0fs)" % (
                i, off["state"], off["attempts"], off["elapsed"],
                on["state"], on["attempts"], on["elapsed"])
            if args.audio:
                label, step, order = ("up/down %d, mute" % VOL_STEP, VOL_STEP, "vol")
                if args.variations:
                    label, step, order = VARIATIONS[(i - 1) % len(VARIATIONS)]
                if order is None:
                    a = dict(base=None, up=None, down=None, mute=None, unmute=None,
                             elapsed=0, notes="", ok=True)
                elif on["state"] == "on":
                    a = audio_block(board, step, order)
                else:
                    a = dict(base=None, up=None, down=None, mute=None, unmute=None,
                             elapsed=0, notes="skipped, TV not on", ok=False)
                row += [a["base"], a["up"], a["down"], a["mute"], a["unmute"],
                        a["elapsed"], a["notes"], "ok" if a["ok"] else "bad", label]
                ok = ok and a["ok"]
                line += "  [%s] vol %s>%s>%s mute %s>%s (%.0fs) %s" % (
                    label, a["base"], a["up"], a["down"], a["mute"], a["unmute"],
                    a["elapsed"], "ok" if a["ok"] else "BAD " + a["notes"])
            result = "PASS" if ok else "FAIL"
            row.append(result)
            rows.append(row)
            csv.write(",".join(str(x) for x in row) + "\n")
            csv.flush()
            print(line + "  " + result, flush=True)
            if args.stop_on_fail and result != "PASS":
                aborted = "stopped at first FAIL (cycle %d)" % i
                break
    except (serial.SerialException, OSError) as err:
        aborted = "serial error: %s" % err
        print(aborted, flush=True)
    except KeyboardInterrupt:
        aborted = "interrupted"
    finally:
        end = datetime.now()
        write_summary(args, start, end, status, rows, aborted)
        raw.close()
        csv.close()


def write_summary(args, start, end, status, rows, aborted):
    n = len(rows)
    passed = sum(1 for r in rows if r[-1] == "PASS")
    fails = [r for r in rows if r[-1] != "PASS"]
    off_att = [r[3] for r in rows]
    on_att = [r[8] for r in rows]
    off_s = [r[4] for r in rows]
    on_s = [r[9] for r in rows]
    tx_bad = [r for r in rows if r[1] != "ok" or r[6] != "ok"]
    rej_rows = [r for r in rows if any("busy" in str(x) or "reject" in str(x) for x in (r[1], r[5], r[6], r[10]))]
    cec_line = next((l for l in status.splitlines() if l.startswith("cec ")), "")
    with open(args.out + ".md", "w") as md:
        md.write("# CEC TV power cycle test%s\n\n" % (" + volume/mute" if args.audio else ""))
        md.write("- date: %s to %s (%s)\n" % (start.strftime("%Y-%m-%d %H:%M:%S"), end.strftime("%H:%M:%S"), str(end - start).split(".")[0]))
        md.write("- port: %s @ %d, board at start: `%s`\n" % (args.port, args.baud, cec_line.strip()))
        md.write("- cycle: `cec off` -> %.0fs settle -> `cec tv status` (retry x%d every %.0fs) -> `cec on` -> same" % (SETTLE_S, QUERY_RETRIES, RETRY_GAP_S))
        if args.audio:
            md.write(" -> `cec vol status` -> `cec vol up %d` -> status -> `cec vol down %d` -> status -> `cec vol mute` -> status -> `cec vol mute` -> status" % (VOL_STEP, VOL_STEP))
        md.write("\n- pass = TV reports `standby` after off and `on` after on")
        if args.strict:
            md.write("; power command went out exactly once, acked first time, no firmware retry")
        if args.audio:
            md.write("; every key frame acked; soundbar reports base+step, base, muted, unmuted")
        if args.variations:
            md.write("\n- variations rotated per cycle: " + ", ".join(v[0] for v in VARIATIONS))
        md.write("\n\n")
        if aborted:
            md.write("**ABORTED after %d cycles: %s**\n\n" % (n, aborted))
        md.write("## Result: %d/%d PASS, %d FAIL\n\n" % (passed, n, n - passed))
        if n:
            md.write("| | off | on |\n|---|---|---|\n")
            md.write("| queries to confirm, min/avg/max | %d / %.2f / %d | %d / %.2f / %d |\n" % (
                min(off_att), sum(off_att) / n, max(off_att), min(on_att), sum(on_att) / n, max(on_att)))
            md.write("| seconds cmd to confirm, min/avg/max | %.1f / %.1f / %.1f | %.1f / %.1f / %.1f |\n" % (
                min(off_s), sum(off_s) / n, max(off_s), min(on_s), sum(on_s) / n, max(on_s)))
            md.write("| cycles with first-query confirm | %d | %d |\n\n" % (
                sum(1 for a in off_att if a == 1), sum(1 for a in on_att if a == 1)))
        md.write("- power command TX not `ok`: %d cycles\n" % len(tx_bad))
        md.write("- shell rejections (busy / already waiting, resent after %.0fs): %d cycles\n" % (BUSY_GAP_S, len(rej_rows)))
        anomalies = [r for r in rows if r[5] or r[10]]
        md.write("- cycles with power query anomalies (no ack / transitional / no report): %d\n\n" % len(anomalies))
        if args.audio and n:
            with_audio = [r for r in rows if r[11] not in (None, "None") or r[17]]
            a_ok = sum(1 for r in with_audio if r[18] == "ok")
            a_s = [r[16] for r in rows if r[16]]
            bases = [r[11] for r in rows if r[11] not in (None, "None")]
            md.write("## Volume and mute\n\n")
            md.write("- audio block passed: %d/%d cycles that had one\n" % (a_ok, len(with_audio)))
            if args.variations:
                for label, _, _ in VARIATIONS:
                    vr = [r for r in rows if r[19] == label]
                    md.write("- `%s`: %d cycles, %d PASS\n" % (label, len(vr), sum(1 for r in vr if r[-1] == "PASS")))
            if a_s:
                md.write("- seconds per audio block, min/avg/max: %.1f / %.1f / %.1f\n" % (min(a_s), sum(a_s) / len(a_s), max(a_s)))
            if bases:
                md.write("- soundbar volume at block start: first %s, last %s, min %s, max %s\n" % (bases[0], bases[-1], min(bases), max(bases)))
            a_notes = [r for r in rows if r[17]]
            md.write("- cycles with audio notes: %d\n\n" % len(a_notes))
            if a_notes:
                md.write("| cycle | variation | base | up | down | mute | unmute | notes | audio |\n|---|---|---|---|---|---|---|---|---|\n")
                for r in a_notes:
                    md.write("| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n" % (r[0], r[19] if len(r) > 19 else "", r[11], r[12], r[13], r[14], r[15], r[17], r[18]))
                md.write("\n")
        if fails:
            md.write("## Failures\n\n| cycle | off tx | off state | on tx | on state | notes |\n|---|---|---|---|---|---|\n")
            for r in fails:
                extra = (" audio: " + str(r[17])) if args.audio else ""
                md.write("| %s | %s | %s | %s | %s | %s %s%s |\n" % (r[0], r[1], r[2], r[6], r[7], r[5], r[10], extra))
            md.write("\n")
        if anomalies:
            md.write("## Power query anomalies (passed or not)\n\n")
            kinds = {}
            for r in anomalies:
                for note in (r[5] + ";" + r[10]).split(";"):
                    note = note.split(":", 1)[-1].strip()
                    if note:
                        kinds[note] = kinds.get(note, 0) + 1
            md.write("| note | occurrences |\n|---|---|\n")
            for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]):
                md.write("| %s | %d |\n" % (k, v))
            md.write("\n")
            if len(anomalies) <= 40:
                md.write("| cycle | off notes | on notes | result |\n|---|---|---|---|\n")
                for r in anomalies:
                    md.write("| %s | %s | %s | %s |\n" % (r[0], r[5], r[10], r[-1]))
                md.write("\n")
            else:
                md.write("Per cycle notes in the csv (%d cycles).\n\n" % len(anomalies))
        md.write("Per cycle data: `%s.csv`. Full serial capture: `%s.raw.log`.\n" % (args.out, args.out))


if __name__ == "__main__":
    sys.exit(main())
