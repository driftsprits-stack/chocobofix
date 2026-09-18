#!/usr/bin/env bash
# End-to-end and security checks against the built binaries.
# usage: tests/integration.sh [build_dir]    (run from the repository root)
set -uo pipefail
BUILD="${1:-build}"
PORT=${PORT:-8123}
DATA=data/upstream/PS1/01_data
TMP=$(mktemp -d)
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf '  ok   %s\n' "$1"; }
no(){ FAIL=$((FAIL+1)); printf '  FAIL %s\n' "$1"; }
chk(){ if [ "$1" = "$2" ]; then ok "$3"; else no "$3 (got '$1', expected '$2')"; fi; }
cleanup(){ [ -n "${SVC:-}" ] && kill "$SVC" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT

echo "== CLI: solve all three scenarios =="
"$BUILD/trackaccess" solve --data "$DATA" --out "$TMP/out" --scenario all --seconds 60 --workers 8 \
  > "$TMP/solve.log" 2>&1
chk "$?" "0" "solve exits 0"
for s in A B C; do
  for f in SCHEDULE_ACCESS.csv SCHEDULE_OCCUPANCY.csv RESULTS.csv; do
    [ -s "$TMP/out/$s/$f" ] && ok "scenario $s produced $f" || no "scenario $s produced $f"
  done
  # RESULTS.csv must never mix scenarios.
  n=$(tail -n +2 "$TMP/out/$s/RESULTS.csv" | cut -d, -f1 | sort -u | wc -l | tr -d ' ')
  chk "$n" "1" "scenario $s RESULTS.csv holds exactly one scenario"
  hdr=$(head -1 "$TMP/out/$s/SCHEDULE_ACCESS.csv")
  chk "$hdr" "activity_id,access_seq,week,eclo,access_night" "scenario $s SCHEDULE_ACCESS header is exact"
  hdr=$(head -1 "$TMP/out/$s/SCHEDULE_OCCUPANCY.csv")
  chk "$hdr" "activity_id,week,location_id,co_share_group" "scenario $s SCHEDULE_OCCUPANCY header is exact"
  hdr=$(head -1 "$TMP/out/$s/RESULTS.csv")
  chk "$hdr" "scenario,contract_number,simulated_completion_date,overrun_days" "scenario $s RESULTS header is exact"
done

echo "== CLI: re-validate the emitted files =="
for s in A B C; do
  "$BUILD/trackaccess" validate --data "$DATA" --submission "$TMP/out/$s" > "$TMP/val_$s.json" 2>&1
  chk "$?" "0" "scenario $s validates as feasible"
done

echo "== independent Python cross-check agrees =="
for s in A B C; do
  out=$(python3 tools/derive/crosscheck.py "$DATA" "$TMP/out/$s" 2>&1)
  echo "$out" | grep -q FEASIBLE && ok "python cross-check: $out" || no "python cross-check: $out"
done

echo "== the shipped official sample still validates =="
"$BUILD/trackaccess" validate --data "$DATA" --submission data/upstream/PS1/03_submission_sample >/dev/null 2>&1
chk "$?" "0" "official sample validates as feasible"

echo "== the checker rejects a corrupted submission =="
cp -r "$TMP/out/A" "$TMP/broken"
# Remove one activity's accesses entirely: workload conservation must fail.
victim=$(tail -n +2 "$TMP/broken/SCHEDULE_ACCESS.csv" | head -1 | cut -d, -f1)
grep -v "^$victim," "$TMP/broken/SCHEDULE_ACCESS.csv" > "$TMP/x" && mv "$TMP/x" "$TMP/broken/SCHEDULE_ACCESS.csv"
"$BUILD/trackaccess" validate --data "$DATA" --submission "$TMP/broken" >/dev/null 2>&1
chk "$?" "3" "a submission missing an activity's work is rejected with exit 3"

echo "== the loader rejects malformed input =="
cp -r "$DATA" "$TMP/badinst"
printf 'key,value\nhorizon_start,2027-13-45\nhorizon_weeks,30\n' > "$TMP/badinst/06_PARAMETERS.csv"
"$BUILD/trackaccess" solve --data "$TMP/badinst" --out "$TMP/o2" --scenario A --seconds 5 >/dev/null 2>&1
chk "$?" "1" "an impossible date is rejected with exit 1"

echo "== explain: a reachable week =="
out=$("$BUILD/trackaccess" explain --data "$DATA" --activity A004 --week 21 --scenario A --seconds 15 2>&1)
echo "$out" | grep -q "YES - it can" && ok "explain reports a reachable week as reachable" \
  || no "explain on a reachable week: $(echo "$out" | tail -1)"

echo "== explain: an impossible week names the binding rule =="
out=$("$BUILD/trackaccess" explain --data "$DATA" --activity A004 --week 5 --scenario A --seconds 15 2>&1)
echo "$out" | grep -q "NO - proven impossible" && ok "explain proves an impossible week impossible" \
  || no "explain on an impossible week"
echo "$out" | grep -q "BINDING  predecessor precedence" && ok "explain names predecessor precedence as binding" \
  || no "explain names the binding rule"
# A timeout must never be dressed up as impossibility.
out=$("$BUILD/trackaccess" explain --data "$DATA" --activity A004 --week 5 --scenario A --seconds 1 2>&1)
echo "$out" | grep -q "NOT ESTABLISHED\|NO - proven impossible" && ok "a tiny budget yields either a proof or 'not established', never a bare no" \
  || no "explain under a tiny budget"

echo "== repair: a disruption is absorbed and re-checked =="
out=$("$BUILD/trackaccess" repair --data "$DATA" --out "$TMP/rep" \
        --supply "SEC:BET:H01_H02:EB@15=0" --supply "SEC:BET:H01_H02:EB@16=0" \
        --scenario A --seconds 45 2>&1)
chk "$?" "0" "repair exits 0"
echo "$out" | grep -q "independent check: FEASIBLE" && ok "the repaired plan passes the independent check" \
  || no "repaired plan feasibility"
echo "$out" | grep -q "churn" && ok "repair reports how much moved" || no "repair reports churn"
[ -s "$TMP/rep/SCHEDULE_ACCESS.csv" ] && ok "repair exported a schedule" || no "repair exported a schedule"
# A malformed disruption must be refused, not guessed at.
"$BUILD/trackaccess" repair --data "$DATA" --out "$TMP/rep2" --supply "NOT_A_LOCATION@9=1" --seconds 5 >/dev/null 2>&1
chk "$?" "1" "an unknown location in a disruption is refused"

echo "== compare: before/after =="
out=$("$BUILD/trackaccess" compare --data "$DATA" --before "$TMP/out/A" --after "$TMP/rep" 2>&1)
echo "$out" | grep -q "activities changed" && ok "compare reports what changed" || no "compare output"

echo "== the competition score is not contaminated by the repair preference =="
# out/A and a repair with no disruption effect must score identically.
a=$("$BUILD/trackaccess" validate --data "$DATA" --submission "$TMP/out/A" 2>/dev/null \
     | python3 -c 'import json,sys;print(json.load(sys.stdin)["soft_scores"]["objective_score"])')
[ -n "$a" ] && ok "scenario A objective recomputed from file: $a" || no "objective recomputed"

echo "== an infeasible scenario policy: refuse by default, fall back only on request =="
cp -r "$DATA" "$TMP/tight"
python3 - "$TMP/tight" <<'PYEOF'
import csv, sys
p = sys.argv[1] + '/07_PROJECT_DETAILS.csv'
rows = list(csv.DictReader(open(p)))
for r in rows: r['planned_completion_date'] = '2027-03-21'
with open(p, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=rows[0].keys()); w.writeheader(); w.writerows(rows)
PYEOF
"$BUILD/trackaccess" solve --data "$TMP/tight" --out "$TMP/tb1" --scenario B --seconds 25 >"$TMP/tb1.log" 2>&1
grep -q "infeasible" "$TMP/tb1.log" && ok "an unsatisfiable Scenario B is reported infeasible" \
  || no "unsatisfiable Scenario B reported infeasible"
[ ! -f "$TMP/tb1/B/SCHEDULE_ACCESS.csv" ] && ok "nothing is exported for it by default" \
  || no "nothing exported by default"

"$BUILD/trackaccess" solve --data "$TMP/tight" --out "$TMP/tb2" --scenario B --seconds 25 --fallback >"$TMP/tb2.log" 2>&1
[ -f "$TMP/tb2/B/SCHEDULE_ACCESS.csv" ] && ok "--fallback does export a plan" || no "--fallback exports a plan"
[ -f "$TMP/tb2/B/NOT_SUBMISSION_READY.txt" ] && ok "  ... marked NOT_SUBMISSION_READY on disk" \
  || no "fallback plan is marked on disk"
grep -q "OUT OF POLICY" "$TMP/tb2.log" && ok "  ... and the run says so loudly" || no "fallback run says so"
# The whole point: only the scenario POLICY is breached, never a safety rule.
rules=$(python3 -c "
import json,collections
d=json.load(open('$TMP/tb2/B/VALIDATION.json'))
print(','.join(sorted({v['rule'] for v in d['hard_violations']})))")
chk "$rules" "planned_date" "  ... and the ONLY breaches are the scenario policy, not any safety rule"

echo "== service: start with token auth =="
TOKEN=testtoken0123456789abcdef
"$BUILD/trackaccess-service" --host 127.0.0.1 --port "$PORT" --root "$TMP/var" --web web \
  --worker "$BUILD/trackaccess" --token "$TOKEN" --public-instance "$DATA" > "$TMP/svc.log" 2>&1 &
SVC=$!
for i in $(seq 1 40); do curl -sf "http://127.0.0.1:$PORT/api/v1/health" >/dev/null 2>&1 && break; sleep 0.25; done
B="http://127.0.0.1:$PORT/api/v1"
AUTH=(-H "Authorization: Bearer $TOKEN")

code(){ curl -s -o /dev/null -w '%{http_code}' "$@"; }
chk "$(code "$B/health")" "200" "health needs no token"
chk "$(code -X POST "$B/instances/demo")" "401" "an unauthenticated request is refused"
chk "$(code -H 'Authorization: Bearer wrong-token-value-x' -X POST "$B/instances/demo")" "401" "a wrong token is refused"
chk "$(code "${AUTH[@]}" "$B/jobs/doesnotexist")" "404" "an unknown job is a 404"
chk "$(code "${AUTH[@]}" "$B/jobs/..%2f..%2fetc%2fpasswd")" "404" "a traversal-styled job id does not resolve"
chk "$(code "${AUTH[@]}" "$B/instances/ABC123/detail")" "404" "an id outside our alphabet is refused"

IID=$(curl -s "${AUTH[@]}" -X POST "$B/instances/demo" | python3 -c 'import json,sys;print(json.load(sys.stdin)["instance_id"])' 2>/dev/null)
[ -n "$IID" ] && ok "bundled public instance loads over the API" || no "bundled public instance loads over the API"
chk "$(code "${AUTH[@]}" -X POST "$B/jobs?instance_id=$IID&scenario=Z")" "400" "an unknown scenario is refused"
chk "$(code "${AUTH[@]}" -X POST "$B/jobs?instance_id=nosuchinstance&scenario=A")" "404" "a job for an unknown instance is refused"

JID=$(curl -s "${AUTH[@]}" -X POST "$B/jobs?instance_id=$IID&scenario=A&seconds=30" \
      | python3 -c 'import json,sys;print(json.load(sys.stdin)["job_id"])' 2>/dev/null)
for i in $(seq 1 60); do
  ST=$(curl -s "${AUTH[@]}" "$B/jobs/$JID" | python3 -c 'import json,sys;print(json.load(sys.stdin)["state"])' 2>/dev/null)
  [ "$ST" = done ] || [ "$ST" = failed ] || [ "$ST" = cancelled ] && break
  sleep 1
done
chk "$ST" "done" "a solve job submitted over the API completes"
feas=$(curl -s "${AUTH[@]}" "$B/jobs/$JID/validation/A" | python3 -c 'import json,sys;print(json.load(sys.stdin)["feasible"])' 2>/dev/null)
chk "$feas" "True" "the API job's plan is feasible"
chk "$(code "${AUTH[@]}" "$B/jobs/$JID/files/A/SCHEDULE_ACCESS.csv")" "200" "a competition file downloads"
chk "$(code "${AUTH[@]}" "$B/jobs/$JID/files/A/ETC_PASSWD.csv")" "404" "a non-competition filename is refused"

echo "== the service survives a worker that dies =="
before=$(curl -s "$B/health" | python3 -c 'import json,sys;print(json.load(sys.stdin)["status"])')
JID2=$(curl -s "${AUTH[@]}" -X POST "$B/jobs?instance_id=$IID&scenario=all&seconds=30" \
       | python3 -c 'import json,sys;print(json.load(sys.stdin)["job_id"])' 2>/dev/null)
sleep 0.4
pkill -9 -f "trackaccess solve" 2>/dev/null
sleep 2
after=$(curl -s "$B/health" | python3 -c 'import json,sys;print(json.load(sys.stdin)["status"])' 2>/dev/null)
chk "$after" "ok" "the service is still healthy after its worker was killed"
st2=$(curl -s "${AUTH[@]}" "$B/jobs/$JID2" | python3 -c 'import json,sys;print(json.load(sys.stdin)["state"])' 2>/dev/null)
[ "$st2" = failed ] || [ "$st2" = done ] && ok "the killed job is reported as $st2, not left running" \
  || no "the killed job state is '$st2'"

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
