import { useState, useRef, useEffect } from 'react'
import './MissionMap.css'
import SectionIcon from './SectionIcon.jsx'
import moonUrl from '../assets/moon-map-2k.jpg'
import groundUrl from '../assets/moon-ground.jpg'
import { mapGround } from '../data/backgroundPhoto.js'
import { labelsFor } from '../live/labels.js'

// ---- How this map works ----
// The map is a whole flat Moon (the "world"). The made-up survey zone sits on it as a small rectangle.
// You can zoom out until the whole Moon is on screen, or zoom in until the survey zone fills the screen
// (and then closer still), like a map app.
//
// The map keeps one "view": which part of the world is on screen.
//   zoom = how close we are (1 = the whole Moon, 10 = the survey zone fills the screen)
//   x, y = the top-left corner of the visible part, in world units
// Everything on the map (the drawing, the pins, the robot) is placed using that view.

const MIN_ZOOM = 1
const MAX_ZOOM = 40
const DETAIL_ZOOM = 6 // from this zoom on, the pins, robot and grid are shown (before that, one zone marker)

function clamp(value, low, high) {
  return Math.min(high, Math.max(low, value))
}

// Keeps the view inside the world: not too far out, not too far in, and not panned off the edge.
function limit(view, world, ratio) {
  const zoom = clamp(view.zoom, MIN_ZOOM, MAX_ZOOM)
  const width = world.width / zoom
  const height = width * ratio
  return {
    zoom,
    x: clamp(view.x, 0, world.width - width),
    // zoomed far out, the Moon is shorter than the screen, so it sits in the middle with space above and below
    y: height >= world.height ? (world.height - height) / 2 : clamp(view.y, 0, world.height - height),
  }
}

// Zooms by a factor but keeps the spot under the mouse (or fingers) where it is, like a map app.
// fx and fy say where that spot is on screen, from 0 (left/top) to 1 (right/bottom).
function zoomAt(view, world, ratio, factor, fx, fy) {
  const width = world.width / view.zoom
  const height = width * ratio
  const spotX = view.x + fx * width
  const spotY = view.y + fy * height
  const zoom = clamp(view.zoom * factor, MIN_ZOOM, MAX_ZOOM)
  const newWidth = world.width / zoom
  return limit({ zoom, x: spotX - fx * newWidth, y: spotY - fy * newWidth * ratio }, world, ratio)
}

// Grid lines at multiples of "step" local units, like 20, 40, 60...
function multiples(step, max) {
  const values = []
  for (let value = step; value < max; value += step) values.push(value)
  return values
}

