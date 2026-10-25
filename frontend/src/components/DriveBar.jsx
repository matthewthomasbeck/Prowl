import { useEffect, useRef, useState } from 'react'
import './DriveBar.css'
import { useLiveValue } from '../live/useProwlLink.js'

// ---- Remote control: a bar along the bottom of every page, like a game's controls ----
// Robot only (the dashboard bridge). App keeps one of these under whichever page is open, so the keyboard switch
// stays as it is when the page changes. The robot moves only while a key or button is held: src/live/useDrive.js
// repeats the command 10 times a second and sends a zero when it's let go, when the window loses focus or when
// the tab is hidden. Stop always works, even while another dashboard is driving.
// Commands are always full speed: on Prowl's wheels, driving slowly is harder on them than driving fast.
//
// Also shows what the robot reports back: the four wheel duties, the IMU, and whether each sensor is sending.
//   drive: from useDrive(link). canCommand: may this page drive? onToken(token): the token box was filled in.
//   error: the newest drive error from the robot (e.g. "another dashboard is driving"), or null.
//   link: from useProwlLink, for the wheel duties and the IMU, which only their own readouts re-draw for (they change
//   10 and 5 times a second). topicAges: status.topic_age_s, seconds since each topic arrived.
//   motors: status.motors, whether driving reaches the wheels: 'live', 'dry_run', null (the motor driver isn't
//   answering), or undefined (an older bridge that doesn't say).

const STALE_S = 2

// Why the robot won't move even though the controls work
const MOTOR_WARNINGS = {
  dry_run: "Motors in dry run: the wheel readout responds, but the robot won't move. Restart the robot's launch with live:=true.",
  null: "The motor driver isn't answering, so the robot can't move.",
}

// What each key does: x forward, y left (strafe), turn counter-clockwise. The robot has mecanum wheels.
const KEYS = {
  w: { x: 1 },
  arrowup: { x: 1 },
  s: { x: -1 },
  arrowdown: { x: -1 },
  a: { y: 1 },
  d: { y: -1 },
  q: { turn: 1 },
  arrowleft: { turn: 1 },
  e: { turn: -1 },
  arrowright: { turn: -1 },
}
const STOP_KEYS = [' ', 'x']

// The six drive buttons, in the same layout as the keys: Q W E over A S D.
const BUTTONS = [
  { key: 'Q', label: 'Turn left', move: { turn: 1 }, icon: 'turn-left' },
  { key: 'W', label: 'Forward', move: { x: 1 }, icon: 'up' },
  { key: 'E', label: 'Turn right', move: { turn: -1 }, icon: 'turn-right' },
  { key: 'A', label: 'Slide left', move: { y: 1 }, icon: 'left' },
  { key: 'S', label: 'Backward', move: { x: -1 }, icon: 'down' },
  { key: 'D', label: 'Slide right', move: { y: -1 }, icon: 'right' },
]

const ARROW_TURN = { up: 0, right: 90, down: 180, left: -90 }

function DriveIcon({ name }) {
  if (name.startsWith('turn')) {
    // a curved arrow; the right turn is the left one mirrored
    return (
      <svg className="drive-icon" viewBox="0 0 24 24" aria-hidden="true" style={name === 'turn-right' ? { transform: 'scaleX(-1)' } : undefined}>
        <path className="drive-icon-line" d="M17 18a7 7 0 1 0-9.5-9.5" />
        <path d="M5 4.5v5.5h5.5z" />
      </svg>
    )
  }
  return (
    <svg className="drive-icon" viewBox="0 0 24 24" aria-hidden="true" style={{ transform: `rotate(${ARROW_TURN[name]}deg)` }}>
      <path d="M12 6l7 11H5z" />
    </svg>
  )
}

const WHEELS = [
  { id: 'front_left', short: 'FL', name: 'Front left' },
  { id: 'front_right', short: 'FR', name: 'Front right' },
  { id: 'rear_left', short: 'RL', name: 'Rear left' },
  { id: 'rear_right', short: 'RR', name: 'Rear right' },
]

function signed(value, places = 2) {
  const text = Math.abs(value).toFixed(places)
  if (Number(text) === 0) return (0).toFixed(places)
  return `${value > 0 ? '+' : '−'}${text}`
}

const degrees = (radians) => (radians * 180) / Math.PI

// Tilt from the accelerometer, which feels gravity: roll is leaning left or right, pitch is nose up or down.
// The accelerometer isn't calibrated yet (it reads gravity about 7 % low), which doesn't change these angles.
function tilt(accel) {
  const [ax, ay, az] = accel
  return { roll: degrees(Math.atan2(ay, az)), pitch: degrees(Math.atan2(-ax, Math.hypot(ay, az))) }
}

