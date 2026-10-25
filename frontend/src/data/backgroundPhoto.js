// Facts about the NASA images, kept here so the on-screen credits and docs/credits.md agree.
// The Moon pictures are DECORATION: not robot footage, and they do not show the (fictional) mapped location.

// The page background. It is the same map the interactive Moon globe uses.
export const backgroundPhoto = {
  description: 'a map of the whole Moon (LROC color map)',
  credit: "NASA's Scientific Visualization Studio",
  sourceName: 'CGI Moon Kit',
  sourceUrl: 'https://svs.gsfc.nasa.gov/4720',
}

// The picture of lunar ground under the exploration map. It is a real photo, but the mission area drawn on it
// is fictional, so it is NOT a real place and the robot is not really there.
export const mapGround = {
  description: 'Apollo 16 photo of the Moon (far side, near Guyot crater)',
  credit: 'NASA',
  sourceName: 'NASA Image and Video Library, AS16-121-19407',
  sourceUrl: 'https://images.nasa.gov/details/as16-121-19407',
}

// The map that is wrapped around the interactive Moon globe.
export const moonMap = {
  credit: "NASA's Scientific Visualization Studio",
  sourceName: 'CGI Moon Kit (LROC color map)',
  sourceUrl: 'https://svs.gsfc.nasa.gov/4720',
  note: 'Optimized for looks, not for science.',
}

// The map that is wrapped around the GPS page's Earth globe. Unlike the Moon pictures, the robot's position marked on
// it is real: it comes from the robot's GPS.
export const earthMap = {
  credit: 'NASA Earth Observatory',
  sourceName: 'Blue Marble: Next Generation, December, with topography and bathymetry',
  sourceUrl: 'https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-topography-bathymetry/',
  note: 'A cloud-free composite from 2004, so it shows no weather and nothing built since.',
}
