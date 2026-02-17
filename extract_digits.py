from PIL import Image
import os

# Load the screenshots
img1 = Image.open('IC7300-screenshots/numbers-out-band-tx.png')  # Has 1,2,3,4,5,6,7,8
img2 = Image.open('IC7300-screenshots/frequency-input.png')      # Has 0,7,1,3

# Digit coordinates in numbers-out-band-tx.png (12.345.678)
# Measured from the screenshot - frequency display area
# Format: (left, top, right, bottom)
coords_img1 = {
    '1': (155, 40, 185, 85),   # First '1'
    '2': (185, 40, 220, 85),   # '2'
    '3': (235, 40, 270, 85),   # '3' (after first dot)
    '4': (270, 40, 305, 85),   # '4'
    '5': (305, 40, 340, 85),   # '5'
    '6': (355, 40, 390, 85),   # '6' (after second dot)
    '7': (390, 40, 425, 85),   # '7'
    '8': (425, 40, 460, 85),   # '8'
    'dot': (220, 65, 235, 85), # Decimal separator
}

# Digit coordinates in frequency-input.png (7.113.000)
coords_img2 = {
    '0': (285, 30, 325, 75),   # '0' from "000"
    '9': (270, 160, 310, 205), # '9' button (we'll use this if no 9 in display)
}

# Create output directory
os.makedirs('resources/web/digits', exist_ok=True)

# Extract digits from first image
for digit, coords in coords_img1.items():
    cropped = img1.crop(coords)
    cropped.save(f'resources/web/digits/{digit}.png')
    print(f'Extracted {digit}: {coords}')

# Extract 0 from second image  
for digit, coords in coords_img2.items():
    cropped = img2.crop(coords)
    cropped.save(f'resources/web/digits/{digit}.png')
    print(f'Extracted {digit}: {coords}')

# Now let's find the 9 - check other screenshots
for fname in ['vfoB-split.png', 'strong-signal.png', 'NB-on.png']:
    try:
        img = Image.open(f'IC7300-screenshots/{fname}')
        print(f'\nChecking {fname}: {img.size}')
        # We'll need to manually identify if there's a 9 in these
    except:
        pass

print('\nDigit extraction complete!')
print('Note: We may need to manually locate digit 9 in one of the other screenshots')
