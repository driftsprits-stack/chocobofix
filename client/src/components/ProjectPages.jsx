export default function ProjectPages({resource}) {
  if(!resource.position && !resource.data?.next_cursor) return null;
  return <nav className="pagination" aria-label="Project pages"><button className="btn" disabled={!resource.position || resource.loading} onClick={resource.previous}>previous projects</button><span>page {resource.position+1}</span><button className="btn" disabled={!resource.data?.next_cursor || resource.loading} onClick={resource.next}>next projects</button></nav>;
}
