import { formatElapsed } from '../hooks/useMissionClock.js'

// ---- Turning live backend data into what the panels already use ----
// The panels were built around the shapes in data/sampleMission.js (samples, obstacles, robot, path,
// all in "local grid" units on a 120 x 80 survey zone). This file produces the same shapes from the
// robot's real data, so the panels don't need to know whether data is live or demo.
//
// The robot works in metres (its SLAM map frame, y pointing up). The survey zone it sends says which
// part of that frame the dashboard shows. Here metres become local grid units, with y flipped so it
// points down like the screen.

const LOCAL_WIDTH = 120 // must match mapArea in data/sampleMission.js
const LOCAL_HEIGHT = 80

// Used if the robot doesn't send a zone: 15 m x 10 m, centered on where the robot started.
const DEFAULT_ZONE = { x: -7.5, y: -5, width: 15, height: 10 }

function round1(value) {
  return Math.round(value * 10) / 10
}

export function makeProjection(zone = DEFAULT_ZONE) {
  const scale = Math.min(LOCAL_WIDTH / zone.width, LOCAL_HEIGHT / zone.height) // local units per metre
  return {
    scale,
    unitCm: Math.round(100 / scale),
    toLocal: (x, y) => ({ x: round1((x - zone.x) * scale), y: round1((zone.y + zone.height - y) * scale) }),
    zone,
  }
}

// ---- Text for each kind of thing the camera can report ----
// The keys are the labels the robot's detector sends. Add an entry when the detector learns a new label.
// The words are careful on purpose: a detector label is a guess, never a test result.
const SAMPLE_TEXT = {
  light_patch: {
    title: 'Lighter-toned patch',
    seen: 'a patch lighter than the ground around it',
    possibleValue:
      'Lighter material can point to ice or hydrated minerals that a future outpost could use. It could also just be dust.',
    stillUnknown: 'Whether it contains any water at all. Color and appearance cannot show that. It needs testing.',
    nextStep: 'Ask the Earth team whether this spot should be sampled for analysis.',
  },
  loose_material: {
    title: 'Loose surface material',
    seen: 'an area of loose, granular surface material',
    possibleValue: 'If suitable, it could be tested as a building material for shelters or landing pads.',
    stillUnknown: 'What it is made of, and whether it is strong or stable enough to build with.',
    nextStep: 'Compare it with other loose material nearby, then decide whether it needs analysis.',
  },
  dark_fragments: {
    title: 'Dark fragments',
    seen: 'darker fragments that look different from the ground around them',
    possibleValue: 'Unknown. It is not yet clear whether they have any use.',
    stillUnknown: 'Almost everything: what they are, how common they are, and whether they matter.',
    nextStep: 'Flag them for analysis first, before any use is considered.',
  },
}

function humanize(label) {
  const words = String(label).replace(/[_-]+/g, ' ').trim()
  return words.charAt(0).toUpperCase() + words.slice(1)
}

function describeSample(item, simulated) {
  const percent = Math.round((item.confidence ?? 0) * 100)
  const camera = simulated ? 'The simulated camera' : "The robot's camera"
  const known = SAMPLE_TEXT[item.label]
  if (known) {
    return {
      title: known.title,
      observed: `${camera} marked ${known.seen} (${percent}% detector confidence). Its position comes from the camera's bearing and the LiDAR's range.`,
      possibleValue: known.possibleValue,
      stillUnknown: known.stillUnknown,
      nextStep: known.nextStep,
    }
  }
  return {
    title: humanize(item.label),
    observed: `${camera} labeled something "${item.label}" (${percent}% detector confidence).`,
    possibleValue: 'Not known yet.',
    stillUnknown: 'What it is. A detector label is a guess, not a test result.',
    nextStep: 'Look at it again with the camera and decide whether it matters.',
  }
}

function describeObstacle(item, simulated) {
  const size = item.size_m ? `about ${item.size_m.toFixed(1)} m across` : 'of unknown size'
  return {
    title: `Obstacle ${size}`,
    observed: `${simulated ? 'Simulated LiDAR' : 'LiDAR'} returns outline something solid ${size}.`,
    possibleValue: 'The robot has to steer around it, and it can hide whatever is behind it.',
    stillUnknown: 'What it is. LiDAR measures shape and distance, not material.',
    nextStep: 'Add a note if the robot should take a closer look with the camera.',
  }
}

// Builds everything the panels need from the live link. noteDrafts holds notes still being typed here.
export function buildLiveMission(link, noteDrafts) {
  const simulated = !!link.mission?.simulated
  const projection = makeProjection(link.mission?.zone || DEFAULT_ZONE)
  const startedAt = link.session?.started_at ?? Date.now() / 1000
  const since = (ts) => formatElapsed(Math.max(0, Math.round((ts ?? startedAt) - startedAt)))

  const toFinding = (item) => {
    const isObstacle = item.kind === 'obstacle'
    const review = link.reviews[item.code] || {}
    const where = projection.toLocal(item.x, item.y)
    return {
      id: item.code,
      kind: isObstacle ? 'obstacle' : 'sample',
      ...(isObstacle ? describeObstacle(item, simulated) : describeSample(item, simulated)),
      x: where.x,
      y: where.y,
      discoveredAt: since(item.first_seen),
      status: review.status ?? (isObstacle ? 'New' : 'Needs analysis'),
      note: noteDrafts[item.code] ?? review.note ?? '',
    }
  }
  const byCode = (a, b) => a.id.localeCompare(b.id)
  const items = link.items.filter((item) => item.code)
  const samples = items.filter((item) => item.kind !== 'obstacle').map(toFinding).sort(byCode)
  const obstacles = items.filter((item) => item.kind === 'obstacle').map(toFinding).sort(byCode)

  const robot = link.pose
    ? projection.toLocal(link.pose.x, link.pose.y)
    : { x: LOCAL_WIDTH / 2, y: LOCAL_HEIGHT / 2 }
  const path = link.trail.map(([x, y]) => projection.toLocal(x, y))

  // The LiDAR map image, placed in local units. Its origin is its bottom-left corner in metres.
  let scanMap = null
  if (link.map) {
    const { origin, width, height, resolution } = link.map
    const topLeft = projection.toLocal(origin[0], origin[1] + height * resolution)
    scanMap = {
      href: link.map.href,
      x: topLeft.x,
      y: topLeft.y,
      width: width * resolution * projection.scale,
      height: height * resolution * projection.scale,
    }
  }

  const queue = link.reports.map((report) => ({
    sampleId: report.finding_id,
    status: report.status,
    note: report.note || '',
    queuedAt: since(report.ts),
  }))

  return {
    mode: simulated ? 'simulator' : 'robot',
    mission: { name: link.mission?.name || link.session?.name || 'Live mission' },
    startedAt,
    unitCm: projection.unitCm,
    samples,
    obstacles,
    robot,
    path: path.length ? path : [robot],
    scanMap,
    queue,
  }
}
