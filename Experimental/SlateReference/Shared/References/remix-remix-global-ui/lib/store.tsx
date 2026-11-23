'use client';
import React, { createContext, useContext, useState, ReactNode } from 'react';

export type RecordClassification = 'scene' | 'folder' | 'sketch' | 'solid' | 'cylinder' | 'sphere' | 'cone' | 'revolve' | 'loft';

export interface RecordProfile {
  Name?: string;
  Visible?: boolean;
  Position?: [number, number, number];
  Rotation?: [number, number, number];
  Scale?: [number, number, number];
  Albedo?: [number, number, number, number];
  Roughness?: number;
  Metalness?: number;
  ShadingMode?: number;
  Selectable?: boolean;
  ExtrudeDepth?: number;
  DraftAngle?: number;
  WallThickness?: number;
  CappedEnds?: boolean;
  Radius?: number;
  Height?: number;
  SegmentTally?: number;
  RingTally?: number;
  BaseRadius?: number;
  TipRadius?: number;
  SweepAngle?: number;
  AxisChoice?: number;
  ProfileClosed?: boolean;
  SectionTally?: number;
  TangencyStart?: number;
  TangencyEnd?: number;
  Ruled?: boolean;
  Units?: number;
  ToleranceLinear?: number;
  ToleranceAngular?: number;
  DocumentPath?: string;
  BooleanMode?: number;
  Suppressed?: boolean;
  NestedTally?: number;
  PlaneChoice?: number;
  ConstraintTally?: number;
  CurveTally?: number;
  FullyConstrained?: boolean;
  GridSnap?: number;
  [key: string]: any;
}

export interface RecordEntry {
  token: string;
  name: string;
  classification: RecordClassification;
  expanded?: boolean;
  hidden?: boolean;
  nested?: RecordEntry[];
  profile?: RecordProfile;
}

function issueToken() {
  return 'r' + Math.random().toString(36).substr(2, 9);
}

const initialStore: RecordEntry[] = [
  { token: 'r001', name: 'Part', classification: 'scene', expanded: true, nested: [
    { token: 'r002', name: 'Sketches', classification: 'folder', expanded: true, nested: [
      { token: 'r003', name: 'SK_BasePlate', classification: 'sketch', nested: [] },
      { token: 'r004', name: 'SK_BoltHoles', classification: 'sketch', nested: [] },
    ]},
    { token: 'r005', name: 'Bodies', classification: 'folder', expanded: true, nested: [
      { token: 'r006', name: 'BODY_Bracket', classification: 'folder', expanded: true, nested: [
        { token: 'r007', name: 'SOL_Plate',  classification: 'solid',    nested: [] },
        { token: 'r008', name: 'SOL_Boss',   classification: 'cylinder', nested: [] },
        { token: 'r009', name: 'SOL_Rib',    classification: 'solid',    nested: [] },
      ]},
      { token: 'r010', name: 'SOL_Housing', classification: 'solid', nested: [] },
      { token: 'r011', name: 'SOL_Dome',    classification: 'sphere', nested: [] },
    ]},
  ]},
];

