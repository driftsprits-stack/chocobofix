/** Shared, plain-language states. One instruction per sentence. */

export function Loading({ what = 'Loading' }) {
  return <p className="label" role="status">{what}…</p>;
}

export function Failed({ error, onRetry, what = 'load' }) {
  if (!error) return null;
  return (
    <div role="alert" className="failed">
      <p>Could not {what}. {error.message}</p>
      {onRetry ? (
        <p style={{ marginTop: 'var(--gap-3)' }}>
          <button type="button" className="btn btn-quiet" onClick={onRetry}>try again</button>
        </p>
      ) : null}
    </div>
  );
}

export function Empty({ children }) {
  return <p className="muted measure">{children}</p>;
}
