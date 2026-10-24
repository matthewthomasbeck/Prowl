import { useCallback, useEffect, useRef, useState } from 'react'

// ---- Hold-to-drive for the robot bridge ----
// The bridge on the robot (edge/src/prowl_dashboard_bridge) keeps the robot moving only while drive
// messages keep arriving, and stops it 0.3 s after the last one. So while a control is held, this sends
// the current command 10 times a second, and when it's let go it sends a zero straight away.
// Letting go, switching to another window or tab, and closing the page all count as letting go. If the
// connection drops, the bridge stops the robot by itself.
//
// Commands are fractions of the robot's top speeds (set on the robot), each from -1 to 1:
//   x     forward (+) / backward (-)
//   y     strafe left (+) / right (-)
//   turn  counter-clockwise (+) / clockwise (-)
//
// With the link from useProwlLink():
//   const drive = useDrive(link)
//   <button {...drive.hold({ x: 1 })}>Forward</button>   // drives while pressed (mouse, touch or pen)
//   drive.set({ x: 0.5, turn: -0.2 })                     // e.g. from a joystick; set({}) lets go
//   drive.stop()                                          // stop button: stops the robot whoever is driving
//
// If the robot has a drive token set, open the dashboard with ?token=<drive token> to be allowed to drive.
// Without it the bridge answers each drive message with an error, which useProwlLink logs to the console.

const SEND_MS = 100
const STILL = { x: 0, y: 0, turn: 0 }

function command({ x = 0, y = 0, turn = 0 }) {
  return x === 0 && y === 0 && turn === 0 ? STILL : { x, y, turn }
}

export function useDrive(link) {
  const send = useRef(link.send)
  const current = useRef(STILL) // the command being held, or STILL
  const [driving, setDriving] = useState(false)

  useEffect(() => {
    send.current = link.send
  })

  // Sends the held command. Returns false (and lets go) if there is no connection right now.
  const push = useCallback(() => {
    if (send.current({ type: 'drive', ...current.current })) return true
    current.current = STILL
    setDriving(false)
    return false
  }, [])

  // A zero drive, not a stop: if someone else is driving, letting go of a button here must not stop them.
  const release = useCallback(() => {
    if (current.current === STILL) return
    current.current = STILL
    setDriving(false)
    send.current({ type: 'drive', ...STILL })
  }, [])

  useEffect(() => {
    const timer = setInterval(() => {
      if (current.current !== STILL) push()
    }, SEND_MS)
    const onVisibility = () => {
      if (document.hidden) release()
    }
    window.addEventListener('blur', release)
    document.addEventListener('visibilitychange', onVisibility)
    return () => {
      clearInterval(timer)
      window.removeEventListener('blur', release)
      document.removeEventListener('visibilitychange', onVisibility)
      release()
    }
  }, [push, release])

  const set = useCallback(
    (request) => {
      const next = command(request)
      if (next === STILL) return release()
      current.current = next
      if (push()) setDriving(true) // the first message goes out now, not on the next tick
    },
    [push, release],
  )

  const stop = useCallback(() => {
    current.current = STILL
    setDriving(false)
    send.current({ type: 'stop' })
  }, [])

  // Props for a button that drives while it's held down.
  const hold = useCallback(
    (request) => ({
      onPointerDown: (event) => {
        event.currentTarget.setPointerCapture?.(event.pointerId) // keeps the release even if the pointer slides off
        set(request)
      },
      onPointerUp: release,
      onPointerCancel: release,
      onLostPointerCapture: release,
      onContextMenu: (event) => event.preventDefault(), // a long press on a phone would open a menu instead
    }),
    [set, release],
  )

  return { driving, set, stop, hold, release }
}
