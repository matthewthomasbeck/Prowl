import { useState, useEffect } from 'react'

// Turns a number of seconds into text like "T+00:58:12".
export function formatElapsed(totalSeconds) {
  const hours = String(Math.floor(totalSeconds / 3600)).padStart(2, '0')
  const minutes = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, '0')
  const seconds = String(totalSeconds % 60).padStart(2, '0')
  return `T+${hours}:${minutes}:${seconds}`
}

// A simulated mission clock: starts at a made-up time and adds one second every second.
export function useMissionClock(startSeconds) {
  const [seconds, setSeconds] = useState(startSeconds)

  useEffect(() => {
    const timer = setInterval(() => setSeconds((current) => current + 1), 1000)
    return () => clearInterval(timer) // stop the timer if the page goes away
  }, [])

  return seconds
}