// scanMap (live only): the robot's LiDAR map as an image placed in local units { href, x, y, width, height }.
// unitCm (live only): how many centimetres one local grid unit is. labels: wording for the data source.
function MissionMap({
  area,
  world,
  zoneName,
  path,
  robot,
  samples,
  obstacles,
  selectedId,
  onSelect,
  scanMap = null,
  unitCm,
  labels = labelsFor('demo'),
}) {
  const items = [...samples, ...obstacles] // anything on the map that can be selected
  const ratio = area.height / area.width // the map box is the same shape as the survey zone
  const fitZoom = world.width / area.width // the zoom where the survey zone exactly fills the screen
  const zoneView = { zoom: fitZoom, x: world.missionX, y: world.missionY }

  const [view, setView] = useState(zoneView) // start looking at the survey zone
  const [moving, setMoving] = useState(false) // true while the person is panning or zooming
  const mapRef = useRef(null)
  const pointers = useRef(new Map()) // fingers or mouse currently pressing the map
  const lastPinch = useRef(0)
  const [moveStamp, setMoveStamp] = useState(0) // goes up by one each time the map is moved
  const [seenSelection, setSeenSelection] = useState(selectedId) // which selection the view was last adjusted for

  const visibleWidth = world.width / view.zoom
  const visibleHeight = visibleWidth * ratio
  const detail = view.zoom >= DETAIL_ZOOM // are we close enough to show pins, robot and grid?

  // The survey zone's local points (like { x: 30, y: 44 }) are placed on the world by adding the zone's corner.
  function toWorld(point) {
    return { x: point.x + world.missionX, y: point.y + world.missionY }
  }

  // Turns a world point into CSS percentages for the part of the world on screen.
  // Points outside the view get percentages below 0 or above 100, and are simply clipped away.
  function pointStyle(point) {
    return {
      left: `${((point.x - view.x) / visibleWidth) * 100}%`,
      top: `${((point.y - view.y) / visibleHeight) * 100}%`,
    }
  }

  function onScreen(point) {
    return (
      point.x >= view.x &&
      point.x <= view.x + visibleWidth &&
      point.y >= view.y &&
      point.y <= view.y + visibleHeight
    )
  }

  // While the person is moving the map, the targeting frame must follow instantly instead of gliding.
  function markMoving() {
    setMoving(true)
    setMoveStamp((stamp) => stamp + 1)
  }

  // Each time the map moves, wait a moment after the last movement, then let the frame glide again.
  useEffect(() => {
    if (moveStamp === 0) return
    const timer = setTimeout(() => setMoving(false), 300)
    return () => clearTimeout(timer)
  }, [moveStamp])

  function zoomBy(factor, fx = 0.5, fy = 0.5) {
    markMoving()
    setView((current) => zoomAt(current, world, ratio, factor, fx, fy))
  }

  function showSurveyZone() {
    markMoving()
    setView(zoneView)
  }

  function showWholeMoon() {
    markMoving()
    setView(limit({ zoom: MIN_ZOOM, x: 0, y: 0 }, world, ratio))
  }

  // Moves the view by a fraction of what is on screen (0.1 = a tenth of the screen).
  function panBy(fractionX, fractionY) {
    markMoving()
    setView((current) => {
      const width = world.width / current.zoom
      return limit(
        { ...current, x: current.x + fractionX * width, y: current.y + fractionY * width * ratio },
        world,
        ratio,
      )
    })
  }

  // ---- Mouse, touch and pen ----
  function handlePointerDown(event) {
    if (event.target.closest('button')) return // pins and zoom buttons do their own thing
    event.currentTarget.setPointerCapture(event.pointerId)
    pointers.current.set(event.pointerId, { x: event.clientX, y: event.clientY })
    lastPinch.current = 0
    markMoving()
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
      markMoving()
      setView((current) =>
        zoomAt(
          current,
          world,
          ratio,
          Math.exp(-event.deltaY * 0.0015),
          (event.clientX - rect.left) / rect.width,
          (event.clientY - rect.top) / rect.height,
        ),
      )
    }
    element.addEventListener('wheel', handleWheel, { passive: false })
    return () => element.removeEventListener('wheel', handleWheel)
    // markMoving only uses a setState, so it is safe to leave out
  }, [world, ratio])

  // When a different sample is selected (for example from the list), fly to it: zoom in to at least the
  // survey zone and center its pin. React allows adjusting state while rendering when the thing it depends
  // on (here: the selection) has changed, so no effect is needed.
  if (seenSelection !== selectedId) {
    setSeenSelection(selectedId)
    const sample = items.find((item) => item.id === selectedId)
    if (sample) {
      const target = toWorld(sample)
      const comfortable =
        detail &&
        target.x >= view.x + visibleWidth * 0.08 &&
        target.x <= view.x + visibleWidth * 0.92 &&
        target.y >= view.y + visibleHeight * 0.08 &&
        target.y <= view.y + visibleHeight * 0.92
      if (!comfortable) {
        const zoom = Math.max(view.zoom, fitZoom)
        const width = world.width / zoom
        setView(limit({ zoom, x: target.x - width / 2, y: target.y - (width * ratio) / 2 }, world, ratio))
        markMoving()
      }
    }
  }

  // The path in world units, as text for the SVG: "936,283 950,279 ..."
  const pathPoints = path.map((point) => `${point.x + world.missionX},${point.y + world.missionY}`).join(' ')

  const selectedSample = items.find((item) => item.id === selectedId)

  // The local grid (only shown when close up) gets finer as you zoom in, like a real map adding detail.
  const gridStep = view.zoom >= 30 ? 5 : view.zoom >= 18 ? 10 : 20
  const gridXs = multiples(gridStep, area.width)
  const gridYs = multiples(gridStep, area.height)
  const rulerX = gridXs.filter((x) => {
    const worldX = x + world.missionX
    return worldX >= view.x && worldX <= view.x + visibleWidth
  })
  const rulerY = gridYs.filter((y) => {
    const worldY = y + world.missionY
    const start = path[0]
    return worldY >= view.y && worldY <= view.y + visibleHeight && (!start || Math.abs(y - start.y) > 6) // not on the "Start" label
  })

  // Close up, the sharp survey-zone photo fades in over the whole-Moon map (like a map app loading finer tiles).
  const photoOpacity = clamp((view.zoom - 3) / 5, 0, 1)

  // Which look a pin gets: selected, still needs review, or already reviewed.
  function pinClass(sample) {
    if (sample.id === selectedId) return 'pin selected'
    return sample.status === 'Reviewed' ? 'pin pin-reviewed' : 'pin pin-review'
  }

  // Obstacles look like pins with a warning triangle. A cleared obstacle is dimmer.
  function obstacleClass(obstacle) {
    if (obstacle.id === selectedId) return 'pin pin-obstacle selected'
    return obstacle.status === 'Cleared' ? 'pin pin-obstacle pin-obstacle-cleared' : 'pin pin-obstacle'
  }

  const atSurveyZone =
    Math.abs(view.zoom - fitZoom) < 0.01 &&
    Math.abs(view.x - world.missionX) < 0.5 &&
    Math.abs(view.y - world.missionY) < 0.5

  return (
    <section className="panel map-panel">
      <div className="panel-heading">
        <h2>
          <SectionIcon name="map" />
          Exploration map
        </h2>
        <span className="tag">{labels.mapTag}</span>
      </div>

      {/* map-stage gives the map room; --map-ratio keeps the map box the same shape as the survey zone */}
      <div className="map-stage">
        <div
          ref={mapRef}
          className="map"
          style={{ '--map-ratio': area.width / area.height }}
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
              photo, a dashed outline around the survey zone, the local grid (close up only) and the travel path.
              The viewBox is the part of the world on screen, so changing it zooms and pans the drawing. */}
          <svg
            className="map-svg"
            viewBox={`${view.x} ${view.y} ${visibleWidth} ${visibleWidth * ratio}`}
            aria-hidden="true"
          >
            <rect
              className="map-space"
              x={-world.width}
              y={-world.height}
              width={world.width * 3}
              height={world.height * 3}
            />
            <image href={moonUrl} x="0" y="0" width={world.width} height={world.height} preserveAspectRatio="none" />
            <image
              href={groundUrl}
              x={world.missionX}
              y={world.missionY}
              width={area.width}
              height={area.height}
              preserveAspectRatio="none"
              opacity={photoOpacity}
            />
            {/* The robot's LiDAR map (live only). Bright = something solid, faint green = ground it has seen,
                see-through = not seen yet. It sits under the zone outline, grid and path. */}
            {scanMap && (
              <image
                className="scan-layer"
                href={scanMap.href}
                x={scanMap.x + world.missionX}
                y={scanMap.y + world.missionY}
                width={scanMap.width}
                height={scanMap.height}
                preserveAspectRatio="none"
              />
            )}
            <rect
              className="zone-outline"
              x={world.missionX}
              y={world.missionY}
              width={area.width}
              height={area.height}
            />
            {detail &&
              gridXs.map((x) => (
                <line
                  key={`x${x}`}
                  className="grid-line"
                  x1={x + world.missionX}
                  y1={world.missionY}
                  x2={x + world.missionX}
                  y2={world.missionY + area.height}
                />
              ))}
            {detail &&
              gridYs.map((y) => (
                <line
                  key={`y${y}`}
                  className="grid-line"
                  x1={world.missionX}
                  y1={y + world.missionY}
                  x2={world.missionX + area.width}
                  y2={y + world.missionY}
                />
              ))}
            {/* A dark outline under the green dashes keeps the path easy to see over the light ground */}
            <polyline className="travel-path-casing" points={pathPoints} />
            <polyline className="travel-path" points={pathPoints} />
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
              style={pointStyle({ x: world.missionX + area.width / 2, y: world.missionY })}
              onClick={showSurveyZone}
              aria-label={`Zoom in to ${zoneName}`}
            >
              {zoneName}
            </button>
          )}

          {detail && (
            <>
              {/* A ruler of local grid numbers along two edges */}
              {rulerX.map((x) => (
                <span
                  key={`rx${x}`}
                  className="ruler ruler-x"
                  style={{ left: `${((x + world.missionX - view.x) / visibleWidth) * 100}%` }}
                  aria-hidden="true"
                >
                  {x}
                </span>
              ))}
              {rulerY.map((y) => (
                <span
                  key={`ry${y}`}
                  className="ruler ruler-y"
                  style={{ top: `${((y + world.missionY - view.y) / visibleHeight) * 100}%` }}
                  aria-hidden="true"
                >
                  {y}
                </span>
              ))}

              {path.length > 0 && (
                <span className="map-label start-label" style={pointStyle(toWorld(path[0]))}>
                  Start
                </span>
              )}

              <div className="robot-marker" style={pointStyle(toWorld(robot))}>
                <span className="robot-label">Robot</span>
              </div>

              {/* The targeting frame glides to whichever pin is selected. The ping ring replays on each new selection. */}
              {selectedSample && (
                <div
                  className={moving ? 'reticle no-glide' : 'reticle'}
                  style={pointStyle(toWorld(selectedSample))}
                  aria-hidden="true"
                >
                  <span className="reticle-frame">
                    <span className="reticle-corner rc-tl"></span>
                    <span className="reticle-corner rc-tr"></span>
                    <span className="reticle-corner rc-bl"></span>
                    <span className="reticle-corner rc-br"></span>
                  </span>
                  <span key={selectedId} className="reticle-ping"></span>
                </div>
              )}

              {samples.map((sample) => (
                <button
                  key={sample.id}
                  className={pinClass(sample)}
                  style={pointStyle(toWorld(sample))}
                  onClick={() => onSelect(sample.id)}
                  aria-pressed={sample.id === selectedId}
                  aria-label={`${sample.id}: ${sample.title}. Status: ${sample.status}. ${labels.pinSuffix}`}
                  tabIndex={onScreen(toWorld(sample)) ? 0 : -1} // a pin hidden off screen must not be tabbed to
                >
                  {sample.id}
                </button>
              ))}

              {/* Simulated obstacles: a warning triangle plus the ID, so they look different from the sample pins
                  by shape and not only by color. */}
              {obstacles.map((obstacle) => (
                <button
                  key={obstacle.id}
                  className={obstacleClass(obstacle)}
                  style={pointStyle(toWorld(obstacle))}
                  onClick={() => onSelect(obstacle.id)}
                  aria-pressed={obstacle.id === selectedId}
                  aria-label={`${obstacle.id}: ${obstacle.title}. Status: ${obstacle.status}. ${labels.obstaclePinSuffix}`}
                  tabIndex={onScreen(toWorld(obstacle)) ? 0 : -1}
                >
                  <SectionIcon name="warning" plain />
                  {obstacle.id}
                </button>
              ))}
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
            {(view.zoom / fitZoom).toFixed(1)}×
          </span>
        </div>
      </div>

      <ul className="legend">
        <li>
          <span className="swatch swatch-robot"></span>Robot
        </li>
        <li>
          <span className="swatch swatch-path"></span>Traveled path
        </li>
        <li>
          <span className="swatch swatch-zone"></span>Survey zone
        </li>
        <li>
          <span className="swatch swatch-obstacle"></span>
          {labels.legendObstacle}
        </li>
        {scanMap && (
          <li>
            <span className="swatch swatch-lidar"></span>LiDAR map
          </li>
        )}
        <li>
          <span className="swatch swatch-review"></span>Needs review
        </li>
        <li>
          <span className="swatch swatch-reviewed"></span>Reviewed
        </li>
        <li>
          <span className="swatch swatch-selected"></span>Selected
        </li>
      </ul>
      <p className="map-note">
        {scanMap && `The bright outlines and green tint are the LiDAR map. One grid unit is ${unitCm} cm. `}
        Zoomed out: NASA's map of the whole Moon. Close up: {mapGround.description}, {mapGround.credit}. Decorative: the
        survey zone is {scanMap ? 'not shown at its real place on the Moon, and the photo is not where the robot is' : 'fictional and is not a real place on the Moon'}.
      </p>
    </section>
  )
}

export default MissionMap
