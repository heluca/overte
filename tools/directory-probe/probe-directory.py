#!/usr/bin/env python3
"""Check which directory (metaverse) places actually answer.

A place can look alive in the directory and still be an empty sky: the domain server answers,
but its assignment clients (entity server, mixers) never reply to the hole-punch pings, so no
content ever arrives. Nothing on those assignment clients answers an unauthenticated packet, so
the only reliable probe is the real client. This script fetches the directory, writes a test
script that hops one Interface session through each selected place, and reads the verdict back
from the client's log.

Examples:
    probe-directory.py --live                 # every place whose domain checked in recently
    probe-directory.py anime-avatars hytrion  # specific places
    probe-directory.py --live --json          # machine-readable output

Interface must not already be running: a second instance hands its --url to the running one.
"""
import argparse
import datetime as dt
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
import urllib.request

DIRECTORY = "https://mv.overte.org/server"
DEFAULT_BINS = {
    "Darwin": "/Applications/Overte.app/Contents/MacOS/Overte",
}

JS_TEMPLATE = r"""
var PLACES = %(places)s;
var DWELL = %(dwell)d * 1000;
var i = -1;

function report() {
    var n = 0;
    try { n = Entities.findEntities(MyAvatar.position, 4000).length; } catch (e) {}
    print("PROBE_RESULT " + JSON.stringify({
        place: PLACES[i],
        hostname: location.hostname,
        connected: Window.location.isConnected,
        entityServers: Entities.serversExist(),
        entities: n,
        avatars: AvatarList.getAvatarIdentifiers().length
    }));
}

function next() {
    if (i >= 0) { report(); }
    i++;
    if (i >= PLACES.length) { print("PROBE_DONE"); Script.stop(); return; }
    print("PROBE_BEGIN " + PLACES[i]);
    location.handleLookupString("hifi://" + PLACES[i]);
    Script.setTimeout(next, DWELL);
}

Script.setTimeout(next, 3000);
"""


def fetch_places():
    places, page = [], 1
    while True:
        url = f"{DIRECTORY}/api/v1/places?per_page=200&page={page}"
        with urllib.request.urlopen(url, timeout=30) as r:
            body = json.load(r)
        data = body["data"]["places"]
        places.extend(data)
        if page >= int(body.get("total_pages", 1)) or not data:
            return places
        page += 1


def heartbeat_age_min(place):
    hb = place.get("domain", {}).get("time_of_last_heartbeat")
    if not hb:
        return None
    try:
        t = dt.datetime.fromisoformat(hb.replace("Z", "+00:00"))
    except ValueError:
        return None
    return (dt.datetime.now(dt.timezone.utc) - t).total_seconds() / 60


def find_binary(explicit):
    if explicit:
        return explicit
    env = os.environ.get("OVERTE_BIN")
    if env:
        return env
    return DEFAULT_BINS.get(platform.system())


def interface_running():
    try:
        out = subprocess.run(["pgrep", "-f", "Overte.app/Contents/MacOS/Overte|/interface( |$)"],
                             capture_output=True, text=True)
        return out.returncode == 0 and out.stdout.strip() != ""
    except FileNotFoundError:
        return False


def run_interface(binary, places, dwell, log_path):
    js = JS_TEMPLATE % {"places": json.dumps(places), "dwell": dwell}
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False, prefix="probe-directory-") as f:
        f.write(js)
        script = f.name
    cmd = [binary, "--testScript", script, "--quitWhenFinished"]
    if platform.system() == "Darwin":
        # Without a display plugin the Mac build blocks on a modal dialog. (On Linux Qt eats --display.)
        cmd += ["--display", "Desktop"]
    budget = 60 + dwell * len(places) + 15
    with open(log_path, "w") as log:
        try:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=budget)
        except subprocess.TimeoutExpired:
            print(f"warning: Interface did not finish within {budget}s, using partial results", file=sys.stderr)
    os.unlink(script)
    with open(log_path, errors="replace") as log:
        return log.read()


RESULT_RE = re.compile(r"PROBE_RESULT (\{.*\})")
BEGIN_RE = re.compile(r"PROBE_BEGIN (\S+)")


