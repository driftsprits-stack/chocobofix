/**
 * Which plan a project shows in the global view.
 *
 * A, B and C are three answers to different questions. Stacking them together
 * would read as simultaneous work, so exactly one is shown per project and the
 * choice is always visible.
 *
 * An approved version is the shared, published answer and wins. Otherwise the
 * newest version is shown and is labelled a draft. A newer solve never silently
 * replaces an approved plan.
 */
export function choosePlan(versions, prefer) {
  if (!versions || !versions.length) return null;

  if (prefer) {
    const exact = versions.find((v) => String(v.id) === String(prefer));
    if (exact) return exact;
  }

  const approved = versions
    .filter((v) => v.status === 'approved' || (v.approved_at && v.approved_at.length))
    .sort((a, b) => b.version_no - a.version_no);
  if (approved.length) return approved[0];

  return [...versions].sort(
    (a, b) => (b.version_no - a.version_no) || (b.id - a.id)
  )[0];
}

export function planState(v) {
  if (!v) return { label: 'No plan yet', published: false };
  const published = v.status === 'approved' || !!(v.approved_at && v.approved_at.length);
  return {
    published,
    label: published ? 'Published' : 'Draft',
    // An infeasible plan stays visibly infeasible; it is never presented as a
    // usable schedule just because it produced rows.
    failed: !v.feasible || v.violations > 0,
  };
}
