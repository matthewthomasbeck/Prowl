import { useEffect, useRef, useState } from 'react'
import './LidarScope.css'
import { useLiveValue } from '../live/useProwlLink.js'

// ---- The LiDAR scope: what the robot's LiDAR sees right now, around the robot ----
// Robot only (the dashboard bridge). The robot doesn't map the area or track where it is, so this is not a map:
// the robot stays in the middle, facing up, and the scan points are drawn around it. Like a radar screen,
// the last few scans stay on screen and fade, so walls read as solid lines and a moving person leaves a short
// trail.
//
// link: from useProwlLink. The scan ({ points: [[x, y], ...] } in metres, map frame) is read here with useLiveValue,
// so a new scan re-draws only the scope. link.pose: { x, y, theta } or null; without one the robot is at the map
// origin facing +x, which is how the bridge placed the points.
// scanAge: seconds since the last scan arrived (null = never), from the robot's status message.

const TRAIL = 5 // how many scans stay on screen, newest brightest
const RANGES = [2, 4, 8] // metres from the robot to the top edge
const RINGS = { 2: 0.5, 4: 1, 8: 2 } // metres between distance rings, for each range
const RATIO = 1.5 // the same 3:2 box as the map
const CLOSE_M = 0.4 // nearer than this, a side's clearance is flagged
const STALE_S = 2

// Scan points in the robot's own frame: x forward, y left.
function aroundRobot(points, pose) {
  const p = pose || { x: 0, y: 0, theta: 0 }
  const cos = Math.cos(p.theta)
  const sin = Math.sin(p.theta)
  return points.map(([x, y]) => {
    const dx = x - p.x
    const dy = y - p.y
    return [dx * cos + dy * sin, -dx * sin + dy * cos]
  })
}

// The nearest return in front, to the left, to the right and behind (each a quarter turn), or null.
function clearance(points) {
  const nearest = { front: null, left: null, right: null, back: null }
  for (const [x, y] of points) {
    const distance = Math.hypot(x, y)
    const angle = (Math.atan2(y, x) * 180) / Math.PI // 0 = forward, counter-clockwise
    const side = Math.abs(angle) <= 45 ? 'front' : Math.abs(angle) >= 135 ? 'back' : angle > 0 ? 'left' : 'right'
    if (nearest[side] === null || distance < nearest[side]) nearest[side] = distance
  }
  return nearest
}

const SIDES = [
  { side: 'front', label: 'Front' },
  { side: 'left', label: 'Left' },
  { side: 'right', label: 'Right' },
  { side: 'back', label: 'Back' },
]

// The scan points, drawn on a canvas laid over the SVG with the same scale (metres, robot in the middle).
// As SVG circles, each with a glow, the last few scans are about 900 elements replaced 5 times a second, which
// is more than Firefox can draw smoothly. history: scans in the robot's frame (x forward, y left), newest first.
function ScanDots({ history, range, width }) {
  const canvasRef = useRef(null)
  const [size, setSize] = useState(null) // the canvas's size on screen, in CSS pixels

  useEffect(() => {
    const observer = new ResizeObserver(([entry]) => setSize({ w: entry.contentRect.width, h: entry.contentRect.height }))
    observer.observe(canvasRef.current)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    const canvas = canvasRef.current
    if (!size || size.w === 0) return
    const ratio = window.devicePixelRatio || 1
    const w = Math.round(size.w * ratio)
    const h = Math.round(size.h * ratio)
    const context = canvas.getContext('2d')
    // Resizing the canvas makes the browser allocate a new one, so only do it when its size on screen changed
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w
      canvas.height = h
    }
    context.setTransform(1, 0, 0, 1, 0, 0)
    context.clearRect(0, 0, w, h)
    const scale = canvas.width / (width * 2) // pixels per metre; the box has the SVG's proportions
    context.setTransform(scale, 0, 0, scale, canvas.width / 2, canvas.height / 2)
    const dot = range * 0.011 // dots keep the same size on screen at every range
    // Oldest first, so the newest scan is on top. Each scan is a soft halo, then the dots: two fills in all.
    for (let age = history.length - 1; age >= 0; age--) {
      const fade = 1 - age / TRAIL
      for (const [radius, alpha] of [
        [dot * 2.4, 0.22],
        [dot, 1],
      ]) {
        context.beginPath()
        for (const [x, y] of history[age]) {
          // screen x is the robot's right (-y), screen y is its back (-x)
          context.moveTo(-y + radius, -x)
          context.arc(-y, -x, radius, 0, 2 * Math.PI)
        }
        context.fillStyle = `rgba(241, 244, 246, ${alpha * fade})`
        context.fill()
      }
    }
  }, [history, range, width, size])

  return <canvas ref={canvasRef} className="scope-dots" aria-hidden="true" />
}

