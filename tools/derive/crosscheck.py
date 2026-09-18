#!/usr/bin/env python3
"""Independent Python re-check of a submission directory.

Deliberately a separate implementation from the C++ validator: it shares no code
with it, so agreement between the two is evidence rather than tautology. The rule
semantics encoded here are the ones derived in docs/DERIVED_RULES.md and shown to
accept the official 03_submission_sample/ with zero violations.

usage: crosscheck.py <instance_dir> <submission_dir>
"""
import csv, collections, datetime as dt, itertools, sys

def rd(p): return list(csv.DictReader(open(p)))

def main(D, S):
    D = D.rstrip('/') + '/'; S = S.rstrip('/') + '/'
    acts = {r['activity_id']: r for r in rd(D+'08_ACTIVITY_DETAILS.csv')}
    cons = {r['contract_number']: r for r in rd(D+'07_PROJECT_DETAILS.csv')}
    sup  = {r['location_id']: int(r['supply_capacity']) for r in rd(D+'04_LOCATION_SUPPLY.csv')}
    bufcfg = {r['nature_of_works']: (int(r['up_to_buffer_sectors']),
                                     r['opposite_bound_required'] == '1')
              for r in rd(D+'05_BUFFER_LOCATION.csv')}
    par = {r['key']: r['value'] for r in rd(D+'06_PARAMETERS.csv')}
    H = dt.date.fromisoformat(par['horizon_start'])

    order, secorder = collections.defaultdict(list), collections.defaultdict(list)
    for r in sorted(rd(D+'02_STATIONS.csv'), key=lambda x: int(x['seq'])):
        order[r['line_code']].append(r['station_id'])
    for r in sorted(rd(D+'03_SECTORS.csv'), key=lambda x: int(x['seq'])):
        secorder[r['line_code']].append(r['sector_id'])

    def chain(line, bound):
        ch = []
        for i, s in enumerate(order[line]):
            ch.append(f'PLAT:{line}:{s}:{bound}')
            if i < len(secorder[line]): ch.append(f'{secorder[line][i]}:{bound}')
        return ch
    CH = {(l, b): chain(l, b) for l in order for b in ('EB', 'WB')}
    IDX = {k: {loc: i for i, loc in enumerate(v)} for k, v in CH.items()}
    nature = lambda a: cons[acts[a]['contract_number']]['nature_of_activity']
    atype  = lambda a: cons[acts[a]['contract_number']]['access_type']

    def expand(a):
        s, e = acts[a]['start_location_id'], acts[a]['end_location_id']
        _, line, sp, bd = s.split(':'); _, _, ep, _ = e.split(':')
        lst = secorder[line]
        i, j = lst.index(f'SEC:{line}:{sp}'), lst.index(f'SEC:{line}:{ep}')
        if i > j: i, j = j, i
        secs = [f'{x}:{bd}' for x in lst[i:j+1]]
        a0 = lst[i].split(':')[2].split('_')[0]; b0 = lst[j].split(':')[2].split('_')[1]
        st = order[line]
        plats = [f'PLAT:{line}:{x}:{bd}' for x in st[st.index(a0):st.index(b0)+1]]
        return set(secs + plats)

    def zones(a, locs):
        """Returns (closure, buffer_zone).

        closure  = worksite, plus for Live the mirrored opposite bound and the
                   exact cross-line interchange set. Excludes every other activity.
        buffer   = closure grown by up_to_buffer_sectors sectors (the crossing is
                   not itself grown). Pushes only other buffer-carrying work.
        """
        r, mirror = bufcfg[nature(a)]
        base = set(locs); line = next(iter(locs)).split(':')[1]
        if mirror:
            opp = {'EB': 'WB', 'WB': 'EB'}
            base |= {':'.join(p[:3]+[opp[p[3]]]) for p in (x.split(':') for x in list(base))}
        grown = set(base)
        if r > 0:
            spans = {(l.split(':')[1], l.split(':')[3]) for l in base}
            for (ln, b) in spans:
                ch, idx = CH[(ln, b)], IDX[(ln, b)]
                mine = [idx[l] for l in base if l.endswith(':'+b) and l.split(':')[1] == ln]
                if not mine: continue
                need = r
                for k in range(min(mine)-1, -1, -1):
                    grown.add(ch[k])
                    if ch[k].startswith('SEC:'):
                        need -= 1
                        if need == 0: break
                need = r
                for k in range(max(mine)+1, len(ch)):
                    grown.add(ch[k])
                    if ch[k].startswith('SEC:'):
                        need -= 1
                        if need == 0: break
        cross = set()
        if mirror and any('H01_H02' in l or ':H01:' in l or ':H02:' in l for l in base):
            o = 'BET' if line == 'ALP' else 'ALP'
            for b in ('EB', 'WB'):
                cross |= {f'SEC:{o}:H01_H02:{b}', f'PLAT:{o}:H01:{b}', f'PLAT:{o}:H02:{b}'}
        return base | cross, grown | cross

    has_buffer = lambda a: bufcfg[nature(a)][0] > 0

    acc = rd(S+'SCHEDULE_ACCESS.csv'); occ = rd(S+'SCHEDULE_OCCUPANCY.csv')
    res = rd(S+'RESULTS.csv')
    scen = {r['scenario'] for r in res}
    V = []
    if len(scen) != 1: V.append(f'RESULTS.csv mixes scenarios: {scen}')
    scenario = res[0]['scenario']

    # 1 workload (tenths, exact)
    y = collections.Counter()
    for r in acc: y[r['activity_id']] += 15 if r['eclo'] == '1' else 10
    for a in acts:
        if y[a] < int(acts[a]['total_accesses'])*10:
            V.append(f'workload: {a} has {y[a]/10} of {acts[a]["total_accesses"]}')
    # 2 one access per activity-week
    for k, n in collections.Counter((r['activity_id'], r['week']) for r in acc).items():
        if n > 1: V.append(f'allocation: {k[0]} has {n} accesses in wk{k[1]}')
    # 3 planned start
    for r in acc:
        pw = (dt.date.fromisoformat(acts[r['activity_id']]['planned_start_date']) - H).days//7 + 1
        if int(r['week']) < pw:
            V.append(f'planned_start: {r["activity_id"]} wk{r["week"]} < {pw}')
    # 4 precedence
    first = collections.defaultdict(lambda: 10**9); last = collections.defaultdict(int)
    for r in acc:
        w = int(r['week']); a = r['activity_id']
        first[a] = min(first[a], w); last[a] = max(last[a], w)
    for a in acts:
        p = acts[a]['predecessor_activity_id']
        if p and a in first and p in last and first[a] <= last[p]:
            V.append(f'precedence: {a} first wk{first[a]} not after {p} last wk{last[p]}')
    # 5 occupancy == expansion
    booked = collections.defaultdict(set); grp = {}
    for r in occ:
        booked[(r['activity_id'], r['week'])].add(r['location_id'])
        grp[(r['activity_id'], r['week'], r['location_id'])] = r['co_share_group']
    for (a, w), locs in booked.items():
        if locs != expand(a): V.append(f'occupancy: {a} wk{w} span mismatch')
    # 6 capacity
    tol = {'A': 0, 'C': 1}.get(scenario, 10**9)
    slots = collections.defaultdict(set)
    for r in occ: slots[(r['location_id'], r['week'])].add(r['co_share_group'])
    excess = 0
    for (l, w), g in slots.items():
        ex = len(g) - sup[l]
        if ex > 0:
            excess += ex
            if ex > tol: V.append(f'capacity: {l} wk{w} uses {len(g)} > {sup[l]}')
    # 7 legal mix
    mix = collections.defaultdict(list)
    for r in occ: mix[(r['location_id'], r['week'], r['co_share_group'])].append(atype(r['activity_id']))
    for k, v in mix.items():
        c = collections.Counter(v)
        if not ((c['PM'] == 1 and len(v) == 1) or (c['PM'] == 0 and c['PC'] <= 1 and len(v) <= 4)):
            V.append(f'mix: {k} -> {dict(c)}')
    # 8/9 allocation + workfronts
    nights = collections.defaultdict(set); onn = collections.defaultdict(set)
    for r in acc:
        cn = acts[r['activity_id']]['contract_number']
        nights[(cn, r['week'])].add(r['access_night'])
        onn[(cn, r['week'], r['access_night'])].add(r['activity_id'])
    for (cn, w), ns in nights.items():
        if len(ns) > int(cons[cn]['number_of_maximum_access_per_week']):
            V.append(f'allocation: {cn} wk{w} uses {len(ns)} nights')
    for (cn, w, n), s in onn.items():
        if len(s) > int(cons[cn]['number_of_workfronts']):
            V.append(f'workfront: {cn} wk{w} night{n} runs {len(s)}')
    # 10 closure / buffer
    byw = collections.defaultdict(dict)
    for (a, w), locs in booked.items(): byw[w][a] = locs
    for w, d in byw.items():
        for a, b in itertools.combinations(sorted(d), 2):
            if d[a] & d[b]: continue          # settled by co_share_group
            ca, ba = zones(a, d[a]); cb, bb = zones(b, d[b])
            bad = bool((ca & d[b]) or (cb & d[a]))
            if has_buffer(a) and has_buffer(b):
                bad = bad or bool((ba & d[b]) or (bb & d[a]))
            if bad: V.append(f'closure: wk{w} {a} vs {b}')
    # 11 eclo
    ecl = [r for r in acc if r['eclo'] == '1']
    if scenario == 'A' and ecl: V.append(f'eclo: {len(ecl)} ECLO nights in Scenario A')
    if scenario == 'C':
        span = collections.defaultdict(list)
        for r in ecl:
            for l in zones(r['activity_id'], booked[(r['activity_id'], r['week'])])[0]:
                span[l.split(':')[1]].append(int(r['week']))
        for ln, ws in span.items():
            if max(ws)-min(ws)+1 > 2: V.append(f'eclo: line {ln} window {min(ws)}..{max(ws)}')
    # 12 scenario B planned dates
    clast = collections.defaultdict(int)
    for r in acc: clast[acts[r['activity_id']]['contract_number']] = max(
        clast[acts[r['activity_id']]['contract_number']], int(r['week']))
    over_total = 0
    for cn, w in clast.items():
        sim = H + dt.timedelta(days=(w-1)*7+6)
        ov = max(0, (sim - dt.date.fromisoformat(cons[cn]['planned_completion_date'])).days)
        over_total += ov
        if scenario == 'B' and ov > 0: V.append(f'planned_date: {cn} overruns {ov}d')
    # RESULTS.csv agreement
    for r in res:
        cn = r['contract_number']; w = clast[cn]
        sim = (H + dt.timedelta(days=(w-1)*7+6)).isoformat()
        if r['simulated_completion_date'] != sim:
            V.append(f'results: {cn} says {r["simulated_completion_date"]}, recomputed {sim}')
    # weighted score
    wt = {'1': 100, '2': 10, '3': 1}; nudge = {'1': 13, '2': 12, '3': 10}
    tenths = 0
    for a in acts:
        if a not in last: continue
        c = cons[acts[a]['contract_number']]
        sim = H + dt.timedelta(days=(last[a]-1)*7+6)
        ov = max(0, (sim - dt.date.fromisoformat(c['planned_completion_date'])).days)
        tenths += wt[c['contract_priority']]*nudge[acts[a]['activity_priority']]*ov
    obj = {'A': tenths, 'B': 10*(7*excess+5*len(ecl)),
           'C': tenths+10*(7*excess+5*len(ecl))}[scenario]
    print(f'scenario {scenario}: {"FEASIBLE" if not V else str(len(V))+" VIOLATIONS"}'
          f'  overrun_days={over_total} excess={excess} eclo={len(ecl)}'
          f'  weighted={tenths/10} objective={obj/10}')
    for v in V[:15]: print('   ', v)
    return 0 if not V else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
