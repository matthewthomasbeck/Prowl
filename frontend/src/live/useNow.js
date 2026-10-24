import { useEffect, useState } from 'react'

// The current time in seconds, updated once a second. Used for the live mission clock.
export function useNow() {
  const [now, setNow] = useState(() => Date.now() / 1000)

  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now() / 1000), 1000)
    return () => clearInterval(timer)
  }, [])

  return now
}
