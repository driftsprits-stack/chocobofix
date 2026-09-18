# Three-minute demonstration script

**Not recorded and not uploaded.** This is the script to record from.

Audience framing: a works controller at 02:00 who must decide whether a plan is
safe to dispatch, not a software audience.

Record at 1920×1080. Use the bundled public instance so figures are reproducible.
Before recording: `./build/trackaccess-service --host 127.0.0.1 --port 8080
--auth none --root ./var --web ./web --worker ./build/trackaccess
--public-instance ./data/upstream/PS1/01_data`

---

### 0:00–0:20 · The problem

> Every night between the last train and the first, a few hours exist for work on
> the railway. Every contractor wants them. Alpha and Beta share one tunnel at the
> interchange, live-rail work closes the opposite bound too, and every possession
> carries its own safety buffer. This is the tool that decides who gets the track.

Show the Network tab, week slider moving. No slides.

### 0:20–0:45 · Import, and being told exactly what is wrong

Drop the eight CSVs. Then drop a deliberately broken `06_PARAMETERS.csv`.

> It names the file, the row and the field. A planner fixes the data, not the tool.

Reload the good instance: 54 activities, 14 contracts, 30 weeks, and the input
hash that binds every later result to these exact bytes.

### 0:45–1:15 · Generate, with the policy in plain language

Select A + B + C. Read the on-screen explanation of Scenario A aloud:

> Supply is rigid. No extra nights, no early closure. The only lever is letting
> work finish later — so it protects Priority 1 contracts first.

Press Generate. It finishes in under a second.

> All three scenarios, each proven optimal. Not "the best we found" — proven, for
> this model of the rules.

### 1:15–1:50 · The consequence of a decision, before committing

Schedule tab. Point at the contract table: C006 is fourteen days late, everything
else on time. Point at the timeline: red marks work past its planned date.

Click one activity.

> This is the part a controller actually needs. A004 books five locations — two
> tunnel sectors and three platforms, including both ends. Its buffer additionally
> closes one sector either side. It cannot start until A003 has finished, in a
> strictly later week.

Click through to the Network tab with that activity still selected; the schematic
highlights its worksite and its buffer.

### 1:50–2:20 · Proving the answer

Check tab.

> The plan is checked by a separately written checker that does not reuse the
> solver's constraints — so a mistake in the solver cannot hide here. Zero hard
> violations. And it says plainly that this is our checker, not the official
> reference validator, which is not published with the problem.

Mention the corroboration: the same checker accepts the organisers' own sample
submission as feasible, and scores it 48.3 where our Scenario A scores 32.2.

### 2:20–2:40 · The consequence of a decision, before committing

Repair tab. Close `SEC:BET:H01_H02:EB` — the Beta interchange tunnel — for weeks
15 and 16, as if urgent maintenance had taken it.

> Nine point one more penalty, a week more overrun, five activities moved. That is
> what this disruption costs, and a controller sees it before accepting it. The
> unaffected work stays where it was.

Then, on an activity: ask why it cannot run a week earlier.

> Proven impossible — and it names the rule: its predecessor has not finished.
> Not a guess, and not a timeout dressed up as a no.

### 2:40–2:50 · Export, and the honest failure modes

Export tab, download the three files.

> These are the bytes that were validated, not a re-render.

Briefly show the terminal:

```
trackaccess diagnose --data <dense-instance> --scenario A
```

> When an instance genuinely cannot be scheduled, it says which rule binds — and
> it never reports a timeout as impossibility. Those are different answers and a
> controller needs to know which one they have.

### 2:50–3:00 · Close

> Four languages, keyboard throughout, no cloud dependency — it runs with the
> network cable out. A planner sees the consequences of a decision before
> committing to it.

---

**Do not claim on camera:** that the official validator passed it (it was never
available), that it is deployed (it is not), or that any figure came from
anything other than this run.
