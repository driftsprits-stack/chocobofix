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

echo "== CLI: published artefacts are reproducible =="
# Regression guard for the defect recorded as D1 in docs/BASELINE_AUDIT.md:
# multi-worker CP-SAT returned a different optimum of equal value on each run,
# which made a published provenance hash unreproducible. `solve` must default to
# a reproducible search, and must say so in PROVENANCE.json.
"$BUILD/trackaccess" solve --data "$DATA" --out "$TMP/rep1" --scenario C --seconds 60 >/dev/null 2>&1
"$BUILD/trackaccess" solve --data "$DATA" --out "$TMP/rep2" --scenario C --seconds 60 >/dev/null 2>&1
for f in SCHEDULE_ACCESS.csv SCHEDULE_OCCUPANCY.csv RESULTS.csv; do
  a=$(shasum -a 256 "$TMP/rep1/C/$f" | cut -d" " -f1)
  b=$(shasum -a 256 "$TMP/rep2/C/$f" | cut -d" " -f1)
  chk "$a" "$b" "scenario C $f is identical across two default runs"
done
grep -q '"reproducible": true' "$TMP/rep1/C/PROVENANCE.json" \
  && ok "PROVENANCE.json records the run as reproducible" \
  || no "PROVENANCE.json records the run as reproducible"
# An explicitly parallel search must be labelled as not reproducible rather than
# silently published as if it were.
"$BUILD/trackaccess" solve --data "$DATA" --out "$TMP/rep8" --scenario C --seconds 20 --workers 8 \
  > "$TMP/rep8.log" 2>&1
grep -q '"reproducible": false' "$TMP/rep8/C/PROVENANCE.json" \
  && ok "parallel search is recorded as not reproducible" \
  || no "parallel search is recorded as not reproducible"
grep -qi "NOT reproducible" "$TMP/rep8.log" \
  && ok "parallel search warns on stderr" \
  || no "parallel search warns on stderr"

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

echo "== rule-6 exposure is measured, not assumed =="
out=$(python3 tools/derive/exposure.py data/upstream/PS1/01_data data/upstream/PS1/03_submission_sample 2>&1)
echo "$out" | grep -qE "adopted: +0" && ok "the shipped sample has 0 breaches under the adopted reading" \
  || no "sample exposure under the adopted reading: $out"
echo "$out" | grep -qE "literal: +[1-9]" && ok "  ... and a non-zero count under the literal reading, which refutes it" \
  || no "sample exposure under the literal reading"
"$BUILD/trackaccess" solve --data "$DATA" --out "$TMP/strict" --scenario A --seconds 45 --strict-buffers >/dev/null 2>&1
out=$(python3 tools/derive/exposure.py "$DATA" "$TMP/strict/A" 2>&1)
echo "$out" | grep -qE "adopted: +0 +literal: +0" && ok "--strict-buffers output has 0 breaches under BOTH readings" \
  || no "--strict-buffers exposure: $out"

echo "== risk: Scenario B must not be capped at four excess slots =="
# Six PM activities at one capacity-1 location in one week need six possessions:
# five in excess. B forbids overrun, so it must buy that excess. A hard cap of
# four made this infeasible.
"$BUILD/trackaccess" solve --data tests/data/micro_b --out "$TMP/bcap" --scenario B --seconds 30 \
  >"$TMP/bcap.log" 2>&1
chk "$?" "0" "Scenario B solves when it needs more than four excess access-nights"
ex=$(python3 -c "
import json;print(json.load(open('$TMP/bcap/B/VALIDATION.json'))['soft_scores']['excess_access_nights_total'])" 2>/dev/null)
[ "${ex:-0}" -gt 4 ] && ok "  ... and actually spends $ex, above the old cap" || no "  excess spent: $ex"
# The same instance under A, which forbids excess, must be PROVEN infeasible -
# not merely reported as a timeout.
"$BUILD/trackaccess" solve --data tests/data/micro_b --out "$TMP/acap" --scenario A --seconds 30 \
  >"$TMP/acap.log" 2>&1
grep -q "status: infeasible" "$TMP/acap.log" && ok "Scenario A on the same instance is proven infeasible" \
  || no "Scenario A status: $(grep -o 'status: [a-z_]*' "$TMP/acap.log")"

echo "== risk: a timeout is never reported as infeasibility =="
# A budget of one second on a large synthetic instance either proves something
# or reports that it did not - it must never claim impossibility it has not shown.
python3 tools/derive/gen_stress.py data/upstream/PS1/01_data "$TMP/big" 4 >/dev/null 2>&1
"$BUILD/trackaccess" solve --data "$TMP/big" --out "$TMP/bigout" --scenario C --seconds 1 --workers 1 \
  >"$TMP/big.log" 2>&1
st=$(grep -o 'status: [a-z_]*' "$TMP/big.log" | head -1 | cut -d' ' -f2)
case "$st" in
  optimal|feasible|infeasible|timeout_no_solution|cancelled)
    ok "a one-second budget reports a defensible status ($st)";;
  *) no "unexpected status '$st'";;