function LidarScope({ link, scanAge }) {
  const scan = useLiveValue(link, 'scan')
  const pose = link.pose
  const [range, setRange] = useState(4)
  // Keeps the last few scans. Like the map's fly-to, this adjusts state while rendering when the scan changes.
  const [history, setHistory] = useState([])
  const [seenScan, setSeenScan] = useState(null)
  if (scan !== seenScan) {
    setSeenScan(scan)
    if (scan) setHistory((current) => [aroundRobot(scan.points, pose), ...current].slice(0, TRAIL))
  }

  const width = range * RATIO
  const step = RINGS[range]
  const rings = []
  for (let r = step; r <= width + 0.001; r += step) rings.push(r)
  const nearest = clearance(history[0] || [])
  const stale = scanAge === null || scanAge === undefined || scanAge > STALE_S

  let message = null
  if (!scan) message = scanAge === null ? 'Waiting for the LiDAR: its driver has not sent a scan yet' : 'Waiting for the LiDAR…'
  else if (stale) message = `LiDAR quiet for ${Math.round(scanAge)} s — showing its last scan`

  return (
    <div className="map scope" style={{ '--map-ratio': RATIO }} role="group" aria-label={describe(nearest, scan)}>
      {/* Screen x is the robot's right (-y), screen y is its back (-x), so forward is up. Units are metres. */}
      <svg className="map-svg" viewBox={`${-width} ${-range} ${width * 2} ${range * 2}`} aria-hidden="true">
        {rings.map((r) => (
          <circle key={r} className="scope-ring" cx="0" cy="0" r={r} />
        ))}
        <line className="scope-axis" x1="0" y1={-range} x2="0" y2={range} />
        <line className="scope-axis" x1={-width} y1="0" x2={width} y2="0" />
      </svg>
      <ScanDots history={history} range={range} width={width} />

      <span className="map-corner corner-tl" aria-hidden="true"></span>
      <span className="map-corner corner-tr" aria-hidden="true"></span>
      <span className="map-corner corner-bl" aria-hidden="true"></span>
      <span className="map-corner corner-br" aria-hidden="true"></span>

      {/* Distance labels on each ring, straight above the robot */}
      {rings
        .filter((r) => r < range)
        .map((r) => (
          <span key={r} className="ruler scope-ruler" style={{ top: `${((range - r) / (range * 2)) * 100}%` }} aria-hidden="true">
            {r} m
          </span>
        ))}

      <div className="robot-marker" style={{ left: '50%', top: '50%' }}>
        <span className="robot-label">Robot</span>
      </div>
      <span className="map-label scope-forward" aria-hidden="true">
        ▲ Forward
      </span>

      {/* How close the nearest thing is on each side. Close ones get a warning symbol and word, not only a color. */}
      {SIDES.map(({ side, label }) => {
        const distance = nearest[side]
        const close = distance !== null && distance < CLOSE_M
        return (
          <span key={side} className={`scope-clearance scope-${side}${close ? ' close' : ''}`} aria-hidden="true">
            {close && '⚠ '}
            {label} <span className="mono">{distance === null ? '—' : `${distance.toFixed(2)} m`}</span>
            {close && ' close'}
          </span>
        )
      })}

      {message && <p className="scope-message">{message}</p>}

      {/* Range buttons, in the same place and style as the map's zoom buttons */}
      <div className="map-zoom" role="group" aria-label="LiDAR scope range">
        {RANGES.map((value) => (
          <button
            key={value}
            className="map-zoom-button map-zoom-text"
            onClick={() => setRange(value)}
            aria-pressed={range === value}
            aria-label={`Show ${value} metres around the robot`}
          >
            {value} m
          </button>
        ))}
      </div>
    </div>
  )
}

// What a screen reader hears for the scope: the clearance on each side.
function describe(nearest, scan) {
  if (!scan) return 'LiDAR scope around the robot. No scan yet.'
  const parts = SIDES.map(({ side, label }) =>
    nearest[side] === null ? `${label}: nothing in range` : `${label}: ${nearest[side].toFixed(1)} metres`,
  )
  return `LiDAR scope around the robot, forward is up. Nearest return: ${parts.join(', ')}.`
}

// The legend and note under the scope, in the map's own style.
export function LidarLegend() {
  return (
    <>
      <ul className="legend">
        <li>
          <span className="swatch swatch-robot"></span>Robot (forward is up)
        </li>
        <li>
          <span className="swatch swatch-scan"></span>LiDAR return
        </li>
        <li>
          <span className="swatch swatch-scan swatch-scan-old"></span>Earlier scans, fading
        </li>
        <li>
          <span className="swatch swatch-ring"></span>Distance ring
        </li>
        <li>⚠ Closer than {CLOSE_M} m</li>
      </ul>
      <p className="map-note">
        Live from the robot's LiDAR, drawn around the robot. The robot doesn't map the area or track its position, so
        this is what it sees now, not a map.
      </p>
    </>
  )
}

export default LidarScope
