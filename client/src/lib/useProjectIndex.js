import { useState } from 'react';
import { useResource } from './useResource.js';
import { api } from './api.js';
export function useProjectIndex(user) {
  const [cursors, setCursors] = useState([null]);
  const [position, setPosition] = useState(0);
  const result = useResource(s => api.projects(s, cursors[position]), [user?.id, cursors[position]], {enabled: !!user});
  return {...result, position,
    previous: () => setPosition(p => Math.max(0,p-1)),
    next: () => { if(result.data?.next_cursor) { setCursors(c => [...c.slice(0,position+1),result.data.next_cursor]); setPosition(p=>p+1); } }
  };
}
