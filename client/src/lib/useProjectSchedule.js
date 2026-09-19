import { useMemo } from 'react';
import { useResource } from './useResource.js';
import { api } from './api.js';
import { choosePlan } from './planChoice.js';

/** Minimal CSV reader for the emitted files; they carry no quoting. */
function parseCsv(text) {
  const lines = String(text).trim().split(/\r?\n/);
  if (!lines.length) return [];
  const head = lines[0].split(',');
  return lines.slice(1).filter(Boolean).map((l) => {
    const c = l.split(',');
    const o = {};
    head.forEach((h, i) => { o[h] = c[i]; });
    return o;
  });
}

/**
 * Everything one project contributes to the schedule view.
 *
 * Only fetched when `enabled` - the page loads project and plan summaries
 * first, and asks for a project's rows when it is actually expanded.
 */
export function useProjectSchedule(project, prefer, enabled, revision = 0) {
  const pid = project?.id;

  const versions = useResource((s) => api.versions(pid, s), [pid], { enabled: !!pid && enabled });
  const chosen = useMemo(
    () => choosePlan(versions.data?.versions, prefer),
    [versions.data, prefer]
  );

  const vid = chosen?.id;
  const iid = chosen?.instance_id;

  // Version files are immutable, so these are cached by version id.
  const access = useResource(
    (s) => api.versionFile(vid, 'SCHEDULE_ACCESS.csv', s), [vid], { enabled: !!vid && enabled });
  const occupancy = useResource(
    (s) => api.versionFile(vid, 'SCHEDULE_OCCUPANCY.csv', s), [vid], { enabled: !!vid && enabled });
  const detail = useResource(
    (s) => api.instanceDetail(iid, s), [iid], { enabled: !!iid && enabled });
  // Assignments are mutable, so never cached.
  const assigns = useResource(
    (s) => api.assignments(pid, iid, s), [pid, iid, revision], { enabled: !!pid && !!iid && enabled });

  const rows = useMemo(() => {
    if (!access.data || !detail.data) return null;

    const acts = new Map((detail.data.activities || []).map((a) => [a.id, a]));
    const contracts = new Map((detail.data.contracts || []).map((c) => [c.number, c]));
    const coord = new Map(
      (assigns.data?.assignments || []).map((a) => [a.activity_id, a])
    );

    // Location per activity-week, from the occupancy file. The first tunnel
    // sector is the identifying one; the rest are the closure it implies.
    const locByKey = new Map();
    if (occupancy.data) {
      for (const o of parseCsv(occupancy.data)) {
        const k = o.activity_id + ':' + o.week;
        if (!locByKey.has(k)) locByKey.set(k, []);
        locByKey.get(k).push(o.location_id);
      }
    }

    const out = [];
    for (const r of parseCsv(access.data)) {
      const a = acts.get(r.activity_id);
      const locs = locByKey.get(r.activity_id + ':' + r.week) || [];
      out.push({
        key: `${project.id}:${r.activity_id}:${r.week}:${r.access_seq}`,
        projectId: project.id,
        projectName: project.name,
        // Carried on the row so the detail panel never has to guess a calendar
        // mapping, and shows week numbers alone when there is none.
        horizonStart: detail.data?.summary?.horizon_start || null,
        activityId: r.activity_id,
        week: Number(r.week),
        accessSeq: Number(r.access_seq),
        eclo: r.eclo === '1',
        accessNight: Number(r.access_night),
        contract: a?.contract || '',
        contractInfo: contracts.get(a?.contract) || null,
        priority: a?.priority,
        from: a?.from || '',
        to: a?.to || '',
        location: a?.from || locs[0] || '',
        allLocations: locs,
        coordinator: coord.get(r.activity_id) || null,
      });
    }
    return out;
  }, [access.data, occupancy.data, detail.data, assigns.data, project]);

  return {
    versions, chosen, rows,
    horizonStart: detail.data?.summary?.horizon_start || null,
    horizonWeeks: detail.data?.summary?.horizon_weeks || 0,
    instanceId: iid,
    loading: versions.loading || access.loading || occupancy.loading || detail.loading || assigns.loading,
    error: versions.error || access.error || occupancy.error || detail.error || assigns.error,
    reload: () => { versions.reload(); access.reload(); occupancy.reload(); detail.reload(); assigns.reload(); },
    reloadAssignments: assigns.reload,
  };
}
