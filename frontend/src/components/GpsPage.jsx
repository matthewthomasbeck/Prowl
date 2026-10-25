import './GpsPage.css'
import Globe from './Globe.jsx'
import SectionIcon from './SectionIcon.jsx'
import earthMapUrl from '../assets/earth-map-4k.jpg'
import { earthMap } from '../data/backgroundPhoto.js'
import { useLiveValue } from '../live/useProwlLink.js'

// ---- The GPS page: where the robot is on Earth, and what its GPS receiver hears ----
//   available: is the dashboard connected to the robot?
//   link: from useProwlLink; the newest fix is useLiveValue(link, 'gps'), about once a second.
//   gpsAge: seconds since the GPS driver last sent a fix (null = never).
//   onBack(): back to the map page.
// Unlike the Moon pictures elsewhere on the dashboard, the position on this globe is real.

const STALE_S = 3 // the GPS sends once a second
const UERE_M = 2.5 // metres of error per unit of HDOP, as the GPS driver assumes (its uere parameter)
const MOVING_MPS = 0.3 // below this the receiver's course is noise
const SIGNAL_MAX = 50 // dBHz: a strong signal fills the bar
const COMPASS = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW']

function formatDegrees(value, positive, negative) {
  return `${Math.abs(value).toFixed(6)}° ${value >= 0 ? positive : negative}`
}

function formatUtc(seconds) {
  const iso = new Date(seconds * 1000).toISOString()
  return `${iso.slice(0, 10)} ${iso.slice(11, 19)} UTC`
}

// What the tag in the title says, and what the globe says over it, if anything.
function describe(gps, gpsAge) {
  if (gpsAge === null || gpsAge === undefined || !gps) {
    return { tag: 'Offline', live: false, note: 'GPS offline: the GPS driver is not running.' }
  }
  if (gpsAge > STALE_S) {
    const last = gps.fix === 'none' ? '' : ' The marker shows its last position.'
    return { tag: '⚠ Stalled', live: false, note: `No word from the GPS for ${Math.round(gpsAge)} s.${last}` }
  }
  if (gps.fix === 'none') {
    const count = gps.satellites.length
    return {
      tag: '○ Searching',
      live: false,
      note: `No fix yet: ${count} satellite${count === 1 ? '' : 's'} in view. The GPS needs a clear view of the sky, and its first fix can take a minute or more.`,
    }
  }
  return { tag: gps.fix === '3d' ? '● 3D fix' : '● 2D fix', live: true, note: null }
}

// The sky as the robot sees it: the middle is straight up, the edge is the horizon, north at the top.
function SkyPlot({ satellites }) {
  const R = 90 // the horizon, in the SVG's units
  const placed = satellites.filter((s) => s.elevation !== null && s.azimuth !== null)
  const used = satellites.filter((s) => s.used).length
  return (
    <svg
      className="sky-plot"
      viewBox="-112 -112 224 224"
      role="img"
      aria-label={`Sky plot: ${satellites.length} satellites in view, ${used} used for the fix.`}
    >
      {[90, 60, 30].map((r) => (
        <circle key={r} className="sky-ring" r={r} />
      ))}
      <path className="sky-ring" d={`M0 ${-R}V${R}M${-R} 0H${R}`} />
      <text className="sky-label" x="0" y={-R - 7}>N</text>
      <text className="sky-label" x={R + 9} y="0">E</text>
      <text className="sky-label" x="0" y={R + 8}>S</text>
      <text className="sky-label" x={-R - 9} y="0">W</text>
      {placed.map((s) => {
        const r = ((90 - s.elevation) / 90) * R
        const a = (s.azimuth * Math.PI) / 180
        const state = s.used ? 'used' : s.cno !== null ? 'tracked' : 'idle'
        return (
          <g key={s.id} transform={`translate(${r * Math.sin(a)} ${-r * Math.cos(a)})`}>
            <circle className={`sky-sat sky-sat-${state}`} r="8" />
            <text className={`sky-id sky-id-${state}`}>{s.id}</text>
          </g>
        )
      })}
    </svg>
  )
}

