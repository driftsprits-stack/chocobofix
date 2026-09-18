#!/usr/bin/env python3
"""Measures a submission's exposure to the two competing readings of rule 6.

See docs/DERIVED_RULES.md R6c. The brief's rule 6 contradicts itself about
whether sharing a location settles a pair of activities. This reports, for each
submission given, how many activity pairs breach the rule under each reading:

  adopted - sharing any location settles the pair (what we ship by default)
  literal - "buffers apply normally between them" taken at face value

Only the part of a zone OUTSIDE its owner's own worksite can intrude on another
activity; overlapping worksites are governed by capacity and the legal mix.

usage: exposure.py [instance_dir submission_dir ...]
       exposure.py                 # the bundled sample and out/public*
"""
import csv, collections, itertools, sys
D=(sys.argv[1].rstrip('/')+'/') if len(sys.argv)>2 else 'data/upstream/PS1/01_data/'
rd=lambda p: list(csv.DictReader(open(p)))
acts={r['activity_id']:r for r in rd(D+'08_ACTIVITY_DETAILS.csv')}
cons={r['contract_number']:r for r in rd(D+'07_PROJECT_DETAILS.csv')}
bufcfg={r['nature_of_works']:(int(r['up_to_buffer_sectors']),r['opposite_bound_required']=='1')
        for r in rd(D+'05_BUFFER_LOCATION.csv')}
order=collections.defaultdict(list); secorder=collections.defaultdict(list)
for r in sorted(rd(D+'02_STATIONS.csv'),key=lambda x:int(x['seq'])): order[r['line_code']].append(r['station_id'])
for r in sorted(rd(D+'03_SECTORS.csv'),key=lambda x:int(x['seq'])): secorder[r['line_code']].append(r['sector_id'])
def chain(l,b):
    ch=[]
    for i,s in enumerate(order[l]):
        ch.append(f'PLAT:{l}:{s}:{b}')
        if i<len(secorder[l]): ch.append(f'{secorder[l][i]}:{b}')
    return ch
CH={(l,b):chain(l,b) for l in order for b in ('EB','WB')}
IDX={k:{x:i for i,x in enumerate(v)} for k,v in CH.items()}
nature=lambda a: cons[acts[a]['contract_number']]['nature_of_activity']
has_buf=lambda a: bufcfg[nature(a)][0]>0

def zones(a,locs):
    r,mirror=bufcfg[nature(a)]
    base=set(locs); line=next(iter(locs)).split(':')[1]
    if mirror:
        opp={'EB':'WB','WB':'EB'}
        base|={':'.join(p[:3]+[opp[p[3]]]) for p in (x.split(':') for x in list(base))}
    grown=set(base)
    if r>0:
        for (ln,b) in {(l.split(':')[1],l.split(':')[3]) for l in base}:
            ch,idx=CH[(ln,b)],IDX[(ln,b)]
            mine=[idx[l] for l in base if l.endswith(':'+b) and l.split(':')[1]==ln]
            if not mine: continue
            need=r
            for k in range(min(mine)-1,-1,-1):
                grown.add(ch[k])
                if ch[k].startswith('SEC:'):
                    need-=1
                    if need==0: break
            need=r
            for k in range(max(mine)+1,len(ch)):
                grown.add(ch[k])
                if ch[k].startswith('SEC:'):
                    need-=1
                    if need==0: break
    cross=set()
    if mirror and any('H01_H02' in l or ':H01:' in l or ':H02:' in l for l in base):
        o='BET' if line=='ALP' else 'ALP'
        for b in ('EB','WB'):
            cross|={f'SEC:{o}:H01_H02:{b}',f'PLAT:{o}:H01:{b}',f'PLAT:{o}:H02:{b}'}
    return base|cross, grown|cross

def analyse(tag, S, show=0):
    occ=rd(S+'/SCHEDULE_OCCUPANCY.csv')
    booked=collections.defaultdict(set); grp={}
    for r in occ:
        booked[(r['activity_id'],r['week'])].add(r['location_id'])
        grp[(r['activity_id'],r['week'],r['location_id'])]=r['co_share_group']
    byw=collections.defaultdict(dict)
    for (a,w),locs in booked.items(): byw[w][a]=locs
    adopted=0; literal=0; examples=[]
    for w,d in byw.items():
        for a,b in itertools.combinations(sorted(d),2):
            A,B=d[a],d[b]
            ca,ba=zones(a,A); cb,bb=zones(b,B)
            # Only the part of a zone OUTSIDE its owner's own worksite can
            # intrude. Overlapping worksites are governed by capacity and the
            # legal mix, not by the closure rule.
            def beyond(zone, own, other): return bool((zone-own) & other)
            clash = beyond(ca,A,B) or beyond(cb,B,A)
            if has_buf(a) and has_buf(b):
                clash = clash or beyond(ba,A,B) or beyond(bb,B,A)
            if not clash: continue
            literal += 1                      # the literal reading always bites
            if not (A & B):
                adopted += 1                  # adopted only when no shared location
            elif len(examples) < show:
                examples.append((w,a,nature(a)[:14],b,nature(b)[:14],
                                 sorted((ba-A)&B | (bb-B)&A | (ca-A)&B | (cb-B)&A)[:2]))
    print(f'{tag:34} adopted: {adopted:3}   literal: {literal:3}')
    for e in examples: print('      ', e)
    return adopted, literal




if __name__ == '__main__':
    import os
    if len(sys.argv) > 2:
        for sub in sys.argv[2:]:
            analyse(os.path.basename(sub.rstrip('/')), sub, show=3)
    else:
        print('Exposure to the two readings of rule 6 (docs/DERIVED_RULES.md R6c)\n')
        analyse('upstream sample', 'data/upstream/PS1/03_submission_sample', show=2)
        for s in 'ABC':
            if os.path.isdir(f'out/public/{s}'): analyse(f'ours, scenario {s}', f'out/public/{s}')
        for s in 'ABC':
            if os.path.isdir(f'out/public-strict/{s}'): analyse(f'ours --strict-buffers, {s}', f'out/public-strict/{s}')
