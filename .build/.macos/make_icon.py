import icnsutil

img = icnsutil.IcnsFile()
img.add_media(file='icon_16x16.png')
img.add_media(file='icon_32x32.png')
img.add_media(file='icon_128x128.png')
img.write('./ecat.icns')