function GpsPage({ available, link, gpsAge, onBack }) {
  const gps = useLiveValue(link, 'gps')

  if (!available) {
    return (
      <main className="gps-page">
        <section className="panel gps-view">
          <div className="panel-heading">
            <h2>
              <SectionIcon name="globe" />
              Robot position
            </h2>
            <button className="button gps-back" onClick={onBack}>
              Back to the map
            </button>
          </div>
          <p className="gps-empty">The robot's GPS appears here when the dashboard is connected to the robot.</p>
        </section>
      </main>
    )
  }

  const { tag, live, note } = describe(gps, gpsAge)
  const hasFix = gps && gps.fix !== 'none'
  const satellites = gps?.satellites ?? []
  const moving = hasFix && gps.speed !== null && gps.speed >= MOVING_MPS

  const rows = [
    ['Latitude', hasFix ? formatDegrees(gps.lat, 'N', 'S') : '—'],
    ['Longitude', hasFix ? formatDegrees(gps.lon, 'E', 'W') : '—'],
    ['Altitude', hasFix && gps.alt !== null ? `${gps.alt.toFixed(1)} m above sea level` : '—'],
    ['Accuracy', hasFix && gps.hdop !== null ? `± ${Math.max(1, Math.round(gps.hdop * UERE_M))} m (HDOP ${gps.hdop.toFixed(2)})` : '—'],
    ['Speed', hasFix && gps.speed !== null ? `${gps.speed.toFixed(1)} m/s` : '—'],
    [
      'Heading',
      moving && gps.course !== null ? `${Math.round(gps.course)}° ${COMPASS[Math.round(gps.course / 45) % 8]}` : hasFix ? 'standing still' : '—',
    ],
    ['Satellites', gps ? `${gps.satellites_used} used, ${satellites.length} in view` : '—'],
    ['GPS time', gps?.utc ? formatUtc(gps.utc) : '—'],
  ]

  return (
    <main className="gps-page">
      <section className="panel gps-view">
        <div className="panel-heading">
          <h2>
            <SectionIcon name="globe" />
            Robot position
          </h2>
          <span className={live ? 'tag gps-tag-live' : 'tag'}>{tag}</span>
        </div>
        <div className="gps-globe">
          <Globe
            mapUrl={earthMapUrl}
            name="the Earth"
            size={520}
            maxZoom={12}
            softZoom={3.5}
            marker={hasFix ? { lat: gps.lat, lon: gps.lon } : null}
          />
          {note && (
            <p className="gps-note" role="status">
              {note}
            </p>
          )}
        </div>
        <p className="gps-credit">
          Earth map: {earthMap.sourceName},{' '}
          <a href={earthMap.sourceUrl} target="_blank" rel="noreferrer">
            {earthMap.credit}
          </a>
          . {earthMap.note}
        </p>
      </section>

      <section className="panel gps-side">
        <div className="gps-side-scroll">
          <div className="panel-heading">
            <h2>
              <SectionIcon name="map" />
              Fix
            </h2>
          </div>
          <dl className="gps-readouts">
            {rows.map(([label, value]) => (
              <div key={label} className="gps-readout">
                <dt className="field-label">{label}</dt>
                <dd className="mono">{value}</dd>
              </div>
            ))}
          </dl>

          <div className="panel-heading gps-sats-heading">
            <h2>
              <SectionIcon name="satellite" />
              Satellites
            </h2>
          </div>
          {satellites.length === 0 ? (
            <p className="gps-empty">
              {gps ? 'None in view. Indoors the GPS hears nothing: it needs open sky.' : 'Nothing from the GPS yet.'}
            </p>
          ) : (
            <>
              <SkyPlot satellites={satellites} />
              <p className="gps-legend">
                <span className="gps-key gps-key-used" aria-hidden="true"></span> used
                <span className="gps-key gps-key-tracked" aria-hidden="true"></span> heard
                <span className="gps-key gps-key-idle" aria-hidden="true"></span> expected, not heard
              </p>
              <ul className="gps-signals" aria-label="Signal strength of each satellite">
                {satellites.map((s) => (
                  <li key={s.id} className="gps-signal">
                    <span className="mono gps-signal-id">{s.id}</span>
                    <span className="gps-signal-bar" aria-hidden="true">
                      <span
                        className={s.used ? 'gps-signal-fill used' : 'gps-signal-fill'}
                        style={{ width: `${Math.min(100, ((s.cno ?? 0) / SIGNAL_MAX) * 100)}%` }}
                      ></span>
                    </span>
                    <span className="mono gps-signal-value">
                      {s.cno === null ? 'not heard' : `${s.cno} dBHz`}
                      {s.used ? ' ✓' : ''}
                    </span>
                  </li>
                ))}
              </ul>
            </>
          )}
        </div>
      </section>
    </main>
  )
}

export default GpsPage
