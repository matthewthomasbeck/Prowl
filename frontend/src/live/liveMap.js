// Turns the robot's LiDAR map into a picture that sits on top of the exploration map.
//
// The backend sends the map as a grayscale PNG: black = something solid (rock, wall), white = open
// ground the LiDAR has seen, gray = not seen yet. Here it is recolored so that:
//   solid  -> bright outline color
//   open   -> a faint green tint (the area the robot has actually mapped)
//   unseen -> fully see-through, so the decorative ground photo shows
// Row 0 of the image is the top of the map, which matches how SVG draws images, so no flipping is needed.

function accentRgb() {
  const value = getComputedStyle(document.documentElement).getPropertyValue('--green-rgb').trim()
  const parts = value.split(',').map((part) => Number(part.trim()))
  return parts.length === 3 && parts.every((part) => Number.isFinite(part)) ? parts : [123, 227, 166]
}

export function recolorMap(message) {
  return new Promise((resolve) => {
    const image = new Image()
    image.onload = () => {
      const canvas = document.createElement('canvas')
      canvas.width = image.width
      canvas.height = image.height
      const context = canvas.getContext('2d')
      context.drawImage(image, 0, 0)
      const pixels = context.getImageData(0, 0, canvas.width, canvas.height)
      const data = pixels.data
      const [r, g, b] = accentRgb()
      for (let i = 0; i < data.length; i += 4) {
        const value = data[i]
        if (value < 60) {
          data[i] = 241 // solid: near-white, like the page text
          data[i + 1] = 244
          data[i + 2] = 246
          data[i + 3] = 235
        } else if (value > 230) {
          data[i] = r // open ground the LiDAR has seen
          data[i + 1] = g
          data[i + 2] = b
          data[i + 3] = 46
        } else {
          data[i + 3] = 0 // not seen yet
        }
      }
      context.putImageData(pixels, 0, 0)
      resolve({
        href: canvas.toDataURL('image/png'),
        origin: message.origin,
        width: message.width,
        height: message.height,
        resolution: message.resolution,
      })
    }
    image.onerror = () => resolve(null)
    image.src = `data:image/png;base64,${message.data}`
  })
}