// The four duties the motor driver applies, laid out as the robot seen from above. + pushes the robot forward.
function Wheels({ link }) {
  const motorStatus = useLiveValue(link, 'motorStatus')
  if (!motorStatus) return <p className="drive-empty">No report from the motor driver yet.</p>
  const duties = {}
  motorStatus.wheels?.forEach((wheel, i) => (duties[wheel] = motorStatus.duties?.[i] ?? 0))
  return (
    <div className="wheel-grid">
      {WHEELS.map((wheel) => {
        const duty = Math.max(-1, Math.min(1, duties[wheel.id] ?? 0))
        return (
          <div className="wheel" key={wheel.id} role="img" aria-label={`${wheel.name} wheel: duty ${signed(duty)}`}>
            <span className="wheel-name">{wheel.short}</span>
            <span className="wheel-bar" aria-hidden="true">
              {/* starts in the middle and grows right for +, left for − (a transform, so no re-layout) */}
              <span className="wheel-fill" style={{ transform: `scaleX(${duty})` }}></span>
            </span>
            <span className="wheel-duty mono">{signed(duty)}</span>
          </div>
        )
      })}
    </div>
  )
}

const LEVEL_REACH_PX = 17 // how far the bubble can move from the middle of its 44 px level

function Imu({ link }) {
  const imu = useLiveValue(link, 'imu')
  if (!imu) return <p className="drive-empty">Waiting for the IMU. It calibrates for 2 s after it starts: keep the robot still.</p>
  const turnRate = degrees(imu.gyro[2])
  const lean = tilt(imu.accel)
  const dx = (Math.max(-30, Math.min(30, lean.roll)) / 30) * LEVEL_REACH_PX
  const dy = (-Math.max(-30, Math.min(30, lean.pitch)) / 30) * LEVEL_REACH_PX
  return (
    <div className="imu">
      {/* A bubble level: the dot moves the way the robot leans, up to 30° at the edge */}
      <span className="imu-level" aria-hidden="true">
        <span className="imu-bubble" style={{ transform: `translate(${dx.toFixed(1)}px, ${dy.toFixed(1)}px)` }}></span>
      </span>
      <dl className="imu-values">
        <div>
          <dt>Turning</dt>
          <dd className="mono">
            {signed(turnRate, 1)} °/s{' '}
            <span className="imu-word">{Math.abs(turnRate) < 1 ? 'steady' : turnRate > 0 ? 'left' : 'right'}</span>
          </dd>
        </div>
        <div>
          <dt>Roll</dt>
          <dd className="mono">{signed(lean.roll, 1)}°</dd>
        </div>
        <div>
          <dt>Pitch</dt>
          <dd className="mono">{signed(lean.pitch, 1)}°</dd>
        </div>
      </dl>
    </div>
  )
}

const SENSORS = [
  { topic: 'scan', name: 'LiDAR' },
  { topic: 'motor_status', name: 'Motors' },
  { topic: 'imu', name: 'IMU' },
]

function sensorState(age) {
  if (age === null || age === undefined) return { symbol: '○', text: 'not started', className: 'sensor-off' }
  if (age > STALE_S) return { symbol: '⚠', text: `quiet ${Math.round(age)} s`, className: 'sensor-stale' }
  return { symbol: '✓', text: 'sending', className: 'sensor-ok' }
}

