#pragma once

#include "display.hpp"

#include <span>

// HUB75E driver for the ESP32-S3 LCD_CAM peripheral (i80 mode, 16 bit) fed by GDMA.
//
// The whole panel is described by one DMA image. Every 16 bit word of that image is
// one pixel-clock tick on the panel and carries all HUB75 signals:
//
//   bit  0..5   R1 G1 B1 R2 G2 B2
//   bit  6      LAT
//   bit  7      OE
//   bit  8..12  A B C D E
//
// CLK is the LCD pixel clock (WR). The DMA descriptor chain loops over the image
// forever, so once started the CPU is not involved in refreshing the panel.
//
// Colour depth comes from binary code modulation: each scan row is shifted out once
// per bit plane and displayed for a time proportional to the plane's weight. The
// display (OE) periods all point at one shared constant buffer per row, so only the
// shift data is stored per plane.
//
// Two images are kept. update() renders into the idle one and then relinks the
// descriptor chain so the DMA switches to it at the next frame boundary.
namespace hub75
{

int init();

// Blocks until the panel is showing the new frame (at most about two refresh periods).
void update(std::span<const display::Color> framebuffer);

} // namespace hub75
