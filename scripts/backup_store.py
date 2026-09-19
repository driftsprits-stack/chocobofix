#!/usr/bin/env python3
"""Consistent offline backup and restore. Never writes over an existing store.
Stop the service first. SQLite online backup alone does not snapshot artifacts.
"""
import argparse, hashlib, json, shutil, sqlite3
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()


def manifest(root):
    return {str(p.relative_to(root)): digest(p) for p in sorted(root.rglob('*'))
            if p.is_file() and p.name != 'BACKUP_MANIFEST.json'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['backup', 'verify', 'restore'])
    parser.add_argument('source', type=Path)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--service-stopped', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve()
    if not source.is_dir(): parser.error('source must be a directory')
    if any(p.is_symlink() for p in source.rglob('*')): parser.error('symlinks are not supported')
    if args.mode in ['verify', 'restore']:
        recorded = json.loads((source / 'BACKUP_MANIFEST.json').read_text())
        if manifest(source) != recorded['files']: raise SystemExit('Backup verification failed.')
    if args.mode == 'verify': print('Backup hashes match.'); return
    if not args.service_stopped: parser.error('stop the service and pass --service-stopped')
    if not args.destination: parser.error('--destination is required')
    dest = args.destination.resolve()
    if dest.exists() or source == dest or source in dest.parents: parser.error('destination must be new and outside the source')
    shutil.copytree(source, dest)
    for db in dest.rglob('*.db'):
        with sqlite3.connect(str(db)) as conn:
            if conn.execute('PRAGMA integrity_check').fetchone()[0] != 'ok': raise SystemExit('Database integrity check failed.')
    if args.mode == 'backup':
        (dest / 'BACKUP_MANIFEST.json').write_text(json.dumps({'format':1,'files':manifest(dest)}, indent=2)+'\n')
    print(f'{args.mode} complete. Protect the destination with encryption and restricted access.')

if __name__ == '__main__': main()
