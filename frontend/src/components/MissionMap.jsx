import { useState, useRef, useEffect } from 'react'
import './MissionMap.css'
import SectionIcon from './SectionIcon.jsx'
import LidarScope, { LidarLegend } from './LidarScope.jsx'
import moonUrl from '../assets/moon-map-2k.jpg'
import groundUrl from '../assets/moon-ground.jpg'
import { mapGround } from '../data/backgroundPhoto.js'

// ---- How this map works ----
// The map is a whole flat Moon (the "world"). The made-up survey zone sits on it as a small rectangle.
// You can zoom out until the whole Moon is on screen, or zoom in until the survey zone fills the screen
// (and then closer still), like a map app.
//
// The map keeps one "view": which part of the world is on screen.
//   zoom = how close we are (1 = the whole Moon, 10 = the survey zone fills the screen)
//   x, y = the top-left corner of the visible part, in world units
// Everything on the map (the drawing, the robot) is placed using that view.

const MIN_ZOOM = 1
const MAX_ZOOM = 40
const DETAIL_ZOOM = 6 // from this zoom on, the robot and grid are shown (before that, one zone marker)

// The survey zone in local grid units (the numbers along the map's edges), and the whole Moon as a flat world in
// the same units, with the zone's top-left corner at (zoneX, zoneY). Every number here is for illustration only:
// the survey zone is fictional and is not a real place on the Moon.
const ZONE = { width: 120, height: 80 }
const WORLD = { width: 1200, height: 600, zoneX: 928, zoneY: 225 }
const RATIO = ZONE.height / ZONE.width // the map box is the same shape as the survey zone
const FIT_ZOOM = WORLD.width / ZONE.width // the zoom where the survey zone exactly fills the screen
const ZONE_VIEW = { zoom: FIT_ZOOM, x: WORLD.zoneX, y: WORLD.zoneY }
// The robot doesn't track its position yet, so it stays in the middle of the zone.
const ROBOT = { x: WORLD.zoneX + ZONE.width / 2, y: WORLD.zoneY + ZONE.height / 2 }

function clamp(value, low, high) {
  return Math.min(high, Math.max(low, value))
}

// Keeps the view inside the world: not too far out, not too far in, and not panned off the edge.
function limit(view) {
  const zoom = clamp(view.zoom, MIN_ZOOM, MAX_ZOOM)
  const width = WORLD.width / zoom
  const height = width * RATIO
  return {
    zoom,
    x: clamp(view.x, 0, WORLD.width - width),
    // zoomed far out, the Moon is shorter than the screen, so it sits in the middle with space above and below
    y: height >= WORLD.height ? (WORLD.height - height) / 2 : clamp(view.y, 0, WORLD.height - height),
  }
}

// Zooms by a factor but keeps the spot under the mouse (or fingers) where it is, like a map app.
// fx and fy say where that spot is on screen, from 0 (left/top) to 1 (right/bottom).
function zoomAt(view, factor, fx, fy) {
  const width = WORLD.width / view.zoom
  const height = width * RATIO
  const spotX = view.x + fx * width
  const spotY = view.y + fy * height
  const zoom = clamp(view.zoom * factor, MIN_ZOOM, MAX_ZOOM)
  const newWidth = WORLD.width / zoom
  return limit({ zoom, x: spotX - fx * newWidth, y: spotY - fy * newWidth * RATIO })
}

// Grid lines at multiples of "step" local units, like 20, 40, 60...
function multiples(step, max) {
  const values = []
  for (let value = step; value < max; value += step) values.push(value)
  return values
}

