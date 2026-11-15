"""Integer display zoom and image replacement across console mode changes."""


def integer_zoom(size, screen_size, maximum=3):
    width, height = size
    screen_width, screen_height = screen_size
    return max(1, min(maximum, max(1, screen_width - 32) // width,
                      max(1, screen_height - 96) // height))


def present(canvas, image, screen_size, photo_factory):
    zoom = integer_zoom(image.size, screen_size)
    size = image.width * zoom, image.height * zoom
    from PIL import Image
    scaled = image.resize(size, Image.Resampling.NEAREST)
    previous = getattr(canvas, 'image', None)
    if previous is not None and (previous.width(), previous.height()) == size:
        previous.paste(scaled)
        return False
    canvas.image = photo_factory(scaled)
    canvas.configure(image=canvas.image)
    return True
