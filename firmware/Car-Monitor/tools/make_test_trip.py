#!/usr/bin/env python3
"""Read a TLOG trip file written by the ESP32 logger and print a health + trip summary.

Format (little-endian):
  file header : 'TLOG' + version byte                              (5 bytes)
  record      : magic 0xA5, type, len, seq(u32), tMs(u32), payload, crc16(u16)
  GPS payload : latE7(i32) lonE7(i32) speedCentiMph(u16) altDeciM(i16)
                headingDeci(u16) sats(u8) status(u8) utcMs(u32)    (20 bytes)
  CRC         : CRC-16/CCITT-FALSE over magic..payload
Status byte: 0 = LINK_LOST, 1 = NO_FIX, 2 = OK (trustworthy position)
"""
import argparse
import math
import struct
import sys

MAGIC = 0xA5
HEADER_LEN = 11
MAX_PAYLOAD = 64
REC_GPS = 1
GPS_FMT = "<iiHhHBBI"
GPS_LEN = struct.calcsize(GPS_FMT)  # 20


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def haversine_m(lat1, lon1, lat2, lon2):
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def parse(data: bytes):
    """Walk the file. Returns (records, stats). Bad data costs one byte, then we resync."""
    stats = dict(version=None, header_ok=False, records=0, crc_failures=0,
                 bytes_skipped=0, unknown_types=0, seq_gaps=0, seq_missing=0,
                 time_gaps=0, time_gap_ms=0, truncated_tail=0)
    if data[:4] != b"TLOG" or len(data) < 5:
        return [], stats
    stats["header_ok"] = True
    stats["version"] = data[4]

    recs = []
    i = 5
    expect_seq = None
    last_t = None
    n = len(data)
    while i < n:
        if data[i] != MAGIC:
            stats["bytes_skipped"] += 1
            i += 1
            continue
        if n - i < HEADER_LEN:
            stats["truncated_tail"] = n - i
            break
        length = data[i + 2]
        if length > MAX_PAYLOAD:
            stats["bytes_skipped"] += 1
            i += 1
            continue
        total = HEADER_LEN + length + 2
        if n - i < total:
            stats["truncated_tail"] = n - i
            break
        body = data[i:i + HEADER_LEN + length]
        stored = struct.unpack_from("<H", data, i + HEADER_LEN + length)[0]
        if crc16(body) != stored:
            stats["crc_failures"] += 1
            stats["bytes_skipped"] += 1
            i += 1
            continue

        rtype = data[i + 1]
        seq, tms = struct.unpack_from("<II", data, i + 3)
        payload = data[i + HEADER_LEN:i + HEADER_LEN + length]

        if expect_seq is not None and seq != expect_seq:
            stats["seq_gaps"] += 1
            if seq > expect_seq:
                stats["seq_missing"] += seq - expect_seq
        expect_seq = seq + 1

        if last_t is not None and tms - last_t > 1000:
            stats["time_gaps"] += 1
            stats["time_gap_ms"] += tms - last_t
        last_t = tms

        rec = dict(type=rtype, seq=seq, tms=tms)
        if rtype == REC_GPS and length == GPS_LEN:
            lat, lon, spd, alt, hdg, sats, status, utc = struct.unpack(GPS_FMT, payload)
            rec.update(lat=lat / 1e7, lon=lon / 1e7, mph=spd / 100.0, alt_m=alt / 10.0,
                       heading=hdg / 10.0, sats=sats, status=status, utc_ms=utc)
        else:
            stats["unknown_types"] += 1
        recs.append(rec)
        stats["records"] += 1
        i += total
    return recs, stats


def fmt_utc(ms):
    s = ms // 1000
    return f"{s // 3600:02d}:{(s // 60) % 60:02d}:{s % 60:02d} UTC"