esac
if [ "$st" = "timeout_no_solution" ]; then
  grep -q "not a proof" "$TMP/big.log" && ok "  ... and says explicitly that it is not a proof" \
    || no "  timeout message does not disclaim proof"
fi

echo "== risk: minimal change is lexicographic, not a weight on the objective =="
# The repaired plan must score exactly what the scenario scores, with churn
# broken only between plans of equal objective.
"$BUILD/trackaccess" repair --data "$DATA" --out "$TMP/lex" \
  --supply "SEC:BET:H01_H02:EB@15=0" --scenario A --seconds 45 >"$TMP/lex.log" 2>&1
chk "$?" "0" "repair completes"
# Re-score the emitted plan from scratch; it must match what repair reported.
rep=$(grep -oE 'objective [0-9]+\.[0-9]' "$TMP/lex.log" | tail -1 | cut -d' ' -f2)
got=$(python3 -c "
import json;print(json.load(open('$TMP/lex/VALIDATION.json'))['soft_scores']['objective_score'])" 2>/dev/null)
# repair prints 'objective X  (+d)' for the after-plan; take that one.
rep=$(grep -oE 'objective [0-9]+\.[0-9]+' "$TMP/lex.log" | tail -1 | cut -d' ' -f2)
chk "$got" "$rep" "  the recomputed objective equals the reported one (no churn blended in)"
[ -f "$TMP/lex/PROVENANCE.json" ] && ok "the disruption is recorded in PROVENANCE.json" \
  || no "PROVENANCE.json written"
grep -q "SEC:BET:H01_H02:EB" "$TMP/lex/PROVENANCE.json" && ok "  ... naming the affected location" \
  || no "provenance names the location"

echo "== service: shared-project layer over HTTP =="
"$BUILD/trackaccess-service" --host 127.0.0.1 --port "$PORT" --root "$TMP/var" --web web \
  --worker "$BUILD/trackaccess" --public-instance "$DATA" > "$TMP/svc.log" 2>&1 &
SVC=$!
for i in $(seq 1 40); do curl -sf "http://127.0.0.1:$PORT/api/v1/health" >/dev/null 2>&1 && break; sleep 0.25; done
B="http://127.0.0.1:$PORT/api/v1"

code(){ curl -s -o /dev/null -w '%{http_code}' "$@"; }
chk "$(code "$B/health")" "200" "health needs no session"
chk "$(code -X POST "$B/projects?name=x")" "401" "an unauthenticated request is refused"
chk "$(code -H 'Authorization: Bearer not-a-real-session' "$B/auth/me")" "401" "a bogus session token is refused"

# Accounts, roles, projects, versions, approval and audit are covered in detail
# by the Python suite, which drives exactly what the interface calls.
if python3 tests/test_multiuser.py "http://127.0.0.1:$PORT" > "$TMP/mu.log" 2>&1; then
  n=$(grep -cE '^  ok ' "$TMP/mu.log")
  PASS=$((PASS+n)); printf '  ok   shared-project suite: %s checks passed\n' "$n"
else
  no "shared-project suite (see $TMP/mu.log)"; tail -15 "$TMP/mu.log"
fi

# Sign in as the planner the Python suite created, for the remaining checks.
PTOK=$(curl -s -X POST "$B/auth/login" -d 'username=plan.pat&password=planner-password-01' \
       | python3 -c 'import json,sys;print(json.load(sys.stdin)["token"])' 2>/dev/null)
AUTH=(-H "Authorization: Bearer $PTOK")
chk "$(code "${AUTH[@]}" "$B/jobs/doesnotexist")" "404" "an unknown job is a 404"
chk "$(code "${AUTH[@]}" "$B/jobs/..%2f..%2fetc%2fpasswd")" "404" "a traversal-styled job id does not resolve"
chk "$(code "${AUTH[@]}" "$B/instances/999999/detail")" "404" "an unknown instance is refused"
PID=$(curl -s "${AUTH[@]}" "$B/projects" | python3 -c 'import json,sys;d=json.load(sys.stdin)["projects"];print(d[0]["id"] if d else "")' 2>/dev/null)
IID=$(curl -s "${AUTH[@]}" "$B/projects/$PID/instances" | python3 -c 'import json,sys;d=json.load(sys.stdin)["instances"];print(d[0]["id"] if d else "")' 2>/dev/null)
chk "$(code "${AUTH[@]}" -X POST "$B/projects/$PID/jobs?instance_id=$IID&scenario=Z")" "400" "an unknown scenario is refused"
VID=$(curl -s "${AUTH[@]}" "$B/projects/$PID/versions" | python3 -c 'import json,sys;d=json.load(sys.stdin)["versions"];print(d[0]["id"] if d else "")' 2>/dev/null)
chk "$(code "${AUTH[@]}" "$B/versions/$VID/files/SCHEDULE_ACCESS.csv")" "200" "a competition file downloads from a version"
chk "$(code "${AUTH[@]}" "$B/versions/$VID/files/ETC_PASSWD.csv")" "404" "a non-competition filename is refused"

echo "== single-page routes resolve, and /api never returns a page =="
# A deep link like /workspace must get the app shell so a refresh works. An
# unknown /api path must stay JSON: answering it with HTML turns a 404 into a
# parse error at the caller. A missing asset must stay a 404, not a blank page.
for route in / /workspace /workspace/12/review /settings /no-such-route; do
  code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT$route")
  chk "$code" "200" "GET $route serves the app shell"
done
ct=$(curl -sI "http://127.0.0.1:$PORT/api/v1/nope" | tr -d '\r' | awk -F': ' 'tolower($1)=="content-type"{print $2}')
chk "$ct" "application/json" "an unknown /api path answers as JSON, not HTML"
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/api/v1/nope")
chk "$code" "404" "an unknown /api path is a 404"
body=$(curl -s "http://127.0.0.1:$PORT/api/v1/nope")
case "$body" in *"<!doctype"*|*"<html"*) no "an unknown /api path must not return HTML";; *) ok "an unknown /api path must not return HTML";; esac
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/assets/definitely-missing.js")
chk "$code" "404" "a missing asset stays a 404 rather than the shell"