export function establishProfile(entry: RecordEntry): RecordProfile {
  if (entry.profile) return entry.profile;
  const p: RecordProfile = {
    Name: entry.name, Visible: !entry.hidden,
    Position: [0,0,0], Rotation: [0,0,0], Scale: [1,1,1],
    Albedo: [214,216,222,255], Roughness: 0.42, Metalness: 0.08,
    ShadingMode: 0, Selectable: true
  };
  
  const nestedCount = entry.nested ? entry.nested.length : 0;
  
  switch (entry.classification) {
    case 'scene':    Object.assign(p, { Units: 1, ToleranceLinear: 0.01, ToleranceAngular: 0.5, DocumentPath: '/Projects/Bracket_Rev4.wsdoc' }); break;
    case 'folder':   Object.assign(p, { NestedTally: nestedCount, BooleanMode: 0, Suppressed: false }); break;
    case 'sketch':   Object.assign(p, { PlaneChoice: 0, ConstraintTally: 12, CurveTally: 8, FullyConstrained: false, GridSnap: 0.5 }); break;
    case 'solid':    Object.assign(p, { ExtrudeDepth: 12.5, DraftAngle: 0, WallThickness: 2.5, CappedEnds: true }); break;
    case 'cylinder': Object.assign(p, { Radius: 6.25, Height: 18, SegmentTally: 32, CappedEnds: true }); break;
    case 'sphere':   Object.assign(p, { Radius: 8.4, SegmentTally: 48, RingTally: 24 }); break;
    case 'cone':     Object.assign(p, { BaseRadius: 7, TipRadius: 0, Height: 16, SegmentTally: 32 }); break;
    case 'revolve':  Object.assign(p, { SweepAngle: 360, AxisChoice: 1, ProfileClosed: true }); break;
    case 'loft':     Object.assign(p, { SectionTally: 3, TangencyStart: 0, TangencyEnd: 0, Ruled: false }); break;
  }
  entry.profile = p;
  return p;
}

// Pre-fill profiles
function prefillProfiles(entries: RecordEntry[]) {
  for (const entry of entries) {
    establishProfile(entry);
    if (entry.nested) prefillProfiles(entry.nested);
  }
}
prefillProfiles(initialStore);

export type RevisionCategory = 'start' | 'feature' | 'param' | 'sketch' | 'transform' | 'body' | 'add' | 'edit' | 'drop';

export interface Revision {
  id: string;
  token: string;
  date: Date;
  category: RevisionCategory;
  title: string;
  subtitle?: string;
  comment?: string;
  author?: string;
  editValue?: string | number;
}

export interface AppState {
  records: RecordEntry[];
  selection: Set<string>;
  filterText: string;
  updateRecord: (token: string, changes: Partial<RecordEntry>) => void;
  updateProfile: (token: string, changes: Partial<RecordProfile>) => void;
  setSelection: (selection: Set<string>) => void;
  setFilterText: (text: string) => void;
  revisions: Revision[];
  addRevision: (rev: Omit<Revision, 'id' | 'date'>) => void;
  updateRevision: (id: string, changes: Partial<Revision>) => void;
}

const AppContext = createContext<AppState | undefined>(undefined);

function generateRevisions(): Revision[] {
  const revs: Revision[] = [];
  let time = Date.now() - 1000 * 60 * 60 * 24;

  const addRev = (token: string, cat: RevisionCategory, title: string, subtitle: string, comment?: string, author?: string, editValue?: string | number) => {
    revs.push({ id: 'v_' + Math.random().toString(36).substr(2,9), token, date: new Date(time), category: cat, title, subtitle, comment, author, editValue });
    time += 1000 * 60 * 5; // advance 5 minutes
  };

  const records = [
    { t: 'r001', name: 'Part' },
    { t: 'r002', name: 'Sketches' },
    { t: 'r003', name: 'SK_BasePlate' },
    { t: 'r004', name: 'SK_BoltHoles' },
    { t: 'r005', name: 'Bodies' },
    { t: 'r006', name: 'BODY_Bracket' },
    { t: 'r007', name: 'SOL_Plate' },
    { t: 'r008', name: 'SOL_Boss' },
    { t: 'r009', name: 'SOL_Rib' },
    { t: 'r010', name: 'SOL_Housing' },
    { t: 'r011', name: 'SOL_Dome' }
  ];

  const authors = ['Alex Chen', 'Sam Rivera', 'System', 'Maria Rossi'];
  const mockComments = [
    'Increased radius to match new constraints.',
    'Customer requested smoother finish.',
    'Adjusting clearance for thermal expansion.',
    'Reverting back to rev 2 geometry.',
    'Approximated spline from DXF import.',
  ];

  for (const rec of records) {
    addRev(rec.t, 'start', `Created ${rec.name}`, 'Initial state');
    addRev(rec.t, 'add', `Added to scene`, 'Inserted at origin');
    
    for (let i = 1; i <= 8; i++) {
       const actions = [
         { c: 'transform', t: `Translate ${rec.name}`, s: `Moved ${(Math.random()*10).toFixed(2)}mm` },
         { c: 'edit', t: `Material Update`, s: `Changed roughness to ${(Math.random()).toFixed(2)}` },
         { c: 'param', t: `Set Parameter`, s: `Radius = ${(Math.random()*5+2).toFixed(2)}mm`, val: (Math.random()*5+2).toFixed(2) },
         { c: 'feature', t: `Extrude Feature`, s: `Depth = ${(Math.random()*20+5).toFixed(2)}mm`, val: (Math.random()*20+5).toFixed(2) },
         { c: 'sketch', t: `Modify Sketch`, s: `Adjusted spline handles` }
       ];
       const act = actions[i % actions.length];
       
       const author = authors[Math.floor(Math.random() * authors.length)];
       const comment = Math.random() > 0.7 ? mockComments[Math.floor(Math.random() * mockComments.length)] : undefined;

       addRev(rec.t, act.c as RevisionCategory, act.t, act.s, comment, author, act.val);
    }
  }
  return revs;
}