def summarize(path, recs, st):
    gps = [r for r in recs if "status" in r]
    print(path)
    if not st["header_ok"]:
        print("  NOT A TLOG FILE (bad header)")
        return
    print(f"  format version:      {st['version']}")
    print(f"  records read:        {st['records']}")
    print(f"  CRC failures:        {st['crc_failures']}")
    print(f"  bytes skipped:       {st['bytes_skipped']}")
    print(f"  sequence gaps:       {st['seq_gaps']}  ({st['seq_missing']} records missing)")
    print(f"  logging stalls >1s:  {st['time_gaps']}  ({st['time_gap_ms'] / 1000:.1f} s total)")
    if st["truncated_tail"]:
        print(f"  truncated last record: {st['truncated_tail']} bytes (power cut mid-write)")
    if st["unknown_types"]:
        print(f"  unknown record types: {st['unknown_types']}")
    if not gps:
        return

    names = {0: "LINK_LOST", 1: "NO_FIX", 2: "OK"}
    counts = {k: sum(1 for r in gps if r["status"] == k) for k in names}
    print("  GPS records by status: " + ", ".join(f"{names[k]} {v}" for k, v in counts.items()))

    dur = (gps[-1]["tms"] - gps[0]["tms"]) / 1000.0
    print(f"  duration:            {dur:.1f} s ({st['records'] / dur:.2f} records/s)" if dur > 0
          else "  duration:            0 s")

    utcs = [r["utc_ms"] for r in gps if r["utc_ms"]]
    if utcs:
        print(f"  GPS time span:       {fmt_utc(utcs[0])} -> {fmt_utc(utcs[-1])}")
    else:
        print("  GPS time span:       none (module never reported a time)")

    good = [r for r in gps if r["status"] == 2]
    if good:
        dist = 0.0
        for a, b in zip(good, good[1:]):
            if b["tms"] - a["tms"] <= 2000:
                dist += haversine_m(a["lat"], a["lon"], b["lat"], b["lon"])
        print(f"  distance (OK only):  {dist / 1609.344:.2f} mi")
        print(f"  top speed:           {max(r['mph'] for r in good):.1f} mph")
        print(f"  max satellites:      {max(r['sats'] for r in good)}")
    else:
        print("  no OK fixes in this file, so no distance or speed to report")


def main():
    ap = argparse.ArgumentParser(description="Summarize a TLOG trip file")
    ap.add_argument("file")
    ap.add_argument("--csv", help="write GPS records to this CSV file")
    ap.add_argument("--plot", action="store_true", help="plot the route and speed (needs matplotlib)")
    args = ap.parse_args()

    with open(args.file, "rb") as f:
        data = f.read()
    recs, st = parse(data)
    summarize(args.file, recs, st)

    gps = [r for r in recs if "status" in r]
    if args.csv:
        with open(args.csv, "w") as f:
            f.write("seq,t_ms,status,lat,lon,mph,alt_m,heading,sats,utc_ms\n")
            for r in gps:
                f.write(f"{r['seq']},{r['tms']},{r['status']},{r['lat']:.7f},{r['lon']:.7f},"
                        f"{r['mph']:.2f},{r['alt_m']:.1f},{r['heading']:.1f},{r['sats']},{r['utc_ms']}\n")
        print(f"  wrote {len(gps)} rows to {args.csv}")
    if args.plot:
        try:
            import matplotlib.pyplot as plt
        except ImportError:
            sys.exit("matplotlib not installed: pip install matplotlib")
        good = [r for r in gps if r["status"] == 2]
        if not good:
            sys.exit("nothing to plot: no OK fixes")
        fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4))
        sc = a1.scatter([r["lon"] for r in good], [r["lat"] for r in good],
                        c=[r["mph"] for r in good], s=6)
        fig.colorbar(sc, ax=a1, label="mph")
        a1.set_xlabel("longitude"); a1.set_ylabel("latitude"); a1.set_title("route")
        a2.plot([r["tms"] / 1000 for r in good], [r["mph"] for r in good])
        a2.set_xlabel("seconds since boot"); a2.set_ylabel("mph"); a2.set_title("speed")
        plt.tight_layout()
        plt.show()


if __name__ == "__main__":
    main()
