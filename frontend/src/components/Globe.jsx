import { useRef, useEffect, useState } from 'react'
import './Globe.css'

// A globe drawn by hand on a <canvas>, with no 3D library. The Moon globe and the GPS page's Earth both use it.
// For every pixel of the picture we work out which spot on the globe it shows, then copy that spot's color from a
// flat map of the whole world (an "equirectangular" map: longitude across, latitude down). That is the whole trick.
//   mapUrl: the flat map. name: what it is, for the labels ("the Moon").
//   size: drawing pixels across (CSS scales the canvas to fit). maxZoom: how far in it zooms; softZoom: from where
//   the map's own pixels show, so the hint says close-ups are soft.
//   marker: { lat, lon } in degrees, or null: a point drawn on the globe (the robot). The first time one appears the
//   globe turns to it, unless someone has already moved it; "Center on robot" (or C) turns to it again.
//   spin: turn slowly until touched (never when the device asks for reduced motion).
// mapUrl, size, maxZoom, markerZoom and spin are read once, when the globe is built.

const MIN_ZOOM = 1
const MAX_LAT = 1.45 // radians: stop just short of straight up/down
const TWO_PI = Math.PI * 2
const DEG = Math.PI / 180
const START = { lon: 0, lat: 0.15, zoom: 1 }

// Reads all the pixels of the flat map at a given size, so we can look colors up quickly.
function readPixels(image, width, height) {
  const canvas = document.createElement('canvas')
  canvas.width = width
  canvas.height = height
  const ctx = canvas.getContext('2d', { willReadFrequently: true })
  ctx.drawImage(image, 0, 0, width, height)
  return { data: ctx.getImageData(0, 0, width, height).data, width, height }
}

function clamp(value, low, high) {
  return Math.min(high, Math.max(low, value))
}

