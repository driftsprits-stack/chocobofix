import { describe, it, expect } from 'vitest';
import { filterRows, groupRows, activityRows, sortRows } from '../scheduleModel.js';
const rows = [
  { key:'1',projectId:1,activityId:'A001',contract:'C001',week:3,eclo:false,priority:1,coordinator:{ coordinator_id:7,coordinator:'Ada' },location:'SEC:ALP:S01_S02:EB' },
  { key:'2',projectId:1,activityId:'A002',contract:'C002',week:3,eclo:true,priority:2,coordinator:{ coordinator_id:7,coordinator:'Ada' } },
  { key:'3',projectId:1,activityId:'A001',contract:'C001',week:5,eclo:false,priority:1 },
];
describe('schedule data integrity', () => {
  it('keeps simultaneous activities under the same person and week', () => {
    const group = groupRows(rows, 'person').find(([name]) => name === 'Ada');
    const activities = activityRows(group[1]);
    expect(activities).toHaveLength(2);
    expect(activities.flatMap(a => a.byWeek.get(3))).toHaveLength(2);
  });
  it('retains multiple accesses rather than overwriting a week', () => {
    const a = activityRows([rows[0], { ...rows[0], key:'4' }]);
    expect(a[0].byWeek.get(3)).toHaveLength(2);
  });
  it('combines independent filters and finds location or person', () => {
    expect(filterRows(rows, { person:'me:7', meId:7, week:'3', access:'eclo' })).toEqual([rows[1]]);
    expect(filterRows(rows, { query:'alp', priority:'1' })).toEqual([rows[0]]);
    expect(filterRows(rows, { query:'ada' })).toHaveLength(2);
    expect(filterRows(rows, { person:'unassigned' })).toEqual([rows[2]]);
    expect(filterRows(rows, { access:'standard' })).toHaveLength(2);
    expect(filterRows(rows, { person:'7' })).toHaveLength(2);
    expect(filterRows(rows, { priority:'3' })).toHaveLength(0);
  });
  it('sorts without mutating the source and groups by contract', () => {
    expect(sortRows(rows, 'week', 'desc')[0].week).toBe(5);
    expect(sortRows(rows, 'activityId')[0].activityId).toBe('A001');
    expect(rows[0].week).toBe(3);
    expect(groupRows(rows, 'contract')).toHaveLength(2);
    expect(groupRows(rows, 'project')[0][1]).toHaveLength(3);
    expect(activityRows(rows)).toHaveLength(2);
  });
});
