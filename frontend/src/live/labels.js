// ---- Honest wording for each data source ----
// The dashboard can show three kinds of data, and it always says which one it is:
//   demo      - the made-up data in data/sampleMission.js (no backend, or no robot has connected yet)
//   simulator - live data from the Prowl simulator (backend/tools/fake_robot.py), not a real robot
//   robot     - live data from the real robot
// Nothing here ever says a report was delivered to Earth: reports are saved in the mission log only.

const savedQueueMessages = {
  none: 'Saves this report to the shared mission log on the server. Nothing is sent to Earth.',
  queued: '✓ Saved to the mission log.',
  changed: 'Changed since it was saved. Save the update to include the change.',
}

const labels = {
  demo: {
    sampleTag: 'Simulated · unverified',
    obstacleTag: 'Simulated obstacle',
    pinSuffix: 'Simulated and unverified.',
    obstaclePinSuffix: 'Simulated obstacle.',
    findingsNote: 'Simulated and unverified. Names are guesses, not test results.',
    obstaclesJump: 'Obstacles (simulated)',
    obstaclesBadge: 'simulated',
    activityTag: 'Simulated',
    activitySuffix: ' (simulated)',
    queuedFlag: 'Queued locally — demo only',
    mapTag: 'Simulated map — local coordinates',
    legendObstacle: 'Obstacle (simulated)',
    queueMessages: {
      none: 'Demo only. Adds to a local list. Nothing is sent.',
      queued: '✓ Queued locally — demo only.',
      changed: 'Changed since it was queued. Queue the update to include the change. Local only.',
    },
    clockLabel: 'Simulated mission time',
    camera: 'Camera offline — awaiting robot connection.',
    emptyFindings: 'No points of interest.',
    emptyObstacles: 'No obstacles.',
  },
  simulator: {
    sampleTag: 'Simulator detection · unverified',
    obstacleTag: 'Simulated LiDAR obstacle',
    pinSuffix: 'From the simulator, unverified.',
    obstaclePinSuffix: 'Simulated LiDAR obstacle.',
    findingsNote: 'Live from the Prowl simulator, not a real robot. Names are detector guesses, not test results.',
    obstaclesJump: 'Obstacles (LiDAR)',
    obstaclesBadge: 'simulator',
    activityTag: 'Simulator feed',
    activitySuffix: '',
    queuedFlag: 'Saved to mission log',
    mapTag: 'Simulator LiDAR map',
    legendObstacle: 'Obstacle (LiDAR)',
    queueMessages: savedQueueMessages,
    clockLabel: 'Mission time · simulator',
    camera: 'No video in this version. Camera finds appear as points of interest.',
    emptyFindings: 'Nothing found yet. The camera is still looking.',
    emptyObstacles: 'No obstacles mapped yet.',
  },
  robot: {
    sampleTag: 'Camera detection · unverified',
    obstacleTag: 'LiDAR obstacle',
    pinSuffix: 'Camera detection, unverified.',
    obstaclePinSuffix: 'LiDAR obstacle.',
    findingsNote: "Found by the robot's camera. Names are detector guesses, not test results.",
    obstaclesJump: 'Obstacles (LiDAR)',
    obstaclesBadge: 'LiDAR',
    activityTag: 'Live',
    activitySuffix: '',
    queuedFlag: 'Saved to mission log',
    mapTag: 'Live LiDAR map',
    legendObstacle: 'Obstacle (LiDAR)',
    queueMessages: savedQueueMessages,
    clockLabel: 'Mission time',
    camera: 'No video in this version. Camera finds appear as points of interest.',
    emptyFindings: 'Nothing found yet. The camera is still looking.',
    emptyObstacles: 'No obstacles mapped yet.',
  },
}

export function labelsFor(mode) {
  return labels[mode] || labels.demo
}

// The connection indicator in the header: what it says, and whether its dot shows "live".
export function connectionText({ mode, connection, session, robotConnected }) {
  const who = mode === 'simulator' ? 'Simulator' : 'Robot'
  if (session) {
    if (session.live === false) return { text: 'Run ended — showing its final data', live: false }
    if (robotConnected) return { text: `${who} connected — live`, live: true }
    return { text: `${who} offline — showing last data`, live: false }
  }
  if (connection === 'online') return { text: 'Simulation — waiting for the robot', live: false }
  if (connection === 'connecting') return { text: 'Simulation — connecting…', live: false }
  return { text: 'Simulation — robot not connected', live: false }
}