// The exploration map panel. It can switch to the LiDAR scope (LidarScope.jsx), what the robot's LiDAR sees around
// it right now, and starts on the scope.
//   zoneName: the mission's name, on the survey zone's marker when zoomed out (null before the robot answers).
//   link: from useProwlLink, for the scope. scanAge: seconds since the last scan arrived (null = never).
function MissionMap({ zoneName, link, scanAge }) {
  const [panelView, setPanelView] = useState('scope') // 'map' or 'scope'
  const showScope = panelView === 'scope'
  const zoneLabel = zoneName || 'Survey zone'

  const [view, setView] = useState(ZONE_VIEW) // start looking at the survey zone
  const mapRef = useRef(null)
  const pointers = useRef(new Map()) // fingers or mouse currently pressing the map
  const lastPinch = useRef(0)

  const visibleWidth = WORLD.width / view.zoom
  const visibleHeight = visibleWidth * RATIO
  const detail = view.zoom >= DETAIL_ZOOM // are we close enough to show the robot and grid?

  // Turns a world point into CSS percentages for the part of the world on screen.
  // Points outside the view get percentages below 0 or above 100, and are simply clipped away.
  function pointStyle(point) {
    return {
      left: `${((point.x - view.x) / visibleWidth) * 100}%`,
      top: `${((point.y - view.y) / visibleHeight) * 100}%`,
    }
  }

  function zoomBy(factor, fx = 0.5, fy = 0.5) {
    setView((current) => zoomAt(current, factor, fx, fy))
  }

  function showSurveyZone() {
    setView(ZONE_VIEW)
  }

  function showWholeMoon() {
    setView(limit({ zoom: MIN_ZOOM, x: 0, y: 0 }))
  }

  // Moves the view by a fraction of what is on screen (0.1 = a tenth of the screen).
  function panBy(fractionX, fractionY) {
    setView((current) => {
      const width = WORLD.width / current.zoom
      return limit({ ...current, x: current.x + fractionX * width, y: current.y + fractionY * width * RATIO })
    })
  }

  // ---- Mouse, touch and pen ----
  function handlePointerDown(event) {
    if (event.target.closest('button')) return // the zone marker and zoom buttons do their own thing
    event.currentTarget.setPointerCapture(event.pointerId)
    pointers.current.set(event.pointerId, { x: event.clientX, y: event.clientY })
    lastPinch.current = 0
  }

  function handlePointerMove(event) {
    const previous = pointers.current.get(event.pointerId)
    if (!previous) return
    const current = { x: event.clientX, y: event.clientY }
    pointers.current.set(event.pointerId, current)
    const rect = event.currentTarget.getBoundingClientRect()

    if (pointers.current.size === 1) {
      // One finger or the mouse: drag to pan. Dragging right moves the map right, so the view moves left.
      panBy(-(current.x - previous.x) / rect.width, -(current.y - previous.y) / rect.height)
    } else if (pointers.current.size === 2) {
      // Two fingers: pinch to zoom around the point between them.
      const [first, second] = [...pointers.current.values()]
      const gap = Math.hypot(first.x - second.x, first.y - second.y)
      const fx = ((first.x + second.x) / 2 - rect.left) / rect.width
      const fy = ((first.y + second.y) / 2 - rect.top) / rect.height
      if (lastPinch.current > 0) zoomBy(gap / lastPinch.current, fx, fy)
      lastPinch.current = gap
    }
  }

  function handlePointerEnd(event) {
    pointers.current.delete(event.pointerId)
    lastPinch.current = 0
  }

  function handleDoubleClick(event) {
    if (event.target.closest('button')) return
    const rect = event.currentTarget.getBoundingClientRect()
    zoomBy(2, (event.clientX - rect.left) / rect.width, (event.clientY - rect.top) / rect.height)
  }

  // ---- Keyboard: arrows pan, + and - zoom, 0 returns to the survey zone ----
  function handleKeyDown(event) {
    const actions = {
      ArrowLeft: () => panBy(-0.15, 0),
      ArrowRight: () => panBy(0.15, 0),
      ArrowUp: () => panBy(0, -0.15),
      ArrowDown: () => panBy(0, 0.15),
      '+': () => zoomBy(1.5),
      '=': () => zoomBy(1.5),
      '-': () => zoomBy(1 / 1.5),
      0: () => showSurveyZone(),
    }
    const action = actions[event.key]
    if (action) {
      event.preventDefault()
      action()
    }
  }

  // Scrolling the mouse wheel zooms. This needs a listener that is allowed to cancel the page scroll.
  useEffect(() => {
    const element = mapRef.current
    function handleWheel(event) {
      event.preventDefault()
      const rect = element.getBoundingClientRect()
      setView((current) =>
        zoomAt(
          current,
          Math.exp(-event.deltaY * 0.0015),
          (event.clientX - rect.left) / rect.width,
          (event.clientY - rect.top) / rect.height,
        ),
      )
    }
    element.addEventListener('wheel', handleWheel, { passive: false })
    return () => element.removeEventListener('wheel', handleWheel)
  }, [])

  // The local grid (only shown when close up) gets finer as you zoom in, like a real map adding detail.
  const gridStep = view.zoom >= 30 ? 5 : view.zoom >= 18 ? 10 : 20
  const gridXs = multiples(gridStep, ZONE.width)
  const gridYs = multiples(gridStep, ZONE.height)
  const rulerX = gridXs.filter((x) => {
    const worldX = x + WORLD.zoneX
    return worldX >= view.x && worldX <= view.x + visibleWidth
  })
  const rulerY = gridYs.filter((y) => {
    const worldY = y + WORLD.zoneY
    return worldY >= view.y && worldY <= view.y + visibleHeight
  })

  // Close up, the sharp survey-zone photo fades in over the whole-Moon map (like a map app loading finer tiles).
  const photoOpacity = clamp((view.zoom - 3) / 5, 0, 1)

  const atSurveyZone =
    Math.abs(view.zoom - FIT_ZOOM) < 0.01 &&
    Math.abs(view.x - WORLD.zoneX) < 0.5 &&
    Math.abs(view.y - WORLD.zoneY) < 0.5

  return (
    <section className="panel map-panel">
      <div className="panel-heading">
        <h2>
          <SectionIcon name="map" />
          Exploration map
        </h2>
        <span className="view-switch" role="group" aria-label="What this panel shows">
          <button className={showScope ? 'view-option' : 'view-option active'} onClick={() => setPanelView('map')} aria-pressed={!showScope}>
            Map
          </button>
          <button className={showScope ? 'view-option active' : 'view-option'} onClick={() => setPanelView('scope')} aria-pressed={showScope}>
            LiDAR scope
          </button>
        </span>
        <span className="tag">{showScope ? 'Live LiDAR · around the robot' : 'Position not tracked'}</span>
      </div>

      {showScope && (
        <div className="map-stage">
          <LidarScope link={link} scanAge={scanAge} />
        </div>
      )}

      {/* map-stage gives the map room; --map-ratio keeps the map box the same shape as the survey zone.
          While the scope is showing, the map is only hidden, so it keeps its place and zoom. */}
      <div className="map-stage" hidden={showScope}>
        <div
          ref={mapRef}
          className="map"
          style={{ '--map-ratio': ZONE.width / ZONE.height }}
          tabIndex={0}
          role="group"
          aria-label="Exploration map of the Moon. Drag to pan. Scroll, pinch or double-click to zoom. Arrow keys pan, plus and minus zoom."
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerEnd}
          onPointerCancel={handlePointerEnd}
          onDoubleClick={handleDoubleClick}
          onKeyDown={handleKeyDown}
        >
          {/* The drawing, from the bottom up: dark space, the map of the whole Moon, the sharper survey-zone
              photo, a dashed outline around the survey zone and the local grid (close up only).
              The viewBox is the part of the world on screen, so changing it zooms and pans the drawing. */}
          <svg
            className="map-svg"
            viewBox={`${view.x} ${view.y} ${visibleWidth} ${visibleHeight}`}
            aria-hidden="true"
          >
            <rect
              className="map-space"
              x={-WORLD.width}
              y={-WORLD.height}
              width={WORLD.width * 3}
              height={WORLD.height * 3}
            />
            <image href={moonUrl} x="0" y="0" width={WORLD.width} height={WORLD.height} preserveAspectRatio="none" />
            <image
              href={groundUrl}
              x={WORLD.zoneX}
              y={WORLD.zoneY}
              width={ZONE.width}
              height={ZONE.height}
              preserveAspectRatio="none"
              opacity={photoOpacity}
            />
            <rect
              className="zone-outline"
              x={WORLD.zoneX}
              y={WORLD.zoneY}
              width={ZONE.width}
              height={ZONE.height}
            />
            {detail &&
              gridXs.map((x) => (
                <line
                  key={`x${x}`}
                  className="grid-line"
                  x1={x + WORLD.zoneX}
                  y1={WORLD.zoneY}
                  x2={x + WORLD.zoneX}
                  y2={WORLD.zoneY + ZONE.height}
                />
              ))}
            {detail &&
              gridYs.map((y) => (
                <line
                  key={`y${y}`}
                  className="grid-line"
                  x1={WORLD.zoneX}
                  y1={y + WORLD.zoneY}
                  x2={WORLD.zoneX + ZONE.width}
                  y2={y + WORLD.zoneY}
                />
              ))}
          </svg>

          {/* Frame details: corner brackets */}
          <span className="map-corner corner-tl" aria-hidden="true"></span>
          <span className="map-corner corner-tr" aria-hidden="true"></span>
          <span className="map-corner corner-bl" aria-hidden="true"></span>
          <span className="map-corner corner-br" aria-hidden="true"></span>

          {/* Zoomed far out, one marker shows where the survey zone is. Click it to zoom in. */}
          {!detail && (
            <button
              className="zone-marker"
              style={pointStyle({ x: WORLD.zoneX + ZONE.width / 2, y: WORLD.zoneY })}
              onClick={showSurveyZone}
              aria-label={`Zoom in to ${zoneLabel}`}
            >
              {zoneLabel}
            </button>
          )}

          {detail && (
            <>
              {/* A ruler of local grid numbers along two edges */}
              {rulerX.map((x) => (
                <span
                  key={`rx${x}`}
                  className="ruler ruler-x"
                  style={{ left: `${((x + WORLD.zoneX - view.x) / visibleWidth) * 100}%` }}
                  aria-hidden="true"
                >
                  {x}
                </span>
              ))}
              {rulerY.map((y) => (
                <span
                  key={`ry${y}`}
                  className="ruler ruler-y"
                  style={{ top: `${((y + WORLD.zoneY - view.y) / visibleHeight) * 100}%` }}
                  aria-hidden="true"
                >
                  {y}
                </span>
              ))}

              <div className="robot-marker" style={pointStyle(ROBOT)}>
                <span className="robot-label">Robot</span>
              </div>
            </>
          )}

          {/* Zoom buttons, like a map app */}
          <div className="map-zoom">
            <button
              className="map-zoom-button"
              onClick={() => zoomBy(1.6)}
              aria-label="Zoom in on the map"
              disabled={view.zoom >= MAX_ZOOM}
            >
              +
            </button>
            <button
              className="map-zoom-button"
              onClick={() => zoomBy(1 / 1.6)}
              aria-label="Zoom out on the map"
              disabled={view.zoom <= MIN_ZOOM}
            >
              −
            </button>
            <button
              className="map-zoom-button map-zoom-text"
              onClick={showSurveyZone}
              aria-label="Show the survey zone"
              disabled={atSurveyZone}
            >
              Zone
            </button>
            <button
              className="map-zoom-button map-zoom-text"
              onClick={showWholeMoon}
              aria-label="Show the whole Moon"
              disabled={view.zoom <= MIN_ZOOM}
            >
              Moon
            </button>
          </div>
          <span className="map-zoom-level mono" aria-live="polite">
            {(view.zoom / FIT_ZOOM).toFixed(1)}×
          </span>
        </div>
      </div>

      {showScope ? (
        <LidarLegend />
      ) : (
        <>
          <ul className="legend">
            <li>
              <span className="swatch swatch-robot"></span>Robot
            </li>
            <li>
              <span className="swatch swatch-zone"></span>Survey zone
            </li>
          </ul>
          <p className="map-note">
            Zoomed out: NASA's map of the whole Moon. Close up: {mapGround.description}, {mapGround.credit}. Decorative: the
            survey zone is fictional and is not a real place on the Moon. The robot doesn’t track its position yet, so it
            stays in the middle of the zone.
          </p>
        </>
      )}
    </section>
  )
}

export default MissionMap
