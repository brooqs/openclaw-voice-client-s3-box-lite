from PIL import Image, ImageOps
import sys

img_path = '/home/brooqs/projects/esp-s3-box-lite/jarvis-display.jpeg'
out_path = '/home/brooqs/projects/esp-s3-box-lite/esp32_openclaw_client/main/jarvis_image.h'

img = Image.open(img_path).convert('RGB')
# Resize and crop to fill 320x240
img = ImageOps.fit(img, (320, 240), Image.Resampling.LANCZOS)

with open(out_path, 'w') as f:
    f.write('#pragma once\n')
    f.write('#include <stdint.h>\n\n')
    f.write('const uint8_t jarvis_image_data[320*240*2] = {\n')
    count = 0
    for y in range(240):
        for x in range(320):
            r, g, b = img.getpixel((x, y))
            # RGB565 calculation
            val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            # Big-endian formatting (BSP_LCD_BIGENDIAN = 1)
            high_byte = (val >> 8) & 0xFF
            low_byte = val & 0xFF
            f.write(f'0x{high_byte:02x}, 0x{low_byte:02x}, ')
            count += 2
            if count % 16 == 0:
                f.write('\n')
    f.write('};\n')
