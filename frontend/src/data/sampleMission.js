// SIMULATED SAMPLE DATA. Nothing in this file comes from a real robot.
// Every name, coordinate, time and note is made up for this demo dashboard.
// Later, real robot data can replace these values without changing the components.

export const mission = {
  name: 'Survey Zone A',
  elapsedStartSeconds: 3492, // the simulated mission clock starts at T+00:58:12
}

// Size of the survey zone in local grid units. All x/y values below use this local grid.
export const mapArea = { width: 120, height: 80 }

// The whole Moon, as a flat world you can zoom out to. The made-up survey zone is placed on it at
// (missionX, missionY), measured in the same units. Every number here is for illustration only:
// the survey zone is fictional and is not a real place on the Moon.
export const moonWorld = { width: 1200, height: 600, missionX: 928, missionY: 225 }

// Where the simulated robot is right now.
export const robot = { x: 92, y: 30 }

// The route the simulated robot has already travelled, in order.
export const travelPath = [
  { x: 8, y: 58 },
  { x: 22, y: 54 },
  { x: 30, y: 44 },
  { x: 40, y: 40 },
  { x: 50, y: 33 },
  { x: 60, y: 26 },
  { x: 72, y: 18 },
  { x: 84, y: 22 },
  { x: 92, y: 30 },
]

export const statusOptions = ['Needs analysis', 'Prioritize sampling', 'Reviewed']

// Obstacles use their own, simpler set of statuses.
export const obstacleStatusOptions = ['New', 'Noted', 'Cleared']

// SIMULATED obstacles. Nothing here was detected by a real robot or sensor. They are made-up entries that
// show how an obstacle could appear on the map and how a person could add a note about it.
export const initialObstacles = [
  {
    id: 'O-001',
    kind: 'obstacle',
    title: 'Obstacle ahead of the robot',
    x: 106,
    y: 20,
    discoveredAt: 'T+00:56:30',
    observed: 'Something is in the way ahead of the robot. This is a simulated entry, not a real detection.',
    possibleValue: 'It could block the robot’s next move, or it could be something worth a closer look.',
    stillUnknown: 'What it is, how big it is, and whether the robot can go around it.',
    nextStep: 'Add a note saying what the Earth team wants to do about it.',
    status: 'New',
    note: '',
  },
  {
    id: 'O-002',
    kind: 'obstacle',
    title: 'Obstacle passed earlier',
    x: 58,
    y: 52,
    discoveredAt: 'T+00:22:18',
    observed: 'Something was in the way here earlier in the route. This is a simulated entry.',
    possibleValue: 'It shows where the robot had to change its path.',
    stillUnknown: 'Whether it is still there and whether it matters for later routes.',
    nextStep: 'Nothing is needed unless the Earth team asks for it.',
    status: 'Cleared',
    note: '',
  },
]

// All material names are simulated and unverified.
// "observed" is only what was recorded. The other fields say what is still unknown.
export const initialSamples = [
  {
    id: 'P-001',
    title: 'Candidate water-bearing material',
    x: 30,
    y: 44,
    discoveredAt: 'T+00:12:40',
    observed: 'A lighter-toned patch of fine material, different in color from the ground around it.',
    possibleValue: 'If confirmed, it could point to a water source that a future outpost might use.',
    stillUnknown:
      'Whether it contains any water at all. Color and appearance cannot show that. It needs testing.',
    nextStep: 'Ask the Earth team whether this spot should be sampled for analysis.',
    status: 'Needs analysis',
    note: '',
  },
  {
    id: 'P-002',
    title: 'Loose surface material',
    x: 50,
    y: 33,
    discoveredAt: 'T+00:31:05',
    observed: 'An area of loose, granular surface material, recorded for closer study.',
    possibleValue: 'If suitable, it could be tested as a building material for shelters or landing pads.',
    stillUnknown: 'What it is made of, and whether it is strong or stable enough to build with.',
    nextStep: 'Compare it with other nearby loose material, then decide whether it needs analysis.',
    status: 'Needs analysis',
    note: '',
  },
  {
    id: 'P-003',
    title: 'Unknown material',
    x: 72,
    y: 18,
    discoveredAt: 'T+00:47:52',
    observed: 'Darker, unusual-looking fragments that do not look like the other two samples.',
    possibleValue: 'Unknown. It is not yet clear whether it has any use.',
    stillUnknown: 'Almost everything: what it is, how common it is, and whether it matters.',
    nextStep: 'Flag it for analysis first, before any use is considered.',
    status: 'Needs analysis',
    note: '',
  },
]