def parse_log(text):
    """Slice the log by PROBE_BEGIN markers and count the networking evidence per place."""
    results, current, counts = {}, None, {}
    for line in text.splitlines():
        m = BEGIN_RE.search(line)
        if m:
            current = m.group(1)
            counts[current] = {"holepunch_fail": 0, "sockets_activated": 0, "nodes_added": 0, "nodes_killed": 0}
            continue
        m = RESULT_RE.search(line)
        if m:
            try:
                r = json.loads(m.group(1))
                results[r["place"]] = r
            except json.JSONDecodeError:
                pass
            continue
        if current is None:
            continue
        c = counts[current]
        if "No response to UDP hole punch pings" in line:
            c["holepunch_fail"] += 1
        elif "Activating public socket" in line or "Activating local socket" in line:
            c["sockets_activated"] += 1
        elif "[hifi.networking] Added \"" in line:
            c["nodes_added"] += 1
        elif "[hifi.networking] Killed \"" in line:
            c["nodes_killed"] += 1
    for place, c in counts.items():
        results.setdefault(place, {"place": place, "connected": None})
        results[place].update(c)
    return results


def verdict(r):
    if r.get("connected") is None:
        return "NO-RESULT"
    if not r["connected"]:
        return "NO-DOMAIN-SERVER"
    if r.get("entityServers"):
        return "OK"
    if r.get("nodes_added", 0) > 0 and r.get("sockets_activated", 0) == 0:
        return "ASSIGNMENT-CLIENTS-UNREACHABLE"
    return "NO-ENTITY-SERVER"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("places", nargs="*", help="place names (as in the directory)")
    ap.add_argument("--live", action="store_true", help="probe every place whose domain checked in within --max-age")
    ap.add_argument("--max-age", type=float, default=30, help="minutes since last heartbeat for --live (default 30)")
    ap.add_argument("--list", action="store_true", help="only list directory places with heartbeat age, do not probe")
    ap.add_argument("--dwell", type=int, default=20, help="seconds to wait in each place (default 20)")
    ap.add_argument("--bin", help="Interface binary (default: $OVERTE_BIN or the installed Overte.app)")
    ap.add_argument("--json", action="store_true", help="print results as JSON")
    ap.add_argument("--log", help="keep the Interface log at this path")
    args = ap.parse_args()

    places = fetch_places()
    by_name = {p["name"]: p for p in places}
    by_name.update({p["name"].lower(): p for p in places})
    for p in places:
        p["_age"] = heartbeat_age_min(p)

    if args.list:
        for p in sorted(places, key=lambda p: (p["_age"] is None, p["_age"] or 0)):
            age = "never" if p["_age"] is None else f"{p['_age']:.0f} min"
            print(f"{age:>10}  {p['name']:28} {p['address'].split('/')[0]:24} {p['domain'].get('version', '')}")
        return

    if args.live:
        selected = [p["name"] for p in places if p["_age"] is not None and p["_age"] <= args.max_age]
    else:
        selected = []
        for name in args.places:
            key = name.replace("hifi://", "").split("/")[0]
            selected.append(by_name.get(key, by_name.get(key.lower(), {"name": key}))["name"])
    if not selected:
        sys.exit("nothing to probe (no live places, or none given)")

    binary = find_binary(args.bin)
    if not binary or not os.path.exists(binary):
        sys.exit(f"Interface binary not found ({binary}); pass --bin or set OVERTE_BIN")
    if interface_running():
        sys.exit("Overte is already running; quit it first (a second instance would just hand the URL to it)")

    log_path = args.log or os.path.join(tempfile.gettempdir(), "probe-directory.log")
    print(f"probing {len(selected)} place(s), {args.dwell}s each, log at {log_path}", file=sys.stderr)
    text = run_interface(binary, selected, args.dwell, log_path)
    results = parse_log(text)

    rows = []
    for name in selected:
        r = results.get(name, {"place": name, "connected": None})
        r["verdict"] = verdict(r)
        r["address"] = by_name.get(name, {}).get("address", "").split("/")[0]
        r["heartbeat_min"] = by_name.get(name, {}).get("_age")
        rows.append(r)

    if args.json:
        print(json.dumps(rows, indent=2))
        return
    print(f"{'place':28} {'address':24} {'verdict':30} {'entities':>8} {'nodes':>5} {'acts':>4} {'nopunch':>7}")
    for r in rows:
        print(f"{r['place']:28} {r['address']:24} {r['verdict']:30} {str(r.get('entities', '')):>8} "
              f"{str(r.get('nodes_added', '')):>5} {str(r.get('sockets_activated', '')):>4} {str(r.get('holepunch_fail', '')):>7}")


if __name__ == "__main__":
    main()
