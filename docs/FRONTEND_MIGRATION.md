# Frontend migration: `web/` → `client/`

**Decision.** The React application in `client/` is the primary interface. The
vanilla interface in `web/` is superseded and is no longer what ships.

## Why both still exist

`web/` is a complete, working interface with a larger translated vocabulary than
the React client currently carries. Deleting it in the same change that
introduces the React client would remove the only working fallback before the
replacement had been exercised. It stays in the repository, and it is *not*
copied into the container image.

## What ships

| Thing | Path | Shipped |
| --- | --- | --- |
| React source | `client/src` | built, not shipped raw |
| React build output | `web-dist/` | **yes** — `COPY --from=client /web-dist ./web` |
| Vanilla interface | `web/` | no |

The service takes `--web <dir>` and serves whatever is there, so both can be run
locally for comparison:

```bash
./build/trackaccess-service --web web-dist   # React (what ships)
./build/trackaccess-service --web web        # superseded interface
```

## Routes

`client/src/App.jsx` declares `/`, `/signin`, `/projects`, `/projects/:pid`,
`/projects/:pid/upload`, `/schedules`, `/sample`, `/terms`, `/settings`, and a
catch-all `*` that renders `NotFound.jsx`.

`/workspace` and `/workspace/:pid` are kept as redirects to `/projects` and
`/projects/:pid`, so links handed out before the split still resolve.

Schedule filters live in the query string (`?project=&person=&q=&view=&group=`),
so a filtered view can be shared and survives a refresh. Opening or refreshing any of them works: the service answers an
unmatched non-`/api` GET with `index.html` (see `set_error_handler` in
`src/service/main.cpp`). `/api` paths are never answered with HTML, and a
missing asset stays a 404. Nine checks in `tests/integration.sh` cover this.

## Translation coverage — carried forward, not complete

`web/i18n.js` holds a large translated vocabulary for English, Malay, Simplified
Chinese and Tamil. `client/src/lib/i18n.js` currently covers the Settings screen
in all four. **The rest of the React interface is English only.**

The non-English strings in the React client were written without a native
reviewer and are flagged in the interface itself: choosing Malay, Chinese or
Tamil in Settings shows a notice that the translation needs review. That flag is
`NEEDS_REVIEW` in `client/src/lib/i18n.js` and must stay until a speaker signs
each language off.

**Next step for translation:** port the keys from `web/i18n.js` that the React
screens need, then have each language checked. Do not machine-translate the
scheduling vocabulary — `access night`, `possession`, `co-share group` and
`workfront` are domain terms, and a wrong word changes what an operator believes
the plan says.

## What was deliberately not carried over

- `chocobofix.css` / `workbench.css` — the rounded-card visual system. Replaced,
  not re-skinned.
- The query-string credential submission. See `docs/BASELINE_AUDIT.md`.


## Translation debt after the Projects/Schedules split (2026-09-19)

The new screens - Projects, project upload, project detail, Schedules, the
activity detail drawer and the sign-in rotator - are **English only**. Only
Settings is translated into the four languages.

This is a real regression in coverage relative to `web/i18n.js`, which carries a
much larger translated vocabulary. It is recorded here rather than hidden: the
new strings must be added to `client/src/lib/i18n.js` for `en`, `ms`, `zh` and
`ta`, and the non-English ones still need a native reviewer. Do not
machine-translate the scheduling vocabulary; `access night`, `possession`,
`co-share group` and `workfront` are domain terms.