function Globe({ mapUrl, name, size = 420, maxZoom = 6, softZoom = 3, marker = null, markerZoom = 4, spin = true }) {
  const canvasRef = useRef(null)
  const fixed = useRef({ mapUrl, size, maxZoom, markerZoom, spin }) // see above
  const map = useRef(null) // { image, widths, levels }: the flat map, read at each size the first time it's needed
  const picture = useRef(null) // the drawing buffer, created on the first draw
  const view = useRef({ ...START }) // where we are looking, and how close
  const spinning = useRef(spin) // slowly turns until the person touches the globe
  const touched = useRef(false) // has the person moved the globe themselves?
  const pointers = useRef(new Map()) // fingers or mouse currently pressing the globe
  const lastPinch = useRef(0)
  const markerRef = useRef(marker) // the newest marker, for the drawing code
  const [zoomShown, setZoomShown] = useState(1)
  const [loaded, setLoaded] = useState(false)

  // The map at the right size for this zoom: about as many map pixels around the equator as screen pixels, so
  // zoomed out it isn't grainy and zoomed in it's as sharp as the map allows.
  function mapFor(radius) {
    const m = map.current
    const wanted = 0.8 * TWO_PI * radius
    const width = m.widths.find((w) => w >= wanted) ?? m.widths[m.widths.length - 1]
    if (!m.levels.has(width)) m.levels.set(width, readPixels(m.image, width, width / 2))
    return m.levels.get(width)
  }

  // Draws the globe once, for the current view.
  function draw() {
    const canvas = canvasRef.current
    if (!canvas || !map.current) return
    const ctx = canvas.getContext('2d')
    const { lon, lat, zoom } = view.current
    const { size } = fixed.current
    const radius = (size / 2 - 8) * zoom // the globe's radius in drawing pixels
    const flat = mapFor(radius)
    // Reuse one picture buffer for every redraw, so spinning stays smooth.
    if (!picture.current) picture.current = ctx.createImageData(size, size)
    const out = picture.current.data
    const center = size / 2
    const sinLat0 = Math.sin(lat)
    const cosLat0 = Math.cos(lat)

    for (let py = 0; py < size; py++) {
      const y = (center - py) / radius
      for (let px = 0; px < size; px++) {
        const x = (px - center) / radius
        const rho2 = x * x + y * y
        const i = (py * size + px) * 4
        if (rho2 > 1) {
          out[i + 3] = 0 // outside the globe: see-through
          continue
        }

        // Which latitude and longitude is under this pixel?
        const rho = Math.sqrt(rho2)
        const cosC = Math.sqrt(1 - rho2)
        let latHere = lat
        let lonHere = lon
        if (rho > 0) {
          latHere = Math.asin(cosC * sinLat0 + y * cosLat0)
          lonHere = lon + Math.atan2(x * rho, rho * cosLat0 * cosC - y * sinLat0 * rho)
        }

        // Look up that spot on the flat map (blending the four nearest map pixels).
        let u = lonHere / TWO_PI + 0.5
        u -= Math.floor(u)
        const v = 0.5 - latHere / Math.PI
        const fx = u * flat.width - 0.5
        const fy = clamp(v * flat.height - 0.5, 0, flat.height - 1)
        const x0 = Math.floor(fx)
        const y0 = Math.floor(fy)
        const tx = fx - x0
        const ty = fy - y0
        const xa = ((x0 % flat.width) + flat.width) % flat.width
        const xb = (xa + 1) % flat.width
        const ya = y0
        const yb = Math.min(flat.height - 1, y0 + 1)
        const a = (ya * flat.width + xa) * 4
        const b = (ya * flat.width + xb) * 4
        const c = (yb * flat.width + xa) * 4
        const d = (yb * flat.width + xb) * 4

        // The edge of the globe is a little darker, which makes it look round.
        const shade = 0.4 + 0.6 * cosC
        for (let k = 0; k < 3; k++) {
          const top = flat.data[a + k] * (1 - tx) + flat.data[b + k] * tx
          const bottom = flat.data[c + k] * (1 - tx) + flat.data[d + k] * tx
          out[i + k] = (top * (1 - ty) + bottom * ty) * shade
        }
        out[i + 3] = 255
      }
    }
    ctx.putImageData(picture.current, 0, 0)
    drawMarker(ctx, center, radius, sinLat0, cosLat0)
  }

  // The marker, if it's on the side of the globe facing us: the same sum as above, the other way round.
  function drawMarker(ctx, center, radius, sinLat0, cosLat0) {
    const point = markerRef.current
    if (!point) return
    const phi = point.lat * DEG
    const dLon = point.lon * DEG - view.current.lon
    const facing = sinLat0 * Math.sin(phi) + cosLat0 * Math.cos(phi) * Math.cos(dLon)
    if (facing < 0) return // behind the globe
    const px = center + radius * Math.cos(phi) * Math.sin(dLon)
    const py = center - radius * (cosLat0 * Math.sin(phi) - sinLat0 * Math.cos(phi) * Math.cos(dLon))
    // The accent color, so it follows color-blind mode
    const color = getComputedStyle(document.documentElement).getPropertyValue('--green').trim() || '#7be3a6'

    ctx.save()
    ctx.lineWidth = 2
    ctx.strokeStyle = 'rgba(0, 0, 0, 0.75)' // a dark outline, so it shows on ice and desert too
    ctx.beginPath()
    ctx.arc(px, py, 11, 0, TWO_PI)
    ctx.stroke()
    ctx.strokeStyle = color
    ctx.beginPath()
    ctx.arc(px, py, 9, 0, TWO_PI)
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      ctx.moveTo(px + dx * 13, py + dy * 13)
      ctx.lineTo(px + dx * 19, py + dy * 19)
    }
    ctx.stroke()
    ctx.fillStyle = color
    ctx.strokeStyle = 'rgba(0, 0, 0, 0.75)'
    ctx.beginPath()
    ctx.arc(px, py, 3.5, 0, TWO_PI)
    ctx.fill()
    ctx.stroke()
    ctx.restore()
  }

  // Changes the view, redraws, and updates the "3.5x" label.
  function moveView(changes) {
    const v = view.current
    v.lon = changes.lon ?? v.lon
    v.lat = clamp(changes.lat ?? v.lat, -MAX_LAT, MAX_LAT)
    v.zoom = clamp(changes.zoom ?? v.zoom, MIN_ZOOM, fixed.current.maxZoom)
    setZoomShown(Math.round(v.zoom * 10) / 10)
    draw()
  }

  // The person moved the globe: it stops spinning, and stops turning to the marker by itself.
  function takeOver() {
    spinning.current = false
    touched.current = true
  }

  function centerOnMarker() {
    const point = markerRef.current
    if (!point) return
    spinning.current = false
    moveView({ lon: point.lon * DEG, lat: point.lat * DEG, zoom: Math.max(view.current.zoom, fixed.current.markerZoom) })
  }

  // How many radians one screen pixel of dragging should turn the globe.
  function radiansPerPixel() {
    const canvas = canvasRef.current
    const { size } = fixed.current
    const shownRadius = ((size / 2 - 8) * view.current.zoom * canvas.clientWidth) / size
    return 1 / shownRadius
  }

  function zoomBy(factor) {
    takeOver()
    moveView({ zoom: view.current.zoom * factor })
  }

  function resetView() {
    spinning.current = fixed.current.spin && !window.matchMedia('(prefers-reduced-motion: reduce)').matches
    moveView(START)
  }

  // ---- Mouse, touch and pen ----
  function handlePointerDown(event) {
    takeOver()
    event.currentTarget.setPointerCapture(event.pointerId)
    pointers.current.set(event.pointerId, { x: event.clientX, y: event.clientY })
    lastPinch.current = 0
  }

  function handlePointerMove(event) {
    const previous = pointers.current.get(event.pointerId)
    if (!previous) return
    const current = { x: event.clientX, y: event.clientY }
    pointers.current.set(event.pointerId, current)

    if (pointers.current.size === 1) {
      // One finger or the mouse: drag to turn the globe.
      const step = radiansPerPixel()
      moveView({
        lon: view.current.lon - (current.x - previous.x) * step,
        lat: view.current.lat + (current.y - previous.y) * step,
      })
    } else if (pointers.current.size === 2) {
      // Two fingers: pinch to zoom.
      const [first, second] = [...pointers.current.values()]
      const gap = Math.hypot(first.x - second.x, first.y - second.y)
      if (lastPinch.current > 0) moveView({ zoom: view.current.zoom * (gap / lastPinch.current) })
      lastPinch.current = gap
    }
  }

  function handlePointerEnd(event) {
    pointers.current.delete(event.pointerId)
    lastPinch.current = 0
  }

  // ---- Keyboard: arrows turn the globe, + and - zoom, 0 resets, C centers on the marker ----
  function handleKeyDown(event) {
    const turn = 0.15 / view.current.zoom
    const actions = {
      ArrowLeft: () => moveView({ lon: view.current.lon - turn }),
      ArrowRight: () => moveView({ lon: view.current.lon + turn }),
      ArrowUp: () => moveView({ lat: view.current.lat + turn }),
      ArrowDown: () => moveView({ lat: view.current.lat - turn }),
      '+': () => zoomBy(1.4),
      '=': () => zoomBy(1.4),
      '-': () => zoomBy(1 / 1.4),
      0: () => resetView(),
      c: () => centerOnMarker(),
    }
    const action = actions[event.key]
    if (action) {
      event.preventDefault()
      takeOver()
      action()
    }
  }

  // A new marker position is drawn at once. The first one turns the globe to it, unless the person got there first.
  const markerLat = marker?.lat ?? null
  const markerLon = marker?.lon ?? null
  useEffect(() => {
    markerRef.current = markerLat === null ? null : { lat: markerLat, lon: markerLon }
    if (markerRef.current && !touched.current) {
      touched.current = true
      centerOnMarker()
    } else {
      draw()
    }
    // draw and centerOnMarker only use refs, so the newest marker is all this needs.
  }, [markerLat, markerLon])

  // Load the flat map once, then draw. Also set up scroll-to-zoom and the slow spin.
  useEffect(() => {
    let cancelled = false
    let frame = 0
    const image = new Image()
    image.src = fixed.current.mapUrl
    image.onload = () => {
      if (cancelled) return
      const widths = []
      for (let width = 1024; width < image.naturalWidth; width *= 2) widths.push(width)
      widths.push(image.naturalWidth)
      map.current = { image, widths, levels: new Map() }
      setLoaded(true)
      if (markerRef.current && !touched.current) {
        touched.current = true
        centerOnMarker()
      } else {
        draw()
      }
    }

    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) spinning.current = false

    function turn() {
      if (spinning.current && map.current) {
        view.current.lon += 0.004
        draw()
      }
      frame = requestAnimationFrame(turn)
    }
    frame = requestAnimationFrame(turn)

    // Scrolling the mouse wheel zooms. This needs a listener that is allowed to cancel the page scroll.
    const canvas = canvasRef.current
    function handleWheel(event) {
      event.preventDefault()
      takeOver()
      moveView({ zoom: view.current.zoom * Math.exp(-event.deltaY * 0.0015) })
    }
    canvas.addEventListener('wheel', handleWheel, { passive: false })

    return () => {
      cancelled = true
      cancelAnimationFrame(frame)
      canvas.removeEventListener('wheel', handleWheel)
    }
    // The helpers above only use refs and setState, so it is safe to set this up just once.
  }, [])

  return (
    <div className="globe">
      <canvas
        ref={canvasRef}
        className="globe-canvas"
        width={size}
        height={size}
        tabIndex={0}
        role="img"
        aria-label={`Interactive globe of ${name}. Drag to turn it, scroll or pinch to zoom. Arrow keys turn it, plus and minus zoom.${
          marker ? ' The robot is marked on it; press C to turn to it.' : ''
        }`}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerEnd}
        onPointerCancel={handlePointerEnd}
        onKeyDown={handleKeyDown}
        onDoubleClick={() => zoomBy(2)}
      />
      {!loaded && <p className="globe-loading">Loading the map of {name}…</p>}

      <div className="globe-controls">
        <button className="button" onClick={() => zoomBy(1 / 1.6)} aria-label="Zoom out" disabled={zoomShown <= MIN_ZOOM}>
          −
        </button>
        <span className="globe-zoom mono" aria-live="polite">
          {zoomShown.toFixed(1)}×
        </span>
        <button className="button" onClick={() => zoomBy(1.6)} aria-label="Zoom in" disabled={zoomShown >= maxZoom}>
          +
        </button>
        {marker && (
          <button className="button" onClick={centerOnMarker}>
            Center on robot
          </button>
        )}
        <button className="button" onClick={resetView}>
          Reset view
        </button>
      </div>
      <p className="globe-hint">
        {zoomShown >= softZoom
          ? 'Close-ups look soft because the map picture is small. There is no finer detail in it.'
          : `Drag to turn ${name}. Scroll, pinch or double-click to zoom. Arrow keys and + / − also work.`}
      </p>
    </div>
  )
}

export default Globe