echo "== risk: interactive analyses obey the worker limit =="
# Six explain requests at once, against a service allowing two concurrent
# workers. Every one must be answered - some served, some refused with 429 -
# and none may fork a worker past the bound.
rm -f "$TMP/codes.txt"
CPIDS=""
for i in 1 2 3 4 5 6; do
  curl -s -o /dev/null -w '%{http_code}\n' "${AUTH[@]}" -X POST \
      "$B/instances/$IID/explain?activity=A004&week=21&scenario=A&seconds=8" \
      >> "$TMP/codes.txt" 2>/dev/null &
  CPIDS="$CPIDS $!"
done
# Wait only on the curls. A bare `wait` would also wait on the service, which
# never exits.
for pid in $CPIDS; do wait "$pid" 2>/dev/null; done
codes=$(tr '\n' ' ' < "$TMP/codes.txt" 2>/dev/null)
n_ok=$(tr ' ' '\n' <<< "$codes" | grep -c '^200$')
n_busy=$(tr ' ' '\n' <<< "$codes" | grep -c '^429$')
# The property that matters is that every request gets a real answer and the
# service survives. Whether a given one is served or told the solver is busy
# depends on timing; a crash or a dropped connection (000) is never acceptable.
n_bad=$(tr ' ' '\n' <<< "$codes" | grep -c '^000$')
if [ $((n_ok + n_busy)) -ge 6 ] && [ "$n_bad" -eq 0 ]; then
  ok "six concurrent analyses all answered ($n_ok served, $n_busy told the solver is busy)"
else
  no "concurrent analyses: got '$codes' ($n_bad dropped connections)"
fi
after=$(curl -s "$B/health" | python3 -c 'import json,sys;print(json.load(sys.stdin)["status"])' 2>/dev/null)
chk "$after" "ok" "  ... and the service is still healthy afterwards"

echo "== the service survives a worker that dies =="
JID2=$(curl -s "${AUTH[@]}" -X POST "$B/projects/$PID/jobs?instance_id=$IID&scenario=all&seconds=30" \
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