const INITIAL_REVISIONS: Revision[] = generateRevisions();

export function AppProvider({ children }: { children: ReactNode }) {
  const [records, setRecords] = useState<RecordEntry[]>(initialStore);
  const [selection, setSelection] = useState<Set<string>>(new Set(['r008'])); // start selected with SOL_Boss
  const [filterText, setFilterText] = useState('');
  const [revisions, setRevisions] = useState<Revision[]>(INITIAL_REVISIONS);

  const addRevision = (rev: Omit<Revision, 'id' | 'date'>) => {
    setRevisions(prev => [...prev, { ...rev, id: 'v' + Math.random().toString(36).substr(2, 9), date: new Date() }]);
  };

  const updateRevision = (id: string, changes: Partial<Revision>) => {
    setRevisions(prev => prev.map(r => r.id === id ? { ...r, ...changes } : r));
  };

  const updateRecord = (token: string, changes: Partial<RecordEntry>) => {
    setRecords(prev => {
      const next = JSON.parse(JSON.stringify(prev)); // Deep copy for simplicity
      
      const apply = (entries: RecordEntry[]) => {
        for (let i = 0; i < entries.length; i++) {
          if (entries[i].token === token) {
            entries[i] = { ...entries[i], ...changes };
            return true;
          }
          if (entries[i].nested && apply(entries[i].nested!)) return true;
        }
        return false;
      };
      
      apply(next);
      return next;
    });
  };

  const updateProfile = (token: string, changes: Partial<RecordProfile>) => {
    setRecords(prev => {
      const next = JSON.parse(JSON.stringify(prev));
      const apply = (entries: RecordEntry[]) => {
        for (let i = 0; i < entries.length; i++) {
          if (entries[i].token === token) {
            entries[i].profile = { ...entries[i].profile, ...changes };
            return true;
          }
          if (entries[i].nested && apply(entries[i].nested!)) return true;
        }
        return false;
      };
      apply(next);
      return next;
    });

    const keys = Object.keys(changes);
    if (keys.length > 0) {
      addRevision({
        token,
        category: 'edit',
        title: `Edit ${keys[0]}`,
        subtitle: `Set to ${String(changes[keys[0] as keyof RecordProfile])}`
      });
    }
  };

  return (
    <AppContext.Provider value={{ records, selection, filterText, updateRecord, updateProfile, setSelection, setFilterText, revisions, addRevision, updateRevision }}>
      {children}
    </AppContext.Provider>
  );
}

export function useAppState() {
  const context = useContext(AppContext);
  if (!context) throw new Error('useAppState must be used within AppProvider');
  return context;
}

export function findRecord(records: RecordEntry[], token: string): RecordEntry | null {
  for (const r of records) {
    if (r.token === token) return r;
    if (r.nested) {
      const found = findRecord(r.nested, token);
      if (found) return found;
    }
  }
  return null;
}
