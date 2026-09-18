#!/usr/bin/env python3
"""Generates a larger synthetic instance by replicating the public demand book.

The network is unchanged; only the demand scales. Used for load testing, and
clearly labelled synthetic: it is not a competition instance and its results
say nothing about scoring.

usage: gen_stress.py <src_instance_dir> <dst_dir> <multiplier>
"""
import csv, shutil, sys, os

def main(src, dst, mult):
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(src):
        shutil.copy(os.path.join(src, f), os.path.join(dst, f))
    acts = list(csv.DictReader(open(os.path.join(src, '08_ACTIVITY_DETAILS.csv'))))
    cons = list(csv.DictReader(open(os.path.join(src, '07_PROJECT_DETAILS.csv'))))
    out_a, out_c = list(acts), list(cons)
    for k in range(1, mult):
        for c in cons:
            d = dict(c); d['contract_number'] = f"{c['contract_number']}X{k}"
            out_c.append(d)
        for a in acts:
            d = dict(a)
            d['activity_id'] = f"{a['activity_id']}X{k}"
            d['contract_number'] = f"{a['contract_number']}X{k}"
            d['predecessor_activity_id'] = (f"{a['predecessor_activity_id']}X{k}"
                                            if a['predecessor_activity_id'] else '')
            out_a.append(d)
    with open(os.path.join(dst, '08_ACTIVITY_DETAILS.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=acts[0].keys()); w.writeheader(); w.writerows(out_a)
    with open(os.path.join(dst, '07_PROJECT_DETAILS.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=cons[0].keys()); w.writeheader(); w.writerows(out_c)
    print(f'{dst}: {len(out_a)} activities, {len(out_c)} contracts')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]))