function DriveBar({ drive, canCommand, onToken, error, link, topicAges, motors }) {
  const [keyboardOn, setKeyboardOn] = useState(false) // off until switched on, so keys never surprise anyone
  const [tokenDraft, setTokenDraft] = useState('')

  // The key listener stays the same while keys are held (rebuilding it would let go of them), so it reads drive here.
  const driveRef = useRef(drive)
  useEffect(() => {
    driveRef.current = drive
  })

  // Keyboard on: each key down or up sends the sum of the keys still held, and none held lets go. Holding a key
  // doesn't repeat anything here; useDrive keeps sending while something is held, and lets go if the window loses
  // focus, so a key-up that never arrives can't leave the robot moving. Space or X stops the robot.
  useEffect(() => {
    if (!keyboardOn || !canCommand) return
    const held = new Map()
    const skip = (event) =>
      event.ctrlKey || event.metaKey || event.altKey || event.target.closest('input, textarea, select') || document.querySelector('dialog[open]')
    function apply() {
      const total = { x: 0, y: 0, turn: 0 }
      for (const move of held.values()) for (const axis in move) total[axis] += move[axis]
      for (const axis in total) total[axis] = Math.max(-1, Math.min(1, total[axis]))
      driveRef.current.set(total)
    }
    function onKeyDown(event) {
      const key = event.key.toLowerCase()
      if (skip(event)) return
      if (STOP_KEYS.includes(key)) {
        event.preventDefault()
        held.clear()
        driveRef.current.stop()
        return
      }
      const move = KEYS[key]
      if (!move) return
      event.preventDefault() // no page scrolling
      event.stopPropagation() // and the map's own arrow keys don't pan it as well
      if (event.repeat || held.has(key)) return
      held.set(key, move)
      apply()
    }
    function onKeyUp(event) {
      const key = event.key.toLowerCase()
      if (!held.has(key)) return
      held.delete(key)
      apply()
    }
    const forget = () => held.clear()
    // "true" listens before anything else, so the map never sees these keys
    document.addEventListener('keydown', onKeyDown, true)
    document.addEventListener('keyup', onKeyUp, true)
    window.addEventListener('blur', forget)
    return () => {
      document.removeEventListener('keydown', onKeyDown, true)
      document.removeEventListener('keyup', onKeyUp, true)
      window.removeEventListener('blur', forget)
      if (held.size > 0) driveRef.current.release()
    }
  }, [keyboardOn, canCommand])

  let readout = 'Not driving'
  if (!canCommand) readout = 'Locked: needs the drive token'
  else if (drive.driving) readout = 'Driving'

  return (
    <section className="panel drive-bar" aria-label="Remote control">
      <div className="drive-group drive-controls">
        <p className="field-label drive-title">
          Remote control <span className="tag">Live robot</span>
        </p>
        <div className="drive-pad">
          <div className="drive-keys" role="group" aria-label="Drive: hold a button to move">
            {BUTTONS.map((button) => (
              <button
                key={button.key}
                className="button drive-key"
                {...drive.hold(button.move)}
                disabled={!canCommand}
                aria-label={`${button.label} (hold)`}
                title={`${button.label}: hold (key ${button.key})`}
              >
                <DriveIcon name={button.icon} />
                {keyboardOn && <span className="drive-key-hint mono">{button.key}</span>}
              </button>
            ))}
          </div>
          {/* Stop is the biggest thing here, and works for anyone: it stops whoever is driving */}
          <button className="button button-primary drive-stop" onClick={drive.stop} aria-label="Stop the robot">
            Stop
            {keyboardOn && <span className="drive-key-hint mono">Space</span>}
          </button>
        </div>
      </div>

      <div className="drive-group drive-status">
        <button className="button drive-toggle" onClick={() => setKeyboardOn((on) => !on)} aria-pressed={keyboardOn} disabled={!canCommand}>
          Keyboard
          <span className={keyboardOn ? 'switch-state on' : 'switch-state'}>{keyboardOn ? 'On' : 'Off'}</span>
        </button>
        <p className={drive.driving ? 'drive-readout driving' : 'drive-readout'} role="status">
          <span className="drive-dot" aria-hidden="true"></span>
          {readout}
        </p>
        {motors !== undefined && MOTOR_WARNINGS[motors] && <p className="drive-error">⚠ {MOTOR_WARNINGS[motors]}</p>}
        {error && <p className="drive-error">⚠ {error}</p>}
        <p className="drive-hint">
          {keyboardOn ? 'Hold W S to drive, A D to slide, Q E or ← → to turn. Space stops.' : 'Hold a button to move. Let go to stop.'}
        </p>
        {!canCommand && (
          <form
            className="drive-token"
            onSubmit={(event) => {
              event.preventDefault()
              onToken(tokenDraft.trim())
            }}
          >
            <label htmlFor="drive-token" className="field-label">
              Robot drive token
            </label>
            <span className="drive-token-row">
              <input
                id="drive-token"
                type="password"
                autoComplete="off"
                value={tokenDraft}
                onChange={(event) => setTokenDraft(event.target.value)}
                placeholder="drive_token in the robot's config"
              />
              <button className="button" type="submit" disabled={!tokenDraft.trim()}>
                Unlock
              </button>
            </span>
          </form>
        )}
      </div>

      <div className="drive-group">
        <p className="field-label">Wheels · seen from above</p>
        <Wheels link={link} />
      </div>

      <div className="drive-group">
        <p className="field-label">IMU</p>
        <Imu link={link} />
      </div>

      <div className="drive-group">
        <p className="field-label">Sensors</p>
        <ul className="sensor-list">
          {SENSORS.map(({ topic, name }) => {
            const state = sensorState(topicAges?.[topic])
            return (
              <li key={topic} className={state.className}>
                <span aria-hidden="true">{state.symbol}</span> {name} <span className="sensor-text">{state.text}</span>
              </li>
            )
          })}
        </ul>
      </div>
    </section>
  )
}

export default DriveBar
