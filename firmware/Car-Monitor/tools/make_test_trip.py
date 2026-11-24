#!/usr/bin/env python3
"""Generate TLOG test files with known contents, so read_trip.py can be checked against known answers.

  python make_test_trip.py clean.bin            GPS + OBD, nothing wrong
  python make_test_trip.py damaged.bin --damage  same, plus: one flipped bit, garbage bytes, two
                                                 dropped records, a truncated last record

Prints what the reader should report. The encoders mirror lib/logfmt (little-endian, CRC-16/CCITT-FALSE).
"""
import argparse
import math
import random
import struct

MAGIC = 0xA5
REC_GPS, REC_ELM = 1, 3
ELM_OK, ELM_NO_DATA, ELM_ERROR = 0, 1, 2
PID_ADAPTER_VOLTAGE = 0xF0


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def record(rtype, seq, tms, payload):
    body = struct.pack("<BBBII", MAGIC, rtype, len(payload), seq, tms) + payload
    return body + struct.pack("<H", crc16(body))


def gps_payload(lat, lon, mph, alt_m, heading, sats, status, utc_ms):
    if status != 2:                     # firmware writes zeros unless the fix is OK
        lat = lon = mph = 0
    return struct.pack("<iiHhHBBI", int(lat * 1e7), int(lon * 1e7), int(mph * 100),
                       int(alt_m * 10), int(heading * 10), sats, status, utc_ms)


def elm_payload(pid, status, raw=b""):
    assert len(raw) <= 4 and (status == ELM_OK or not raw)
    return struct.pack("<BBB4s", pid, status, len(raw), raw.ljust(4, b"\0"))


def build(damage):
    """Returns (file bytes, expectations dict)."""
    rng = random.Random(1)
    out = bytearray(b"TLOG\x01")
    seq = 0
    exp = dict(gps=0, obd_ok=0, obd_no_data=0)
    spans = []                          # (start, end) of each record, for damage placement

    def emit(rtype, tms, payload):
        nonlocal seq
        r = record(rtype, seq, tms, payload)
        spans.append((len(out), len(out) + len(r)))
        out.extend(r)
        seq += 1

    lat, lon = 46.7333, -117.1697
    for i in range(60):                 # 12 s of 5 Hz GPS, with OBD between fixes
        t = 1000 + i * 200
        mph = min(40.0, 2.0 * i)
        lat += 0.00001 * mph / 10
        emit(REC_GPS, t, gps_payload(lat, lon, mph, 740.0, 90.0, 9, 2, 40000000 + i * 200))
        exp["gps"] += 1
        # fast PIDs every fix: RPM, speed; slow PIDs rotate; voltage now and then
        rpm_raw = int(800 + mph * 40) * 4
        emit(REC_ELM, t + 20, elm_payload(0x0C, ELM_OK, struct.pack(">H", rpm_raw)))
        emit(REC_ELM, t + 40, elm_payload(0x0D, ELM_OK, bytes([int(mph * 1.609)])))
        exp["obd_ok"] += 2
        if i % 5 == 0:
            emit(REC_ELM, t + 60, elm_payload(0x05, ELM_OK, bytes([90 + 40])))     # 90 C
            exp["obd_ok"] += 1
        if i % 20 == 0:
            mv = 13800
            emit(REC_ELM, t + 80, elm_payload(PID_ADAPTER_VOLTAGE, ELM_OK, struct.pack(">H", mv)))
            exp["obd_ok"] += 1
    t = 1000 + 60 * 200                 # key off: a NO DATA status-change record, no values
    emit(REC_ELM, t, elm_payload(0x0C, ELM_NO_DATA))
    exp["obd_no_data"] += 1

    exp["records"] = seq
    exp["damage"] = None
    if not damage:
        return bytes(out), exp

    # Damage, applied back to front so earlier offsets stay valid.
    out = bytearray(out)
    # 1) truncate the last record (power cut mid-write)
    last_s, last_e = spans[-1]
    del out[last_e - 5:]
    lost = 1
    # 2) drop two whole records in the middle (shows up as a sequence gap of 2)
    a = spans[100][0]; b = spans[101][1]
    del out[a:b]
    lost += 2
    # 3) garbage bytes inserted (no record lost)
    out[spans[60][0]:spans[60][0]] = bytes([0x00, 0xA5, 0x01, 0x14, 0x77])
    # 4) flipped bit inside one record's payload: that record is lost
    s0 = spans[30][0]
    out[s0 + 12] ^= 0x10
    lost += 1
    exp["damage"] = dict(records_lost=lost, seq_missing_min=2, crc_failures_min=1)
    exp["records_read"] = seq - lost
    return bytes(out), exp


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--damage", action="store_true")
    args = ap.parse_args()
    data, exp = build(args.damage)
    with open(args.file, "wb") as f:
        f.write(data)
    print(f"wrote {len(data)} bytes to {args.file}")
    print(f"expected: {exp['gps']} GPS records, {exp['obd_ok']} OBD OK readings, "
          f"{exp['obd_no_data']} OBD NO_DATA record, {exp['records']} records written")
    if exp["damage"]:
        d = exp["damage"]
        print(f"damage: {d['records_lost']} records unreadable, so expect {exp['records_read']} read, "
              f">= {d['crc_failures_min']} CRC failure, sequence gaps for the dropped records, "
              f"a truncated last record")


if __name__ == "__main__":
    main()
