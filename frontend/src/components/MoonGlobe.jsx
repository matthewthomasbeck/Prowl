import { useRef, useEffect, useState } from 'react'
import moonMapUrl from '../assets/moon-map-2k.jpg'
import './MoonGlobe.css'

// The globe is drawn by hand on a <canvas>, with no 3D library.
// For every pixel of the picture we work out which spot on the Moon it shows, then copy that
// spot's color from the flat NASA Moon map. That is the whole trick.

const SIZE = 420 // the canvas is 420 x 420 drawing pixels (CSS scales it to fit the window)
const MIN_ZOOM = 1
const MAX_ZOOM = 6 // the map picture is small, so zooming further would only make it blurrier
const MAX_LAT = 1.45 // radians: stop just short of straight up/down
const TWO_PI = Math.PI * 2

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

function MoonGlobe() {
  const canvasRef = useRef(null)
  const maps = useRef(null) // the flat Moon map, at two sizes: { small, large }
  const picture = useRef(null) // the drawing buffer, created on the first draw
  const view = useRef({ lon: 0, lat: 0.15, zoom: 1 }) // where we are looking, and how close
  const spinning = useRef(true) // slowly turns until the person touches the globe
  const pointers = useRef(new Map()) // fingers or mouse currently pressing the globe
  const lastPinch = useRef(0)
  const [zoomShown, setZoomShown] = useState(1)
  const [loaded, setLoaded] = useState(false)

  // Draws the globe once, for the current view.
  function draw() {
    const canvas = canvasRef.current
    if (!canvas || !maps.current) return
    const ctx = canvas.getContext('2d')
    const { lon, lat, zoom } = view.current
    const map = zoom < 1.8 ? maps.current.small : maps.current.large
    // Reuse one picture buffer for every redraw, so spinning stays smooth.
    if (!picture.current) picture.current = ctx.createImageData(SIZE, SIZE)
    const out = picture.current.data
    const radius = (SIZE / 2 - 8) * zoom // the Moon's radius in drawing pixels
    const center = SIZE / 2
    const sinLat0 = Math.sin(lat)
    const cosLat0 = Math.cos(lat)

    for (let py = 0; py < SIZE; py++) {
      const y = (center - py) / radius
      for (let px = 0; px < SIZE; px++) {
        const x = (px - center) / radius
        const rho2 = x * x + y * y
        const i = (py * SIZE + px) * 4
        if (rho2 > 1) {
          out[i + 3] = 0 // outside the Moon: see-through
          continue
        }

        // Which latitude and longitude on the Moon is under this pixel?
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
        const fx = u * map.width - 0.5
        const fy = clamp(v * map.height - 0.5, 0, map.height - 1)
        const x0 = Math.floor(fx)
        const y0 = Math.floor(fy)
        const tx = fx - x0
        const ty = fy - y0
        const xa = ((x0 % map.width) + map.width) % map.width
        const xb = (xa + 1) % map.width
        const ya = y0
        const yb = Math.min(map.height - 1, y0 + 1)
        const a = (ya * map.width + xa) * 4
        const b = (ya * map.width + xb) * 4
        const c = (yb * map.width + xa) * 4
        const d = (yb * map.width + xb) * 4

        // The edge of the Moon is a little darker, which makes it look round.
        const shade = 0.4 + 0.6 * cosC
        for (let k = 0; k < 3; k++) {
          const top = map.data[a + k] * (1 - tx) + map.data[b + k] * tx
          const bottom = map.data[c + k] * (1 - tx) + map.data[d + k] * tx
          out[i + k] = (top * (1 - ty) + bottom * ty) * shade
        }
        out[i + 3] = 255
      }
    }
    ctx.putImageData(picture.current, 0, 0)
  }

  // Changes the view, redraws, and updates the "3.5x" label.
  function moveView(changes) {
    const v = view.current
    v.lon = changes.lon ?? v.lon
    v.lat = clamp(changes.lat ?? v.lat, -MAX_LAT, MAX_LAT)
    v.zoom = clamp(changes.zoom ?? v.zoom, MIN_ZOOM, MAX_ZOOM)
    setZoomShown(Math.round(v.zoom * 10) / 10)
    draw()
  }

  function stopSpinning() {
    spinning.current = false
  }

  // How many radians one screen pixel of dragging should turn the globe.
  function radiansPerPixel() {
    const canvas = canvasRef.current
    const shownRadius = ((SIZE / 2 - 8) * view.current.zoom * canvas.clientWidth) / SIZE
    return 1 / shownRadius
  }

  function zoomBy(factor) {
    stopSpinning()
    moveView({ zoom: view.current.zoom * factor })
  }

  function resetView() {
    spinning.current = !window.matchMedia('(prefers-reduced-motion: reduce)').matches
    moveView({ lon: 0, lat: 0.15, zoom: 1 })
  }

  // ---- Mouse, touch and pen ----
  function handlePointerDown(event) {
    stopSpinning()
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

  // ---- Keyboard: arrows turn the globe, + and - zoom, 0 resets ----
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
    }
    const action = actions[event.key]
    if (action) {
      event.preventDefault()
      stopSpinning()
      action()
    }
  }

  // Load the flat Moon map once, then draw. Also set up scroll-to-zoom and the slow spin.
  useEffect(() => {
    let cancelled = false
    let frame = 0
    const image = new Image()
    image.src = moonMapUrl
    image.onload = () => {
      if (cancelled) return
      maps.current = {
        small: readPixels(image, 1024, 512),
        large: readPixels(image, image.naturalWidth, image.naturalHeight),
      }
      setLoaded(true)
      draw()
    }

    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) spinning.current = false

    function spin() {
      if (spinning.current && maps.current) {
        view.current.lon += 0.004
        draw()
      }
      frame = requestAnimationFrame(spin)
    }
    frame = requestAnimationFrame(spin)

    // Scrolling the mouse wheel zooms. This needs a listener that is allowed to cancel the page scroll.
    const canvas = canvasRef.current
    function handleWheel(event) {
      event.preventDefault()
      spinning.current = false
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
    <div className="moon-globe">
      <canvas
        ref={canvasRef}
        className="moon-canvas"
        width={SIZE}
        height={SIZE}
        tabIndex={0}
        role="img"
        aria-label="Interactive globe of the Moon. Drag to turn it, scroll or pinch to zoom. Arrow keys turn it, plus and minus zoom."
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerEnd}
        onPointerCancel={handlePointerEnd}
        onKeyDown={handleKeyDown}
        onDoubleClick={() => zoomBy(2)}
      />
      {!loaded && <p className="moon-loading">Loading the Moon map…</p>}

      <div className="moon-controls">
        <button className="button" onClick={() => zoomBy(1 / 1.6)} aria-label="Zoom out" disabled={zoomShown <= MIN_ZOOM}>
          −
        </button>
        <span className="moon-zoom mono" aria-live="polite">
          {zoomShown.toFixed(1)}×
        </span>
        <button className="button" onClick={() => zoomBy(1.6)} aria-label="Zoom in" disabled={zoomShown >= MAX_ZOOM}>
          +
        </button>
        <button className="button" onClick={resetView}>
          Reset view
        </button>
      </div>
      <p className="moon-hint">
        {zoomShown >= 3
          ? 'Close-ups look soft because the map picture is small. There is no finer detail in it.'
          : 'Drag to turn the Moon. Scroll, pinch or double-click to zoom. Arrow keys and + / − also work.'}
      </p>
    </div>
  )
}

export default MoonGlobe
