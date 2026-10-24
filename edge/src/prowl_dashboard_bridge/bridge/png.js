// Minimal PNG encoder for 8-bit grayscale images, written from the PNG specification
// (W3C PNG, 3rd edition): an 8-byte signature, then IHDR, IDAT and IEND chunks. Each chunk is a
// 4-byte big-endian length, a 4-byte type, the data, and a CRC-32 of the type and data.
// The image data is the rows top to bottom, each prefixed by a filter-type byte (0 = none),
// compressed as one zlib stream.

import { crc32, deflateSync } from 'node:zlib'

const SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])

function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length)
  out.writeUInt32BE(data.length, 0)
  out.write(type, 4, 'ascii')
  data.copy(out, 8)
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length)
  return out
}

// pixels: width * height bytes, row 0 at the top of the image.
export function encodeGrayPng(width, height, pixels) {
  if (!(width > 0 && height > 0) || pixels.length !== width * height) {
    throw new Error(`bad image size ${width}x${height} for ${pixels.length} pixels`)
  }
  const header = Buffer.alloc(13)
  header.writeUInt32BE(width, 0)
  header.writeUInt32BE(height, 4)
  header[8] = 8 // bit depth
  header[9] = 0 // color type: grayscale
  header[10] = 0 // compression: deflate
  header[11] = 0 // filter method 0
  header[12] = 0 // no interlace

  const raw = Buffer.alloc(height * (width + 1))
  for (let row = 0; row < height; row++) {
    const start = row * (width + 1)
    raw[start] = 0 // filter type: none
    raw.set(pixels.subarray(row * width, (row + 1) * width), start + 1)
  }

  return Buffer.concat([
    SIGNATURE,
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ])
}
